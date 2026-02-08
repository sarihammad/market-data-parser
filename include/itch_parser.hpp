#pragma once

#include "itch_protocol.hpp"
#include <cstring>
#include <optional>
#include <immintrin.h>  // For SIMD intrinsics

namespace fast_market {

/**
 * Zero-Copy ITCH Parser
 * Uses type punning to directly map wire format to structs
 * No heap allocations, minimal branching
 */
class ITCHParser {
public:
    ITCHParser() = default;
    ~ITCHParser() = default;
    
    /**
     * Parse a message from raw bytes
     * @param data Pointer to message start
     * @param length Length of message in bytes
     * @return Parsed message if valid, nullopt otherwise
     */
    [[nodiscard]] std::optional<ParsedMessage> parse(const uint8_t* data, size_t length) noexcept {
        if (length < sizeof(ITCHMessageHeader)) [[unlikely]] {
            return std::nullopt;
        }
        
        ParsedMessage msg;
        msg.type = static_cast<MessageType>(data[0]);
        
        // Use branch hints based on ITCH message frequency
        // Add Order (A) is the most common message type (~40% of all messages)
        if (msg.type == MessageType::ADD_ORDER) [[likely]] {
            if (length != sizeof(AddOrderMessage)) [[unlikely]] {
                return std::nullopt;
            }
            return parse_add_order(data, msg);
        }
        
        // Execute Order (E) is second most common (~25%)
        if (msg.type == MessageType::EXECUTE_ORDER) [[likely]] {
            if (length != sizeof(ExecuteOrderMessage)) [[unlikely]] {
                return std::nullopt;
            }
            return parse_execute_order(data, msg);
        }
        
        // Other message types (less common)
        switch (msg.type) {
            case MessageType::EXECUTE_ORDER_WITH_PRICE:
                if (length == sizeof(ExecuteOrderWithPriceMessage)) [[likely]] {
                    return parse_execute_with_price(data, msg);
                }
                break;
                
            case MessageType::ORDER_CANCEL:
                if (length == sizeof(OrderCancelMessage)) [[likely]] {
                    return parse_order_cancel(data, msg);
                }
                break;
                
            case MessageType::ORDER_DELETE:
                if (length == sizeof(OrderDeleteMessage)) [[likely]] {
                    return parse_order_delete(data, msg);
                }
                break;
                
            case MessageType::ORDER_REPLACE:
                if (length == sizeof(OrderReplaceMessage)) [[likely]] {
                    return parse_order_replace(data, msg);
                }
                break;
                
            case MessageType::TRADE:
                if (length == sizeof(TradeMessage)) [[likely]] {
                    return parse_trade(data, msg);
                }
                break;
                
            case MessageType::SYSTEM_EVENT:
                if (length == sizeof(SystemEventMessage)) [[likely]] {
                    return parse_system_event(data, msg);
                }
                break;
                
            case MessageType::STOCK_DIRECTORY:
                if (length == sizeof(StockDirectoryMessage)) [[likely]] {
                    return parse_stock_directory(data, msg);
                }
                break;
                
            default:
                // Unknown or unsupported message type
                return std::nullopt;
        }
        
        return std::nullopt;
    }
    
    /**
     * Get current timestamp in nanoseconds
     * Uses rdtsc for minimal overhead
     */
    [[gnu::always_inline]] static inline uint64_t get_timestamp_ns() noexcept {
        uint32_t lo, hi;
        __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
        return (static_cast<uint64_t>(hi) << 32) | lo;
    }

private:
    // Zero-copy parsing using type punning
    // We directly cast the memory, relying on packed structs
    
