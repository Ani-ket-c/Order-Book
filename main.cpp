// ==========================================================================
//  Main Executable Entry Point
//  Now runs the Module 5 Realistic Market Simulator by default.
// ==========================================================================

#include "Simulation/MarketSimulator.hpp"
#include "Limit_Order_Book/Book.hpp"
#include "Analytics/MicrostructureEngine.hpp"
#include "Profiling/LatencyProfiler.hpp"

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>

namespace {

std::uint64_t coreSampleCount(const profiling::LatencyProfiler& profiler) {
    return profiler.count(profiling::LatencyKind::OrderAdd)
         + profiler.count(profiling::LatencyKind::OrderCancel)
         + profiler.count(profiling::LatencyKind::Match);
}

double weightedCoreLatencyNs(const profiling::LatencyProfiler& profiler) {
    const auto add_count = profiler.count(profiling::LatencyKind::OrderAdd);
    const auto cancel_count = profiler.count(profiling::LatencyKind::OrderCancel);
    const auto match_count = profiler.count(profiling::LatencyKind::Match);
    const auto total = add_count + cancel_count + match_count;

    if (total == 0) {
        return 0.0;
    }

    const double weighted_sum =
        profiler.mean(profiling::LatencyKind::OrderAdd) * static_cast<double>(add_count)
      + profiler.mean(profiling::LatencyKind::OrderCancel) * static_cast<double>(cancel_count)
      + profiler.mean(profiling::LatencyKind::Match) * static_cast<double>(match_count);

    return weighted_sum / static_cast<double>(total);
}

std::string fixedNumber(double value, int precision) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

void printPerformanceSummary(const profiling::LatencyProfiler& profiler) {
    const double optimized_latency_ns = weightedCoreLatencyNs(profiler);
    const double optimized_tps = optimized_latency_ns > 0.0
        ? 1'000'000'000.0 / optimized_latency_ns
        : 0.0;

    const std::string latency_text = "~" + fixedNumber(optimized_latency_ns, 0) + " ns";
    const std::string throughput_text = "~" + fixedNumber(optimized_tps / 1'000'000.0, 1) + "M TPS";

    std::cout << "\nPerformance Results\n";
    std::cout << std::left << std::setw(24) << "Metric"
              << "Current Run\n";

    std::cout << std::left << std::setw(24) << "Latency per order"
              << latency_text << "\n";

    std::cout << std::left << std::setw(24) << "Throughput"
              << throughput_text << "\n";

    std::cout << std::left << std::setw(24) << "Orders measured"
              << coreSampleCount(profiler) << "\n";

    std::cout << "Benchmarks were run with " << coreSampleCount(profiler)
              << " in-memory orders (no file I/O during measurement).\n";
}

} // namespace

int main(int argc, char** argv) {
    int steps = 10000; // default simulation steps (10 seconds)
    if (argc > 1) {
        steps = std::atoi(argv[1]);
    }

    std::cout << "Starting LOB Market Simulator for " << steps << " steps...\n";

    Book book;
    analytics::MicrostructureEngine analytics;
    book.setAnalyticsEngine(&analytics);
    
    profiling::LatencyProfiler profiler;

    simulation::MarketSimulator simulator(book, analytics, profiler);
    
    // Tweakable params for realistic market microstructure
    simulator.setOUParams(0.05, 300.0, 1.5);
    simulator.setArrivalRates(50.0, 5.0, 25.0); // Limit/Market/Cancel
    simulator.setInformedRatio(0.15); // 15% informed flow

    // Bootstrap liquidity to establish an initial spread
    for (int i = 0; i < 20; ++i) {
        book.addLimitOrder(1000000 + i, true, 100, 290 - i);
        book.addLimitOrder(2000000 + i, false, 100, 310 + i);
    }

    // Run lock-free gateway simulator
    simulator.run(steps);

    std::cout << "Simulation completed.\n";
    std::cout << "Exported: simulation_results.csv (Metrics)\n";
    std::cout << "Exported: latency_report.txt (HDR-Histogram)\n";
    
    // Output latency report directly to console
    std::cout << "\n" << profiler.report() << "\n";
    printPerformanceSummary(profiler);

    return 0;
}
