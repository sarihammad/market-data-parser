#pragma once

#include <cstdint>
#include <string_view>
#include <array>

namespace fast_market {

// Ensure no padding in structs - critical for zero-copy parsing
#pragma pack(push, 1)

/**
 * NASDAQ ITCH 5.0 Message Types
 * Each message starts with a single byte message type
 */
enum class MessageType : uint8_t {
    SYSTEM_EVENT = 'S',
    STOCK_DIRECTORY = 'R',
    STOCK_TRADING_ACTION = 'H',
    REG_SHO_RESTRICTION = 'Y',
    MARKET_PARTICIPANT_POSITION = 'L',
    MWCB_DECLINE_LEVEL = 'V',
    MWCB_STATUS = 'W',
    IPO_QUOTING_PERIOD = 'K',
    LULD_AUCTION_COLLAR = 'J',
    OPERATIONAL_HALT = 'h',
    ADD_ORDER = 'A',
    ADD_ORDER_MPID = 'F',
    EXECUTE_ORDER = 'E',
    EXECUTE_ORDER_WITH_PRICE = 'C',
    ORDER_CANCEL = 'X',
    ORDER_DELETE = 'D',
    ORDER_REPLACE = 'U',
    TRADE = 'P',
    CROSS_TRADE = 'Q',
    BROKEN_TRADE = 'B',
    NOII = 'I',
    RPII = 'N'
};

/**
 * Common header for all ITCH messages
 * Wire format is big-endian, we handle conversion during parsing
 */
struct ITCHMessageHeader {
    uint8_t message_type;
    uint16_t stock_locate;
    uint16_t tracking_number;
    uint64_t timestamp;  // Nanoseconds since midnight
} __attribute__((packed));

/**
 * Add Order Message (Type A)
 * Most common message type in ITCH feed
 */
struct AddOrderMessage {
    ITCHMessageHeader header;
    uint64_t order_reference_number;
    uint8_t buy_sell_indicator;  // 'B' or 'S'
    uint32_t shares;
    std::array<char, 8> stock;
    uint32_t price;  // Fixed point: divide by 10000 for actual price
} __attribute__((packed));

/**
 * Execute Order Message (Type E)
 * Second most common message type
 */
struct ExecuteOrderMessage {
    ITCHMessageHeader header;
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
} __attribute__((packed));

/**
 * Execute Order with Price Message (Type C)
 */
struct ExecuteOrderWithPriceMessage {
    ITCHMessageHeader header;
    uint64_t order_reference_number;
    uint32_t executed_shares;
    uint64_t match_number;
    uint8_t printable;  // 'Y' or 'N'
    uint32_t execution_price;
} __attribute__((packed));

/**
 * Order Cancel Message (Type X)
 */
struct OrderCancelMessage {
    ITCHMessageHeader header;
    uint64_t order_reference_number;
    uint32_t cancelled_shares;
} __attribute__((packed));

/**
 * Order Delete Message (Type D)
 */
struct OrderDeleteMessage {
    ITCHMessageHeader header;
    uint64_t order_reference_number;
} __attribute__((packed));

/**
 * Order Replace Message (Type U)
 */
struct OrderReplaceMessage {
    ITCHMessageHeader header;
    uint64_t original_order_reference_number;
    uint64_t new_order_reference_number;
    uint32_t shares;
    uint32_t price;
} __attribute__((packed));

/**
 * Trade Message (Type P)
 * Non-cross trade
 */
struct TradeMessage {
    ITCHMessageHeader header;
    uint64_t order_reference_number;
    uint8_t buy_sell_indicator;
    uint32_t shares;
    std::array<char, 8> stock;
    uint32_t price;
    uint64_t match_number;
} __attribute__((packed));

/**
 * System Event Message (Type S)
 */
struct SystemEventMessage {
    ITCHMessageHeader header;
    uint8_t event_code;  // 'O', 'S', 'Q', 'M', 'E', 'C'
} __attribute__((packed));

/**
 * Stock Directory Message (Type R)
 */
struct StockDirectoryMessage {
    ITCHMessageHeader header;
    std::array<char, 8> stock;
    uint8_t market_category;
    uint8_t financial_status_indicator;
    uint32_t round_lot_size;
    uint8_t round_lots_only;
    uint8_t issue_classification;
    std::array<char, 2> issue_sub_type;
    uint8_t authenticity;
    uint8_t short_sale_threshold_indicator;
    uint8_t ipo_flag;
    uint8_t luld_reference_price_tier;
    uint8_t etp_flag;
    uint32_t etp_leverage_factor;
    uint8_t inverse_indicator;
} __attribute__((packed));

#pragma pack(pop)

/**
 * Parsed message union for efficient storage
 * Uses a union to avoid heap allocations
 */
struct ParsedMessage {
    MessageType type;
    
    union {
        AddOrderMessage add_order;
        ExecuteOrderMessage execute_order;
        ExecuteOrderWithPriceMessage execute_with_price;
        OrderCancelMessage order_cancel;
        OrderDeleteMessage order_delete;
        OrderReplaceMessage order_replace;
        TradeMessage trade;
        SystemEventMessage system_event;
        StockDirectoryMessage stock_directory;
    };
    
    // Timestamp when parsed (for latency measurement)
    uint64_t parse_timestamp_ns;
};

/**
 * Convert big-endian to host byte order
 * These are marked [[likely]] for branch prediction optimization
 */
[[gnu::always_inline]] inline uint16_t ntoh16(uint16_t x) {
    return __builtin_bswap16(x);
}

[[gnu::always_inline]] inline uint32_t ntoh32(uint32_t x) {
    return __builtin_bswap32(x);
}

[[gnu::always_inline]] inline uint64_t ntoh64(uint64_t x) {
    return __builtin_bswap64(x);
}

/**
 * Fixed-point price to double conversion
 * Price is stored as integer with 4 decimal places
 */
[[gnu::always_inline]] inline double price_to_double(uint32_t price) {
    return static_cast<double>(price) / 10000.0;
}

/**
 * Extract stock symbol as string_view (zero-copy)
 */
[[gnu::always_inline]] inline std::string_view get_stock_symbol(const std::array<char, 8>& stock) {
    // Find the actual length (may be padded with spaces)
    size_t len = 8;
    while (len > 0 && stock[len - 1] == ' ') {
        --len;
    }
    return std::string_view(stock.data(), len);
}

} // namespace fast_market
