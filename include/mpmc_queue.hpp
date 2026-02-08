#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <new>
#include <memory>

namespace fast_market {

// Cache line size for modern x86-64 processors
inline constexpr size_t CACHE_LINE_SIZE = 64;

// Align to cache line to prevent false sharing
template<typename T>
struct alignas(CACHE_LINE_SIZE) AlignedType {
    T value;
};

/**
 * Lock-free Multiple Producer Multiple Consumer Queue
 * Optimized for single-producer, single-consumer but safe for MPMC
 * Uses cache-line padding to prevent false sharing
 */
template<typename T, size_t Capacity>
class MPMCQueue {
    static_assert((Capacity & (Capacity - 1)) == 0, "Capacity must be power of 2");
    
public:
    MPMCQueue() : head_(0), tail_(0) {
        // Initialize sequence numbers
        for (size_t i = 0; i < Capacity; ++i) {
            sequences_[i].value.store(i, std::memory_order_relaxed);
        }
    }
    
    ~MPMCQueue() = default;
    
    // Non-copyable, non-movable
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;
    
    /**
     * Try to enqueue an item
     * @return true if successful, false if queue is full
     */
    [[nodiscard]] bool try_enqueue(const T& item) noexcept {
        size_t pos = head_.value.load(std::memory_order_relaxed);
        
        for (;;) {
            size_t index = pos & (Capacity - 1);
            size_t seq = sequences_[index].value.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos);
            
            if (diff == 0) {
                // Slot is available, try to claim it
                if (head_.value.compare_exchange_weak(pos, pos + 1, 
                    std::memory_order_relaxed)) {
                    // Successfully claimed, write data
                    buffer_[index].value = item;
                    sequences_[index].value.store(pos + 1, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // Queue is full
                return false;
            } else {
                // Another thread is ahead, update pos
                pos = head_.value.load(std::memory_order_relaxed);
            }
        }
    }
    
    /**
     * Try to dequeue an item
     * @return true if successful, false if queue is empty
     */
    [[nodiscard]] bool try_dequeue(T& item) noexcept {
        size_t pos = tail_.value.load(std::memory_order_relaxed);
        
        for (;;) {
            size_t index = pos & (Capacity - 1);
            size_t seq = sequences_[index].value.load(std::memory_order_acquire);
            intptr_t diff = static_cast<intptr_t>(seq) - static_cast<intptr_t>(pos + 1);
            
            if (diff == 0) {
                // Item is available, try to claim it
                if (tail_.value.compare_exchange_weak(pos, pos + 1,
                    std::memory_order_relaxed)) {
                    // Successfully claimed, read data
                    item = buffer_[index].value;
                    sequences_[index].value.store(pos + Capacity, std::memory_order_release);
                    return true;
                }
            } else if (diff < 0) {
                // Queue is empty
                return false;
            } else {
                // Another thread is ahead, update pos
                pos = tail_.value.load(std::memory_order_relaxed);
            }
        }
    }
    
    [[nodiscard]] size_t size() const noexcept {
        size_t head = head_.value.load(std::memory_order_acquire);
        size_t tail = tail_.value.load(std::memory_order_acquire);
        return head - tail;
    }
    
    [[nodiscard]] bool empty() const noexcept {
        return size() == 0;
    }
    
private:
    // Cache-line aligned head and tail to prevent false sharing
    AlignedType<std::atomic<size_t>> head_;
    AlignedType<std::atomic<size_t>> tail_;
    
    // Data buffer and sequence numbers
    AlignedType<T> buffer_[Capacity];
    AlignedType<std::atomic<size_t>> sequences_[Capacity];
};

} // namespace fast_market
