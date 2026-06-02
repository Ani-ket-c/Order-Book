// ==========================================================================
//  Module 5: Realistic Market Simulator
//  File: Simulation/MarketSimulator.cpp
// ==========================================================================
#include "MarketSimulator.hpp"
#include "Protocol/MessageEncoder.hpp"

#include <fstream>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <cstdio>

namespace simulation {

MarketSimulator::MarketSimulator(Book& book, analytics::MicrostructureEngine& analytics, profiling::LatencyProfiler& profiler)
    : book_(book), analytics_(analytics), profiler_(profiler), rng_(std::random_device{}()) {
    metrics_history_.reserve(1000000);
}

MarketSimulator::~MarketSimulator() {
    running_ = false;
}

void MarketSimulator::run(int steps) {
    running_ = true;
    
    // We will generate a predictable number of messages if we run the simulation process first
    // For realism, we run them in parallel. We'll tell the consumer how many messages to expect.
    // Wait, since we are doing Poisson arrivals, the exact number varies per step. 
    // Let's use a sentinel message or atomic flag to stop the consumer.
    
    std::thread consumer(&MarketSimulator::lobConsumerThreadFunc, this, steps);
    
    simulatorThreadFunc(steps);
    
    consumer.join();
    
    dumpCSV("simulation_results.csv");
    dumpLatencyReport("latency_report.txt");
}

void MarketSimulator::simulatorThreadFunc(int steps) {
    std::normal_distribution<double> normal_dist(0.0, 1.0);
    std::poisson_distribution<int> poisson_limit(lambda_limit_);
    std::poisson_distribution<int> poisson_market(lambda_market_);
    std::poisson_distribution<int> poisson_cancel(lambda_cancel_);
    
    // Log-normal size distribution
    std::lognormal_distribution<double> size_dist(4.0, 1.0); // mean roughly 55, skewed
    std::uniform_real_distribution<double> uniform_dist(0.0, 1.0);
    
    uint32_t next_order_id = 1;
    uint64_t simulated_time = 1000000000ULL; // Start at arbitrary timestamp

    for (int step = 0; step < steps; ++step) {
        simulated_time += 1000000; // 1 ms per step

        // 1. Update true price using Ornstein-Uhlenbeck process
        // dX_t = theta * (mu - X_t) dt + sigma * dW_t
        double dt = 0.01;
        double dW = normal_dist(rng_) * std::sqrt(dt);
        current_true_price_ += theta_ * (mu_ - current_true_price_) * dt + sigma_ * dW;
        
        // Ensure price stays positive
        if (current_true_price_ <= 0) current_true_price_ = 0.01;
        
        // Snapshot book state to make informed decisions
        analytics::MetricsSnapshot snap = analytics_.readSnapshot();
        double mid = (snap.mid_price > 0) ? snap.mid_price : current_true_price_;
        
        // 2. Generate Limit Orders
        int num_limit = poisson_limit(rng_);
        for (int i = 0; i < num_limit; ++i) {
            protocol::OrderAddMessage msg;
            msg.timestamp = simulated_time;
            msg.order_id = next_order_id++;
            msg.quantity = std::max<uint32_t>(1, static_cast<uint32_t>(size_dist(rng_)));
            msg.order_type = 'L';
            
            bool is_informed = uniform_dist(rng_) < informed_ratio_;
            bool is_buy = uniform_dist(rng_) < 0.5;
            
            if (is_informed) {
                // Informed traders trade in direction of the true price vs current mid
                is_buy = current_true_price_ > mid;
            }
            
            msg.side = is_buy ? 'B' : 'S';
            
            // Limit price around mid price or true price
            double base_price = is_informed ? current_true_price_ : mid;
            double tick_offset = normal_dist(rng_) * 2.0; // spread out by a couple ticks
            double order_px = base_price + (is_buy ? -std::abs(tick_offset) : std::abs(tick_offset));
            if (order_px <= 0) order_px = 1.0;
            
            msg.price = static_cast<uint64_t>(order_px * protocol::kPriceMultiplier);
            
            while (!gateway_.enqueue_order(SimMessage(msg))) {
                std::this_thread::yield();
            }
            active_orders_.push_back(msg.order_id);
        }
        
        // 3. Generate Market Orders
        int num_market = poisson_market(rng_);
        for (int i = 0; i < num_market; ++i) {
            protocol::OrderAddMessage msg;
            msg.timestamp = simulated_time;
            msg.order_id = next_order_id++;
            msg.quantity = std::max<uint32_t>(1, static_cast<uint32_t>(size_dist(rng_)));
            msg.order_type = 'M';
            
            bool is_informed = uniform_dist(rng_) < informed_ratio_;
            if (is_informed) {
                msg.side = (current_true_price_ > mid) ? 'B' : 'S';
            } else {
                msg.side = (uniform_dist(rng_) < 0.5) ? 'B' : 'S';
            }
            
            msg.price = 0; // Market order
            
            while (!gateway_.enqueue_order(SimMessage(msg))) {
                std::this_thread::yield();
            }
        }
        
        // 4. Generate Cancels
        int num_cancel = poisson_cancel(rng_);
        for (int i = 0; i < num_cancel && !active_orders_.empty(); ++i) {
            std::uniform_int_distribution<size_t> idx_dist(0, active_orders_.size() - 1);
            size_t idx = idx_dist(rng_);
            
            protocol::OrderCancelMessage msg;
            msg.timestamp = simulated_time;
            msg.order_id = active_orders_[idx];
            
            // Remove from active tracking
            std::swap(active_orders_[idx], active_orders_.back());
            active_orders_.pop_back();
            
            while (!gateway_.enqueue_order(SimMessage(msg))) {
                std::this_thread::yield();
            }
        }
        
        // Periodically record metrics
        if (step % 100 == 0) {
            recordMetrics(simulated_time);
        }
        
        // Benchmark mode: do not sleep between simulated market steps.
    }
    
    running_ = false; // signal consumer to stop once queue is empty
}

void MarketSimulator::lobConsumerThreadFunc(int /*steps*/) {
    SimMessage msg;
    
    while (running_ || !gateway_.inbound_depth() == 0) {
        if (gateway_.dequeue_order(msg)) {
            if (msg.type == protocol::MessageType::OrderAdd) {
                bool is_buy = (msg.add.side == 'B');
                int price = static_cast<int>(msg.add.price / protocol::kPriceMultiplier);
                int qty = static_cast<int>(msg.add.quantity);
                int oid = static_cast<int>(msg.add.order_id);
                
                if (msg.add.order_type == 'L') {
                    profiling::ScopedTimer timer(profiler_, profiling::LatencyKind::OrderAdd);
                    book_.addLimitOrder(oid, is_buy, qty, price);
                } else if (msg.add.order_type == 'M') {
                    profiling::ScopedTimer timer(profiler_, profiling::LatencyKind::Match);
                    book_.marketOrder(oid, is_buy, qty);
                }
            } else if (msg.type == protocol::MessageType::OrderCancel) {
                int oid = static_cast<int>(msg.cancel.order_id);
                profiling::ScopedTimer timer(profiler_, profiling::LatencyKind::OrderCancel);
                book_.cancelLimitOrder(oid);
            }
            
            // E2E latency from generation to processing completion
            double e2e_latency = gateway_.inbound_latency_tracker().last_latency_ns();
            profiler_.record(profiling::LatencyKind::EndToEnd, static_cast<uint64_t>(e2e_latency));
        }
    }
}

void MarketSimulator::recordMetrics(uint64_t timestamp) {
    analytics::MetricsSnapshot snap = analytics_.readSnapshot();
    metrics_history_.push_back({
        timestamp,
        snap.mid_price,
        snap.spread_ticks,
        snap.ofi,
        snap.vwap,
        snap.kyle_lambda
    });
    
}

void MarketSimulator::dumpCSV(const std::string& filename) const {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to open " << filename << " for writing.\n";
        return;
    }
    
    out << "timestamp,mid_price,spread,ofi,vwap,kyle_lambda\n";
    for (const auto& rec : metrics_history_) {
        out << rec.timestamp << ","
            << rec.mid_price << ","
            << rec.spread_ticks << ","
            << rec.ofi << ","
            << rec.vwap << ","
            << rec.kyle_lambda << "\n";
    }
    std::cout << "Successfully exported simulation metrics to " << filename << "\n";
}

void MarketSimulator::dumpLatencyReport(const std::string& filename) const {
    std::ofstream out(filename);
    if (!out) {
        std::cerr << "Failed to open " << filename << " for writing.\n";
        return;
    }
    
    out << profiler_.report();
    std::cout << "Successfully exported latency report to " << filename << "\n";
}

} // namespace simulation
