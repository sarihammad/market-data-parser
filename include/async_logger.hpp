#pragma once

#include "mpmc_queue.hpp"
#include "itch_protocol.hpp"
#include <atomic>
#include <thread>
#include <string>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <cstring>

namespace fast_market {

/**
 * High-Performance Asynchronous Logger
 * Uses MPMC queue to decouple parsing from I/O
 * Supports both O_DIRECT and memory-mapped file modes
 */
class AsyncLogger {
public:
    static constexpr size_t QUEUE_SIZE = 1024 * 1024;  // 1M messages
    static constexpr size_t BUFFER_SIZE = 4096 * 1024; // 4MB write buffer
    static constexpr size_t ALIGNMENT = 4096;          // Page alignment for O_DIRECT
    
    enum class WriteMode {
        MMAP,      // Memory-mapped file (default)
        DIRECT,    // Direct I/O (bypasses page cache)
        BUFFERED   // Standard buffered I/O
    };
    
    AsyncLogger(const std::string& filename, WriteMode mode = WriteMode::MMAP)
        : filename_(filename)
        , write_mode_(mode)
        , running_(false)
        , total_written_(0)
        , buffer_offset_(0)
    {
        // Allocate aligned buffer for O_DIRECT
        if (posix_memalign(reinterpret_cast<void**>(&write_buffer_), ALIGNMENT, BUFFER_SIZE) != 0) {
            throw std::runtime_error("Failed to allocate aligned buffer");
        }
    }
    
    ~AsyncLogger() {
        stop();
        if (write_buffer_) {
            free(write_buffer_);
        }
    }
    
    // Non-copyable, non-movable
    AsyncLogger(const AsyncLogger&) = delete;
    AsyncLogger& operator=(const AsyncLogger&) = delete;
    
    /**
     * Start the logger thread
     */
    void start() {
        if (running_.load(std::memory_order_acquire)) {
            return;
        }
        
        open_file();
        running_.store(true, std::memory_order_release);
        worker_thread_ = std::thread(&AsyncLogger::worker_loop, this);
    }
    
    /**
     * Stop the logger thread and flush remaining data
     */
    void stop() {
        if (!running_.load(std::memory_order_acquire)) {
            return;
        }
        
        running_.store(false, std::memory_order_release);
        
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
        
        flush();
        close_file();
    }
    
    /**
     * Enqueue a message for logging
     * Non-blocking, returns false if queue is full
     */
    [[nodiscard]] bool log(const ParsedMessage& msg) noexcept {
        return queue_.try_enqueue(msg);
    }
    
    /**
     * Get statistics
     */
    [[nodiscard]] size_t get_total_written() const noexcept {
        return total_written_.load(std::memory_order_relaxed);
    }
    
    [[nodiscard]] size_t get_queue_size() const noexcept {
        return queue_.size();
    }

private:
    void open_file() {
        int flags = O_WRONLY | O_CREAT | O_TRUNC;
        
        if (write_mode_ == WriteMode::DIRECT) {
            flags |= O_DIRECT;
        }
        
        fd_ = ::open(filename_.c_str(), flags, 0644);
        if (fd_ < 0) {
            throw std::runtime_error("Failed to open file: " + filename_);
        }
        
        // Set up memory-mapped file if requested
        if (write_mode_ == WriteMode::MMAP) {
            // Pre-allocate file
            const size_t initial_size = 1024 * 1024 * 1024; // 1GB
            if (ftruncate(fd_, initial_size) != 0) {
                ::close(fd_);
                throw std::runtime_error("Failed to allocate file");
            }
            
            // Map the file
            mmap_ptr_ = static_cast<uint8_t*>(
                mmap(nullptr, initial_size, PROT_WRITE, MAP_SHARED, fd_, 0)
            );
            
            if (mmap_ptr_ == MAP_FAILED) {
                ::close(fd_);
                throw std::runtime_error("Failed to mmap file");
            }
            
            mmap_size_ = initial_size;
            
            // Advise kernel about access pattern
            madvise(mmap_ptr_, mmap_size_, MADV_SEQUENTIAL);
        }
    }
    
    void close_file() {
        if (write_mode_ == WriteMode::MMAP && mmap_ptr_ != nullptr) {
            // Sync and unmap
            msync(mmap_ptr_, mmap_size_, MS_SYNC);
            munmap(mmap_ptr_, mmap_size_);
            
            // Truncate to actual size
            ftruncate(fd_, total_written_);
        }
        
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }
    
    void flush() {
        if (buffer_offset_ == 0) {
            return;
        }
        
        if (write_mode_ == WriteMode::MMAP) {
            // Already written to mmap, just update offset
            buffer_offset_ = 0;
        } else {
            // Write buffer to disk
            size_t bytes_to_write = buffer_offset_;
            
            // For O_DIRECT, must write in multiples of block size
            if (write_mode_ == WriteMode::DIRECT) {
                bytes_to_write = (bytes_to_write + ALIGNMENT - 1) & ~(ALIGNMENT - 1);
            }
            
            ssize_t written = ::write(fd_, write_buffer_, bytes_to_write);
            if (written > 0) {
                total_written_.fetch_add(buffer_offset_, std::memory_order_relaxed);
            }
            
            buffer_offset_ = 0;
        }
    }
    
