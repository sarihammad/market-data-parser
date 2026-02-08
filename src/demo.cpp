#include "itch_parser.hpp"
#include "async_logger.hpp"
#include "system_utils.hpp"
#include <iostream>
#include <iomanip>
#include <vector>
#include <algorithm>
#include <cstring>
#include <memory>

using namespace fast_market;

// Helper to create sample messages
class DemoMessageGenerator {
public:
    std::vector<uint8_t> create_add_order(const std::string& symbol, char side, 
                                          uint32_t shares, uint32_t price_cents) {
        std::vector<uint8_t> msg(sizeof(AddOrderMessage));
        auto* order = reinterpret_cast<AddOrderMessage*>(msg.data());
        
        order->header.message_type = static_cast<uint8_t>(MessageType::ADD_ORDER);
        order->header.stock_locate = hton16(1);
        order->header.tracking_number = hton16(seq_++);
        order->header.timestamp = hton64(get_itch_timestamp());
        
        order->order_reference_number = hton64(100000 + seq_);
        order->buy_sell_indicator = side;
        order->shares = hton32(shares);
        
        // Pad symbol to 8 characters
        std::string padded_symbol = symbol;
        padded_symbol.resize(8, ' ');
        std::memcpy(order->stock.data(), padded_symbol.c_str(), 8);
        
        order->price = hton32(price_cents);
        
        return msg;
    }
    
    std::vector<uint8_t> create_trade(const std::string& symbol, char side,
                                      uint32_t shares, uint32_t price_cents) {
        std::vector<uint8_t> msg(sizeof(TradeMessage));
        auto* trade = reinterpret_cast<TradeMessage*>(msg.data());
        
        trade->header.message_type = static_cast<uint8_t>(MessageType::TRADE);
        trade->header.stock_locate = hton16(1);
        trade->header.tracking_number = hton16(seq_++);
        trade->header.timestamp = hton64(get_itch_timestamp());
        
        trade->order_reference_number = hton64(100000 + seq_);
        trade->buy_sell_indicator = side;
        trade->shares = hton32(shares);
        
        std::string padded_symbol = symbol;
        padded_symbol.resize(8, ' ');
        std::memcpy(trade->stock.data(), padded_symbol.c_str(), 8);
        
        trade->price = hton32(price_cents);
        trade->match_number = hton64(500000 + seq_);
        
        return msg;
    }

private:
    uint64_t get_itch_timestamp() {
        // ITCH timestamp is nanoseconds since midnight
        auto now = std::chrono::system_clock::now();
        auto duration = now.time_since_epoch();
        auto midnight = duration - std::chrono::hours(24);  // Approximate
        return std::chrono::duration_cast<std::chrono::nanoseconds>(midnight).count();
    }
    
    uint16_t hton16(uint16_t x) { return __builtin_bswap16(x); }
    uint32_t hton32(uint32_t x) { return __builtin_bswap32(x); }
    uint64_t hton64(uint64_t x) { return __builtin_bswap64(x); }
    
    uint32_t seq_ = 0;
};

void print_message(const ParsedMessage& msg) {
    std::cout << std::setprecision(4) << std::fixed;
    
    switch (msg.type) {
        case MessageType::ADD_ORDER: {
            auto symbol = get_stock_symbol(msg.add_order.stock);
            auto price = price_to_double(msg.add_order.price);
            char side = msg.add_order.buy_sell_indicator;
            
            std::cout << "ADD ORDER: " << symbol 
                     << " " << (side == 'B' ? "BUY" : "SELL")
                     << " " << msg.add_order.shares << " @ $" << price
                     << " (Ref: " << msg.add_order.order_reference_number << ")\n";
            break;
        }
        
        case MessageType::TRADE: {
            auto symbol = get_stock_symbol(msg.trade.stock);
            auto price = price_to_double(msg.trade.price);
            char side = msg.trade.buy_sell_indicator;
            
            std::cout << "TRADE: " << symbol
                     << " " << (side == 'B' ? "BUY" : "SELL")
                     << " " << msg.trade.shares << " @ $" << price
                     << " (Match: " << msg.trade.match_number << ")\n";
            break;
        }
        
        case MessageType::EXECUTE_ORDER: {
            std::cout << "EXECUTE: Ref " << msg.execute_order.order_reference_number
                     << " executed " << msg.execute_order.executed_shares << " shares"
                     << " (Match: " << msg.execute_order.match_number << ")\n";
            break;
        }
        
        case MessageType::SYSTEM_EVENT: {
            std::cout << "SYSTEM EVENT: " << static_cast<char>(msg.system_event.event_code) << "\n";
            break;
        }
        
        default:
            std::cout << "Message type: " << static_cast<int>(msg.type) << "\n";
            break;
    }
}

