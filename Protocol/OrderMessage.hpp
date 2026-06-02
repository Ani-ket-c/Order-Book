// ==========================================================================
//  Module 4: Binary Wire Protocol (ITCH-inspired)
//  File: Protocol/OrderMessage.hpp
//
//  Design a fixed-width binary message format (inspired by NASDAQ ITCH 5.0)
// ==========================================================================
#ifndef PROTOCOL_ORDER_MESSAGE_HPP
#define PROTOCOL_ORDER_MESSAGE_HPP

#include <cstdint>

namespace protocol {

#pragma pack(push, 1)

enum class MessageType : uint8_t {
    OrderAdd = 'A',
    OrderCancel = 'X',
    Execution = 'E'
};

struct OrderAddMessage {
    MessageType type{MessageType::OrderAdd};
    uint64_t timestamp{0};
    uint32_t order_id{0};
    uint8_t side{'B'}; // 'B' for Buy, 'S' for Sell
    uint64_t price{0}; // Fixed-point with 4 decimals
    uint32_t quantity{0};
    uint8_t order_type{'L'}; // 'L' Limit, 'M' Market, 'S' Stop
};
static_assert(sizeof(OrderAddMessage) == 1 + 8 + 4 + 1 + 8 + 4 + 1, "OrderAddMessage size mismatch");

struct OrderCancelMessage {
    MessageType type{MessageType::OrderCancel};
    uint64_t timestamp{0};
    uint32_t order_id{0};
};
static_assert(sizeof(OrderCancelMessage) == 1 + 8 + 4, "OrderCancelMessage size mismatch");

struct ExecutionReportMessage {
    MessageType type{MessageType::Execution};
    uint64_t timestamp{0};
    uint32_t maker_order_id{0};
    uint32_t taker_order_id{0};
    uint64_t exec_price{0}; // Fixed-point with 4 decimals
    uint32_t exec_qty{0};
};
static_assert(sizeof(ExecutionReportMessage) == 1 + 8 + 4 + 4 + 8 + 4, "ExecutionReportMessage size mismatch");

#pragma pack(pop)

// Price scaling factor for 4 decimal places
constexpr uint64_t kPriceMultiplier = 10000;

} // namespace protocol

#endif // PROTOCOL_ORDER_MESSAGE_HPP