    void write_message(const ParsedMessage& msg) {
        // Serialize message to buffer
        size_t msg_size = get_message_size(msg);
        
        if (write_mode_ == WriteMode::MMAP) {
            // Write directly to memory-mapped file
            if (total_written_ + msg_size > mmap_size_) {
                // Need to expand the mapping
                expand_mmap();
            }
            
            serialize_message(mmap_ptr_ + total_written_, msg);
            total_written_ += msg_size;
        } else {
            // Write to buffer
            if (buffer_offset_ + msg_size > BUFFER_SIZE) {
                flush();
            }
            
            serialize_message(write_buffer_ + buffer_offset_, msg);
            buffer_offset_ += msg_size;
        }
    }
    
    void expand_mmap() {
        // Double the size
        size_t new_size = mmap_size_ * 2;
        
        // Sync current data
        msync(mmap_ptr_, mmap_size_, MS_SYNC);
        munmap(mmap_ptr_, mmap_size_);
        
        // Expand file
        ftruncate(fd_, new_size);
        
        // Remap
        mmap_ptr_ = static_cast<uint8_t*>(
            mmap(nullptr, new_size, PROT_WRITE, MAP_SHARED, fd_, 0)
        );
        
        if (mmap_ptr_ == MAP_FAILED) {
            throw std::runtime_error("Failed to expand mmap");
        }
        
        mmap_size_ = new_size;
        madvise(mmap_ptr_, mmap_size_, MADV_SEQUENTIAL);
    }
    
    size_t get_message_size(const ParsedMessage& msg) const {
        switch (msg.type) {
            case MessageType::ADD_ORDER:
                return sizeof(AddOrderMessage);
            case MessageType::EXECUTE_ORDER:
                return sizeof(ExecuteOrderMessage);
            case MessageType::EXECUTE_ORDER_WITH_PRICE:
                return sizeof(ExecuteOrderWithPriceMessage);
            case MessageType::ORDER_CANCEL:
                return sizeof(OrderCancelMessage);
            case MessageType::ORDER_DELETE:
                return sizeof(OrderDeleteMessage);
            case MessageType::ORDER_REPLACE:
                return sizeof(OrderReplaceMessage);
            case MessageType::TRADE:
                return sizeof(TradeMessage);
            case MessageType::SYSTEM_EVENT:
                return sizeof(SystemEventMessage);
            case MessageType::STOCK_DIRECTORY:
                return sizeof(StockDirectoryMessage);
            default:
                return 0;
        }
    }
    
    void serialize_message(uint8_t* dest, const ParsedMessage& msg) {
        // Simple binary serialization - just copy the struct
        // In production, you might want a more sophisticated format
        switch (msg.type) {
            case MessageType::ADD_ORDER:
                std::memcpy(dest, &msg.add_order, sizeof(AddOrderMessage));
                break;
            case MessageType::EXECUTE_ORDER:
                std::memcpy(dest, &msg.execute_order, sizeof(ExecuteOrderMessage));
                break;
            case MessageType::EXECUTE_ORDER_WITH_PRICE:
                std::memcpy(dest, &msg.execute_with_price, sizeof(ExecuteOrderWithPriceMessage));
                break;
            case MessageType::ORDER_CANCEL:
                std::memcpy(dest, &msg.order_cancel, sizeof(OrderCancelMessage));
                break;
            case MessageType::ORDER_DELETE:
                std::memcpy(dest, &msg.order_delete, sizeof(OrderDeleteMessage));
                break;
            case MessageType::ORDER_REPLACE:
                std::memcpy(dest, &msg.order_replace, sizeof(OrderReplaceMessage));
                break;
            case MessageType::TRADE:
                std::memcpy(dest, &msg.trade, sizeof(TradeMessage));
                break;
            case MessageType::SYSTEM_EVENT:
                std::memcpy(dest, &msg.system_event, sizeof(SystemEventMessage));
                break;
            case MessageType::STOCK_DIRECTORY:
                std::memcpy(dest, &msg.stock_directory, sizeof(StockDirectoryMessage));
                break;
        }
    }
    
    void worker_loop() {
        ParsedMessage msg;
        
        while (running_.load(std::memory_order_acquire)) {
            if (queue_.try_dequeue(msg)) {
                write_message(msg);
            } else {
                // Queue empty, flush and yield
                flush();
                std::this_thread::yield();
            }
        }
        
        // Drain remaining messages
        while (queue_.try_dequeue(msg)) {
            write_message(msg);
        }
    }
    
    // Member variables
    MPMCQueue<ParsedMessage, QUEUE_SIZE> queue_;
    std::string filename_;
    WriteMode write_mode_;
    std::atomic<bool> running_;
    std::thread worker_thread_;
    
    int fd_ = -1;
    uint8_t* mmap_ptr_ = nullptr;
    size_t mmap_size_ = 0;
    
    uint8_t* write_buffer_ = nullptr;
    size_t buffer_offset_;
    
    std::atomic<size_t> total_written_;
};

} // namespace fast_market
