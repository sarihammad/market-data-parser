#include "itch_parser.hpp"
#include "async_logger.hpp"
#include "system_utils.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <chrono>
#include <algorithm>
#include <numeric>
#include <memory>
#include <cstring>

using namespace fast_market;

// Generate synthetic ITCH messages for testing
class MessageGenerator {
public:
    std::vector<uint8_t> generate_add_order() {
        std::vector<uint8_t> msg(sizeof(AddOrderMessage));
        auto* add_order = reinterpret_cast<AddOrderMessage*>(msg.data());
        
        add_order->header.message_type = static_cast<uint8_t>(MessageType::ADD_ORDER);
        add_order->header.stock_locate = hton16(1);
        add_order->header.tracking_number = hton16(counter_++);
        add_order->header.timestamp = hton64(SystemUtils::rdtsc());
        
        add_order->order_reference_number = hton64(1000000 + counter_);
        add_order->buy_sell_indicator = 'B';
        add_order->shares = hton32(100);
        std::memcpy(add_order->stock.data(), "AAPL    ", 8);
        add_order->price = hton32(1500000);  // $150.00
        
        return msg;
    }
    
    std::vector<uint8_t> generate_execute_order() {
        std::vector<uint8_t> msg(sizeof(ExecuteOrderMessage));
        auto* exec = reinterpret_cast<ExecuteOrderMessage*>(msg.data());
        
        exec->header.message_type = static_cast<uint8_t>(MessageType::EXECUTE_ORDER);
        exec->header.stock_locate = hton16(1);
        exec->header.tracking_number = hton16(counter_++);
        exec->header.timestamp = hton64(SystemUtils::rdtsc());
        
        exec->order_reference_number = hton64(1000000 + counter_);
        exec->executed_shares = hton32(50);
        exec->match_number = hton64(5000000 + counter_);
        
        return msg;
    }
    
private:
    uint16_t hton16(uint16_t x) { return __builtin_bswap16(x); }
    uint32_t hton32(uint32_t x) { return __builtin_bswap32(x); }
    uint64_t hton64(uint64_t x) { return __builtin_bswap64(x); }
    
    uint32_t counter_ = 0;
};

// Statistics tracker
struct Stats {
    std::vector<uint64_t> latencies;
    uint64_t total_messages = 0;
    uint64_t total_bytes = 0;
    uint64_t start_time = 0;
    uint64_t end_time = 0;
    
    void add_latency(uint64_t latency_ns) {
        latencies.push_back(latency_ns);
    }
    
    void print_summary(uint64_t tsc_freq) {
        if (latencies.empty()) {
            std::cout << "No data collected\n";
            return;
        }
        
        std::sort(latencies.begin(), latencies.end());
        
        double total_time_sec = static_cast<double>(end_time - start_time) / tsc_freq;
        double throughput = total_messages / total_time_sec;
        double bandwidth_mbps = (total_bytes / total_time_sec) / (1024 * 1024);
        
        auto percentile = [this](double p) {
            size_t idx = static_cast<size_t>(latencies.size() * p);
            return latencies[idx];
        };
        
        std::cout << "\n=== Performance Results ===\n";
        std::cout << std::fixed << std::setprecision(2);
        std::cout << "Total messages: " << total_messages << "\n";
        std::cout << "Total time: " << total_time_sec << " seconds\n";
        std::cout << "Throughput: " << throughput << " messages/sec\n";
        std::cout << "Throughput: " << (throughput / 1000000.0) << " M messages/sec\n";
        std::cout << "Bandwidth: " << bandwidth_mbps << " MB/s\n";
        std::cout << "\nLatency Percentiles (nanoseconds):\n";
        std::cout << "  Min:    " << latencies.front() << " ns\n";
        std::cout << "  50th:   " << percentile(0.50) << " ns\n";
        std::cout << "  90th:   " << percentile(0.90) << " ns\n";
        std::cout << "  99th:   " << percentile(0.99) << " ns\n";
        std::cout << "  99.9th: " << percentile(0.999) << " ns\n";
        std::cout << "  Max:    " << latencies.back() << " ns\n";
        
        double avg = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
        std::cout << "  Avg:    " << avg << " ns\n";
    }
};

void benchmark_parser_only(size_t num_messages) {
    std::cout << "\n=== Benchmark 1: Parser Only (No I/O) ===\n";
    std::cout << "Messages to parse: " << num_messages << "\n";
    
    MessageGenerator gen;
    ITCHParser parser;
    Stats stats;
    
    // Pre-generate messages
    std::vector<std::vector<uint8_t>> messages;
    messages.reserve(num_messages);
    
    for (size_t i = 0; i < num_messages; ++i) {
        if (i % 2 == 0) {
            messages.push_back(gen.generate_add_order());
        } else {
            messages.push_back(gen.generate_execute_order());
        }
    }
    
    std::cout << "Warming up CPU...\n";
    SystemUtils::warmup_cpu(100);
    
    std::cout << "Parsing...\n";
    uint64_t tsc_freq = SystemUtils::get_tsc_frequency();
    stats.start_time = SystemUtils::rdtscp();
    
    for (const auto& msg : messages) {
        uint64_t parse_start = SystemUtils::rdtscp();
        auto parsed = parser.parse(msg.data(), msg.size());
        uint64_t parse_end = SystemUtils::rdtscp();
        
        if (parsed) {
            uint64_t latency = parse_end - parse_start;
            stats.add_latency(latency);
            stats.total_messages++;
            stats.total_bytes += msg.size();
        }
    }
    
    stats.end_time = SystemUtils::rdtscp();
    stats.print_summary(tsc_freq);
}

