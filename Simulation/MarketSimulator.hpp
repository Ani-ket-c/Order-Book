// ==========================================================================
//  Module 5: Realistic Market Simulator
//  File: Simulation/MarketSimulator.hpp
// ==========================================================================
#ifndef SIMULATION_MARKET_SIMULATOR_HPP
#define SIMULATION_MARKET_SIMULATOR_HPP

#include "Limit_Order_Book/Book.hpp"
#include "Analytics/MicrostructureEngine.hpp"
#include "Profiling/LatencyProfiler.hpp"
#include "Gateway/SPSCQueue.hpp"
#include "Protocol/OrderMessage.hpp"

#include <random>
#include <thread>
#include <atomic>
#include <vector>
#include <string>

namespace simulation {

// A composite message type for the Gateway to handle both Add and Cancel
struct SimMessage {
    protocol::MessageType type;
    union {
        protocol::OrderAddMessage add;
        protocol::OrderCancelMessage cancel;
    };
    
    SimMessage() : type(protocol::MessageType::OrderAdd), add{} {}
    SimMessage(const protocol::OrderAddMessage& a) : type(protocol::MessageType::OrderAdd), add(a) {}
    SimMessage(const protocol::OrderCancelMessage& c) : type(protocol::MessageType::OrderCancel), cancel(c) {}
};

class MarketSimulator {
public:
    MarketSimulator(Book& book, analytics::MicrostructureEngine& analytics, profiling::LatencyProfiler& profiler);
    ~MarketSimulator();

    // Run the simulation for the given number of steps
    void run(int steps);

    // Configurable parameters
    void setOUParams(double theta, double mu, double sigma) {
        theta_ = theta; mu_ = mu; sigma_ = sigma;
    }
    void setArrivalRates(double limit, double market, double cancel) {
        lambda_limit_ = limit; lambda_market_ = market; lambda_cancel_ = cancel;
    }
    void setInformedRatio(double ratio) { informed_ratio_ = ratio; }

private:
    void simulatorThreadFunc(int steps);
    void lobConsumerThreadFunc(int total_expected_messages);
    
    void recordMetrics(uint64_t timestamp);
    void dumpCSV(const std::string& filename) const;
    void dumpLatencyReport(const std::string& filename) const;

    Book& book_;
    analytics::MicrostructureEngine& analytics_;
    profiling::LatencyProfiler& profiler_;

    // High-capacity SPSC Gateway
    gateway::OrderGateway<SimMessage, protocol::ExecutionReportMessage, 1048576, 1048576> gateway_;

    // Ornstein-Uhlenbeck parameters
    double theta_{0.05};
    double mu_{300.0};
    double sigma_{1.5};
    double current_true_price_{300.0};

    // Poisson arrival rates (lambda)
    double lambda_limit_{20.0};
    double lambda_market_{5.0};
    double lambda_cancel_{10.0};
    
    double informed_ratio_{0.15};

    std::mt19937 rng_;
    
    struct MetricRecord {
        uint64_t timestamp;
        double mid_price;
        double spread_ticks;
        double ofi;
        double vwap;
        double kyle_lambda;
    };
    std::vector<MetricRecord> metrics_history_;
    
    std::atomic<bool> running_{false};
    std::vector<uint32_t> active_orders_;
};

} // namespace simulation

#endif // SIMULATION_MARKET_SIMULATOR_HPP