void demo_basic_parsing() {
    std::cout << "\n=== Demo 1: Basic Message Parsing ===\n\n";
    
    DemoMessageGenerator gen;
    ITCHParser parser;
    
    // Create some sample messages
    auto msg1 = gen.create_add_order("AAPL", 'B', 100, 1500000);  // Buy 100 AAPL @ $150.00
    auto msg2 = gen.create_add_order("MSFT", 'S', 50, 3200000);   // Sell 50 MSFT @ $320.00
    auto msg3 = gen.create_trade("GOOGL", 'B', 25, 1400000);      // Trade 25 GOOGL @ $140.00
    
    // Parse and display
    std::cout << "Parsing messages...\n\n";
    
    if (auto parsed = parser.parse(msg1.data(), msg1.size())) {
        print_message(*parsed);
    }
    
    if (auto parsed = parser.parse(msg2.data(), msg2.size())) {
        print_message(*parsed);
    }
    
    if (auto parsed = parser.parse(msg3.data(), msg3.size())) {
        print_message(*parsed);
    }
}

void demo_async_logging() {
    std::cout << "\n=== Demo 2: Async Logging ===\n\n";
    
    DemoMessageGenerator gen;
    ITCHParser parser;
    
    // Use heap allocation and buffered I/O
    auto logger = std::make_unique<AsyncLogger>("demo_output.bin", AsyncLogger::WriteMode::BUFFERED);
    
    std::cout << "Starting async logger...\n";
    logger->start();
    
    std::cout << "Processing 1000 messages...\n";
    
    for (int i = 0; i < 1000; ++i) {
        auto msg = (i % 2 == 0) 
            ? gen.create_add_order("TSLA", 'B', 100 + i, 2500000 + i * 100)
            : gen.create_trade("NVDA", 'S', 50 + i, 5000000 + i * 100);
        
        if (auto parsed = parser.parse(msg.data(), msg.size())) {
            while (!logger->log(*parsed)) {
                std::this_thread::yield();  // Wait if queue is full
            }
        }
    }
    
    std::cout << "Queue size before stop: " << logger->get_queue_size() << "\n";
    std::cout << "Stopping logger...\n";
    logger->stop();
    
    std::cout << "Total bytes written: " << logger->get_total_written() << "\n";
}

void demo_performance_features() {
    std::cout << "\n=== Demo 3: Performance Features ===\n\n";
    
    std::cout << "System Information:\n";
    std::cout << "  CPU cores: " << SystemUtils::get_cpu_count() << "\n";
    
    uint64_t tsc_freq = SystemUtils::get_tsc_frequency();
    std::cout << "  TSC frequency: ~" << (tsc_freq / 1000000) << " MHz\n";
    
    std::cout << "  Huge pages available: " 
              << (SystemUtils::has_huge_pages() ? "Yes" : "No") << "\n";
    
    std::cout << "\nTesting CPU pinning...\n";
    {
        ScopedCPUPin pin(0);
        if (pin.is_pinned()) {
            std::cout << "  Successfully pinned to CPU 0\n";
        } else {
            std::cout << "  Failed to pin (may need privileges)\n";
        }
    }
    
    std::cout << "\nMeasuring parse latency (10,000 iterations)...\n";
    
    DemoMessageGenerator gen;
    ITCHParser parser;
    auto msg = gen.create_add_order("AAPL", 'B', 100, 1500000);
    
    SystemUtils::warmup_cpu(50);
    
    std::vector<uint64_t> latencies;
    for (int i = 0; i < 10000; ++i) {
        uint64_t start = SystemUtils::rdtscp();
        auto parsed = parser.parse(msg.data(), msg.size());
        uint64_t end = SystemUtils::rdtscp();
        
        if (parsed) {
            latencies.push_back(end - start);
        }
    }
    
    std::sort(latencies.begin(), latencies.end());
    
    std::cout << "  Min latency: " << latencies.front() << " cycles\n";
    std::cout << "  Median latency: " << latencies[latencies.size() / 2] << " cycles\n";
    std::cout << "  99th percentile: " << latencies[latencies.size() * 99 / 100] << " cycles\n";
    std::cout << "  Max latency: " << latencies.back() << " cycles\n";
    
    // Convert to nanoseconds (approximate)
    double cycles_to_ns = 1000000000.0 / tsc_freq;
    std::cout << "\n  Median latency: ~" << (latencies[latencies.size() / 2] * cycles_to_ns) 
              << " ns\n";
}

int main() {
    std::cout << "======================================\n";
    std::cout << " Fast Market Data Parser - Demo\n";
    std::cout << " Zero-Copy NASDAQ ITCH 5.0 Parser\n";
    std::cout << "======================================\n";
    
    demo_basic_parsing();
    demo_async_logging();
    demo_performance_features();
    
    std::cout << "\n=== Demo Complete ===\n";
    
    return 0;
}