void benchmark_parser_with_logger(size_t num_messages) {
    std::cout << "\n=== Benchmark 2: Parser + Async Logger ===\n";
    std::cout << "Messages to parse: " << num_messages << "\n";
    
    MessageGenerator gen;
    ITCHParser parser;
    auto logger = std::make_unique<AsyncLogger>("benchmark_output.bin", AsyncLogger::WriteMode::BUFFERED);
    Stats stats;
    
    // Pre-generate messages
    std::vector<std::vector<uint8_t>> messages;
    messages.reserve(num_messages);
    
    for (size_t i = 0; i < num_messages; ++i) {
        if (i % 2 == 0) {
            messages.push_back(gen.generate_add_order());
        } else {
            messages.push_back(gen.generate_execute_order());
        }
    }
    
    std::cout << "Starting async logger...\n";
    logger->start();
    
    std::cout << "Warming up CPU...\n";
    SystemUtils::warmup_cpu(100);
    
    std::cout << "Parsing and logging...\n";
    uint64_t tsc_freq = SystemUtils::get_tsc_frequency();
    stats.start_time = SystemUtils::rdtscp();
    
    for (const auto& msg : messages) {
        uint64_t parse_start = SystemUtils::rdtscp();
        auto parsed = parser.parse(msg.data(), msg.size());
        
        if (parsed) {
            while (!logger->log(*parsed)) {
                std::this_thread::yield();
            }
            uint64_t parse_end = SystemUtils::rdtscp();
            
            uint64_t latency = parse_end - parse_start;
            stats.add_latency(latency);
            stats.total_messages++;
            stats.total_bytes += msg.size();
        }
    }
    
    stats.end_time = SystemUtils::rdtscp();
    
    std::cout << "Stopping logger (flushing remaining data)...\n";
    logger->stop();
    
    std::cout << "Logger wrote " << logger->get_total_written() << " bytes\n";
    stats.print_summary(tsc_freq);
}

void benchmark_with_cpu_pinning(size_t num_messages) {
    std::cout << "\n=== Benchmark 3: Parser with CPU Pinning ===\n";
    std::cout << "Messages to parse: " << num_messages << "\n";
    
    int cpu_count = SystemUtils::get_cpu_count();
    std::cout << "Available CPUs: " << cpu_count << "\n";
    std::cout << "Pinning to CPU 0\n";
    
    ScopedCPUPin pin(0);
    if (!pin.is_pinned()) {
        std::cout << "Warning: Failed to pin thread to CPU\n";
    }
    
    MessageGenerator gen;
    ITCHParser parser;
    Stats stats;
    
    // Pre-generate messages
    std::vector<std::vector<uint8_t>> messages;
    messages.reserve(num_messages);
    
    for (size_t i = 0; i < num_messages; ++i) {
        if (i % 2 == 0) {
            messages.push_back(gen.generate_add_order());
        } else {
            messages.push_back(gen.generate_execute_order());
        }
    }
    
    std::cout << "Warming up CPU...\n";
    SystemUtils::warmup_cpu(100);
    
    std::cout << "Parsing with CPU affinity...\n";
    uint64_t tsc_freq = SystemUtils::get_tsc_frequency();
    stats.start_time = SystemUtils::rdtscp();
    
    for (const auto& msg : messages) {
        uint64_t parse_start = SystemUtils::rdtscp();
        auto parsed = parser.parse(msg.data(), msg.size());
        uint64_t parse_end = SystemUtils::rdtscp();
        
        if (parsed) {
            uint64_t latency = parse_end - parse_start;
            stats.add_latency(latency);
            stats.total_messages++;
            stats.total_bytes += msg.size();
        }
    }
    
    stats.end_time = SystemUtils::rdtscp();
    stats.print_summary(tsc_freq);
}

int main(int argc, char* argv[]) {
    size_t num_messages = 10000000;  // 10M messages by default
    
    if (argc > 1) {
        num_messages = std::stoull(argv[1]);
    }
    
    std::cout << "=== Fast Market Data Parser Benchmark ===\n";
    std::cout << "C++20 Zero-Copy NASDAQ ITCH Parser\n";
    std::cout << "CPU Count: " << SystemUtils::get_cpu_count() << "\n";
    
    uint64_t tsc_freq = SystemUtils::get_tsc_frequency();
    std::cout << "TSC Frequency: ~" << (tsc_freq / 1000000) << " MHz\n";
    
    // Run benchmarks
    benchmark_parser_only(num_messages);
    benchmark_parser_with_logger(num_messages);
    benchmark_with_cpu_pinning(num_messages);
    
    std::cout << "\n=== All Benchmarks Complete ===\n";
    
    return 0;
}
