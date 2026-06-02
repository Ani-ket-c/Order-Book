#include "SPSCQueue.hpp"

#include <cassert>
#include <iostream>

int main() {
    gateway::LatencyTracker::calibrate();
    assert(gateway::LatencyTracker::is_calibrated());

    gateway::OrderGateway<gateway::GatewayOrder, gateway::GatewayExecutionReport> gateway;

    gateway::GatewayOrder order{42, true, 100, 9900};
    assert(gateway.enqueue_order(order));
    assert(gateway.inbound_depth() == 1);

    gateway::GatewayOrder out{};
    assert(gateway.dequeue_order(out));
    assert(out.idNumber == 42);
    assert(out.shares == 100);
    assert(gateway.inbound_depth() == 0);
    assert(gateway.inbound_latency_tracker().sample_count() == 1);

    gateway::GatewayExecutionReport exec{1, 2, 9900, 50};
    assert(gateway.publish_execution(exec));
    gateway::GatewayExecutionReport exec_out{};
    assert(gateway.dequeue_execution(exec_out));
    assert(exec_out.execQty == 50);

    gateway::SPSCQueue<int, 4> q;
    for (int i = 0; i < 4; ++i) {
        assert(q.try_push(i));
    }
    assert(q.full());
    assert(!q.try_push(99));

    int v = -1;
    for (int i = 0; i < 4; ++i) {
        assert(q.try_pop(v));
        assert(v == i);
    }
    assert(q.empty());

    std::cout << "gateway tests passed (mean inbound latency ns: "
              << gateway.inbound_latency_tracker().mean_latency_ns() << ")\n";
    return 0;
}