    [[gnu::always_inline]] 
    std::optional<ParsedMessage> parse_add_order(const uint8_t* data, ParsedMessage& msg) noexcept {
        // Type pun: reinterpret raw bytes as struct
        const auto* wire_msg = reinterpret_cast<const AddOrderMessage*>(data);
        
        // Convert from big-endian to host byte order
        msg.add_order.header.message_type = wire_msg->header.message_type;
        msg.add_order.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.add_order.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.add_order.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.add_order.order_reference_number = ntoh64(wire_msg->order_reference_number);
        msg.add_order.buy_sell_indicator = wire_msg->buy_sell_indicator;
        msg.add_order.shares = ntoh32(wire_msg->shares);
        msg.add_order.stock = wire_msg->stock;
        msg.add_order.price = ntoh32(wire_msg->price);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_execute_order(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const ExecuteOrderMessage*>(data);
        
        msg.execute_order.header.message_type = wire_msg->header.message_type;
        msg.execute_order.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.execute_order.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.execute_order.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.execute_order.order_reference_number = ntoh64(wire_msg->order_reference_number);
        msg.execute_order.executed_shares = ntoh32(wire_msg->executed_shares);
        msg.execute_order.match_number = ntoh64(wire_msg->match_number);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_execute_with_price(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const ExecuteOrderWithPriceMessage*>(data);
        
        msg.execute_with_price.header.message_type = wire_msg->header.message_type;
        msg.execute_with_price.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.execute_with_price.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.execute_with_price.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.execute_with_price.order_reference_number = ntoh64(wire_msg->order_reference_number);
        msg.execute_with_price.executed_shares = ntoh32(wire_msg->executed_shares);
        msg.execute_with_price.match_number = ntoh64(wire_msg->match_number);
        msg.execute_with_price.printable = wire_msg->printable;
        msg.execute_with_price.execution_price = ntoh32(wire_msg->execution_price);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_order_cancel(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const OrderCancelMessage*>(data);
        
        msg.order_cancel.header.message_type = wire_msg->header.message_type;
        msg.order_cancel.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.order_cancel.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.order_cancel.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.order_cancel.order_reference_number = ntoh64(wire_msg->order_reference_number);
        msg.order_cancel.cancelled_shares = ntoh32(wire_msg->cancelled_shares);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_order_delete(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const OrderDeleteMessage*>(data);
        
        msg.order_delete.header.message_type = wire_msg->header.message_type;
        msg.order_delete.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.order_delete.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.order_delete.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.order_delete.order_reference_number = ntoh64(wire_msg->order_reference_number);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_order_replace(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const OrderReplaceMessage*>(data);
        
        msg.order_replace.header.message_type = wire_msg->header.message_type;
        msg.order_replace.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.order_replace.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.order_replace.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.order_replace.original_order_reference_number = ntoh64(wire_msg->original_order_reference_number);
        msg.order_replace.new_order_reference_number = ntoh64(wire_msg->new_order_reference_number);
        msg.order_replace.shares = ntoh32(wire_msg->shares);
        msg.order_replace.price = ntoh32(wire_msg->price);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_trade(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const TradeMessage*>(data);
        
        msg.trade.header.message_type = wire_msg->header.message_type;
        msg.trade.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.trade.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.trade.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.trade.order_reference_number = ntoh64(wire_msg->order_reference_number);
        msg.trade.buy_sell_indicator = wire_msg->buy_sell_indicator;
        msg.trade.shares = ntoh32(wire_msg->shares);
        msg.trade.stock = wire_msg->stock;
        msg.trade.price = ntoh32(wire_msg->price);
        msg.trade.match_number = ntoh64(wire_msg->match_number);
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_system_event(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const SystemEventMessage*>(data);
        
        msg.system_event.header.message_type = wire_msg->header.message_type;
        msg.system_event.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.system_event.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.system_event.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.system_event.event_code = wire_msg->event_code;
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
    
    [[gnu::always_inline]]
    std::optional<ParsedMessage> parse_stock_directory(const uint8_t* data, ParsedMessage& msg) noexcept {
        const auto* wire_msg = reinterpret_cast<const StockDirectoryMessage*>(data);
        
        msg.stock_directory.header.message_type = wire_msg->header.message_type;
        msg.stock_directory.header.stock_locate = ntoh16(wire_msg->header.stock_locate);
        msg.stock_directory.header.tracking_number = ntoh16(wire_msg->header.tracking_number);
        msg.stock_directory.header.timestamp = ntoh64(wire_msg->header.timestamp);
        
        msg.stock_directory.stock = wire_msg->stock;
        msg.stock_directory.market_category = wire_msg->market_category;
        msg.stock_directory.financial_status_indicator = wire_msg->financial_status_indicator;
        msg.stock_directory.round_lot_size = ntoh32(wire_msg->round_lot_size);
        msg.stock_directory.round_lots_only = wire_msg->round_lots_only;
        msg.stock_directory.issue_classification = wire_msg->issue_classification;
        msg.stock_directory.issue_sub_type = wire_msg->issue_sub_type;
        msg.stock_directory.authenticity = wire_msg->authenticity;
        msg.stock_directory.short_sale_threshold_indicator = wire_msg->short_sale_threshold_indicator;
        msg.stock_directory.ipo_flag = wire_msg->ipo_flag;
        msg.stock_directory.luld_reference_price_tier = wire_msg->luld_reference_price_tier;
        msg.stock_directory.etp_flag = wire_msg->etp_flag;
        msg.stock_directory.etp_leverage_factor = ntoh32(wire_msg->etp_leverage_factor);
        msg.stock_directory.inverse_indicator = wire_msg->inverse_indicator;
        
        msg.parse_timestamp_ns = get_timestamp_ns();
        return msg;
    }
};

} // namespace fast_market
