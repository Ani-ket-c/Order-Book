// ==========================================================================
//  Module 4: Smoke test for Binary Wire Protocol
//  Verifies encoding, zero-copy decoding, and feed handler dispatch.
// ==========================================================================

#include "Protocol/MessageEncoder.hpp"
#include "Limit_Order_Book/Book.hpp"

#include <cassert>
#include <cstdio>
#include <vector>

void test_encode_decode() {
    protocol::OrderAddMessage msg_in;
    msg_in.timestamp = 123456789;
    msg_in.order_id = 42;
    msg_in.side = 'B';
    msg_in.price = 100 * protocol::kPriceMultiplier; // $100
    msg_in.quantity = 500;
    msg_in.order_type = 'L';

    uint8_t buffer[256];
    std::size_t encoded_bytes = protocol::MessageEncoder::encode(msg_in, buffer, sizeof(buffer));
    assert(encoded_bytes == sizeof(protocol::OrderAddMessage));
    (void)encoded_bytes;

    const auto* msg_out = protocol::MessageDecoder::decodeOrderAdd(buffer);
    assert(msg_out->type == protocol::MessageType::OrderAdd);
    assert(msg_out->timestamp == 123456789);
    assert(msg_out->order_id == 42);
    assert(msg_out->side == 'B');
    assert(msg_out->price == 1000000);
    assert(msg_out->quantity == 500);
    assert(msg_out->order_type == 'L');
    (void)msg_out;

    std::printf("[PASS] test_encode_decode\n");
}

void test_feed_handler() {
    Book book;
    protocol::FeedHandler handler(book);

    std::vector<uint8_t> stream;
    
    // Add a Buy Limit Order
    protocol::OrderAddMessage add_msg;
    add_msg.timestamp = 1000;
    add_msg.order_id = 1;
    add_msg.side = 'B';
    add_msg.price = 150 * protocol::kPriceMultiplier;
    add_msg.quantity = 100;
    add_msg.order_type = 'L';
    
    uint8_t buf_add[sizeof(add_msg)];
    protocol::MessageEncoder::encode(add_msg, buf_add, sizeof(buf_add));
    stream.insert(stream.end(), buf_add, buf_add + sizeof(buf_add));

    // Add a Cancel Order
    protocol::OrderCancelMessage cancel_msg;
    cancel_msg.timestamp = 1001;
    cancel_msg.order_id = 1;
    
    uint8_t buf_cancel[sizeof(cancel_msg)];
    protocol::MessageEncoder::encode(cancel_msg, buf_cancel, sizeof(buf_cancel));
    stream.insert(stream.end(), buf_cancel, buf_cancel + sizeof(buf_cancel));

    // Process the stream
    std::size_t processed = handler.processStream(stream.data(), stream.size());
    assert(processed == stream.size());
    (void)processed;

    std::printf("[PASS] test_feed_handler\n");
}

int main() {
    std::printf("=== Module 4: Protocol Test Suite ===\n\n");

    test_encode_decode();
    test_feed_handler();

    std::printf("\n=== All tests passed! ===\n");
    return 0;
}
