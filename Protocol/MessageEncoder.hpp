// ==========================================================================
//  Module 4: Binary Wire Protocol (ITCH-inspired)
//  File: Protocol/MessageEncoder.hpp
//
//  Encoder/Decoder for zero-copy deserialization using reinterpret_cast
//  on aligned buffers, and a FeedHandler to dispatch messages to the LOB.
// ==========================================================================
#ifndef PROTOCOL_MESSAGE_ENCODER_HPP
#define PROTOCOL_MESSAGE_ENCODER_HPP

#include "OrderMessage.hpp"
#include "Limit_Order_Book/Book.hpp"
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace protocol {

class MessageEncoder {
public:
    // Write OrderAddMessage to buffer. Returns bytes written.
    static std::size_t encode(const OrderAddMessage& msg, uint8_t* buffer, std::size_t max_len) noexcept {
        if (max_len < sizeof(OrderAddMessage)) return 0;
        std::memcpy(buffer, &msg, sizeof(OrderAddMessage));
        return sizeof(OrderAddMessage);
    }

    // Write OrderCancelMessage to buffer. Returns bytes written.
    static std::size_t encode(const OrderCancelMessage& msg, uint8_t* buffer, std::size_t max_len) noexcept {
        if (max_len < sizeof(OrderCancelMessage)) return 0;
        std::memcpy(buffer, &msg, sizeof(OrderCancelMessage));
        return sizeof(OrderCancelMessage);
    }

    // Write ExecutionReportMessage to buffer. Returns bytes written.
    static std::size_t encode(const ExecutionReportMessage& msg, uint8_t* buffer, std::size_t max_len) noexcept {
        if (max_len < sizeof(ExecutionReportMessage)) return 0;
        std::memcpy(buffer, &msg, sizeof(ExecutionReportMessage));
        return sizeof(ExecutionReportMessage);
    }
};

class MessageDecoder {
public:
    // Zero-copy decode: simply reinterpret_cast the buffer to the message struct.
    // The caller must ensure that the buffer is large enough for the corresponding message.
    
    static const OrderAddMessage* decodeOrderAdd(const uint8_t* buffer) noexcept {
        return reinterpret_cast<const OrderAddMessage*>(buffer);
    }

    static const OrderCancelMessage* decodeOrderCancel(const uint8_t* buffer) noexcept {
        return reinterpret_cast<const OrderCancelMessage*>(buffer);
    }

    static const ExecutionReportMessage* decodeExecutionReport(const uint8_t* buffer) noexcept {
        return reinterpret_cast<const ExecutionReportMessage*>(buffer);
    }
};

class FeedHandler {
public:
    explicit FeedHandler(Book& book) : book_(book) {}

    // Parse a raw byte stream and dispatch messages to the LOB.
    // Returns the number of bytes successfully processed.
    std::size_t processStream(const uint8_t* buffer, std::size_t length) {
        std::size_t offset = 0;
        
        while (offset < length) {
            // Need at least 1 byte for MessageType
            if (length - offset < sizeof(MessageType)) {
                break;
            }
            
            MessageType type = static_cast<MessageType>(buffer[offset]);
            
            switch (type) {
                case MessageType::OrderAdd: {
                    if (length - offset < sizeof(OrderAddMessage)) return offset;
                    const auto* msg = MessageDecoder::decodeOrderAdd(buffer + offset);
                    dispatchOrderAdd(*msg);
                    offset += sizeof(OrderAddMessage);
                    break;
                }
                case MessageType::OrderCancel: {
                    if (length - offset < sizeof(OrderCancelMessage)) return offset;
                    const auto* msg = MessageDecoder::decodeOrderCancel(buffer + offset);
                    dispatchOrderCancel(*msg);
                    offset += sizeof(OrderCancelMessage);
                    break;
                }
                case MessageType::Execution: {
                    if (length - offset < sizeof(ExecutionReportMessage)) return offset;
                    const auto* msg = MessageDecoder::decodeExecutionReport(buffer + offset);
                    dispatchExecutionReport(*msg);
                    offset += sizeof(ExecutionReportMessage);
                    break;
                }
                default:
                    // Unknown message type in stream: fatal parse error for this stream
                    return offset;
            }
        }
        return offset;
    }

private:
    void dispatchOrderAdd(const OrderAddMessage& msg) {
        bool is_buy = (msg.side == 'B');
        int price = static_cast<int>(msg.price / kPriceMultiplier);
        int quantity = static_cast<int>(msg.quantity);
        int order_id = static_cast<int>(msg.order_id);

        if (msg.order_type == 'L') {
            book_.addLimitOrder(order_id, is_buy, quantity, price);
        } else if (msg.order_type == 'M') {
            book_.marketOrder(order_id, is_buy, quantity);
        } else if (msg.order_type == 'S') {
            book_.addStopOrder(order_id, is_buy, quantity, price); // price as stopPrice
        }
    }

    void dispatchOrderCancel(const OrderCancelMessage& msg) {
        // Our simplified LOB mainly supports cancelLimitOrder natively right now,
        // although we can also search and cancel other types.
        book_.cancelLimitOrder(static_cast<int>(msg.order_id));
    }

    void dispatchExecutionReport(const ExecutionReportMessage& /*msg*/) {
        // In a feed handler, an execution report from an upstream venue 
        // might be used to track trades, but usually the LOB itself generates executions.
        // For mirroring a remote book, we'd process it here.
    }

    Book& book_;
};

} // namespace protocol

#endif // PROTOCOL_MESSAGE_ENCODER_HPP
