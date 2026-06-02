// ==========================================================================
//  Module 5: lob_sim Executable Entry Point
// ==========================================================================

#include "Simulation/MarketSimulator.hpp"
#include "Limit_Order_Book/Book.hpp"
#include "Analytics/MicrostructureEngine.hpp"
#include "Profiling/LatencyProfiler.hpp"

#include <iostream>

int main(int argc, char** argv) {
    int steps = 10000; // default steps
    if (argc > 1) {
        steps = std::atoi(argv[1]);
    }

    std::cout << "Starting LOB Simulation for " << steps << " steps...\n";

    Book book;
    analytics::MicrostructureEngine analytics;
    book.setAnalyticsEngine(&analytics);
    
    profiling::LatencyProfiler profiler;

    simulation::MarketSimulator simulator(book, analytics, profiler);
    
    // Tweakable params
    simulator.setOUParams(0.05, 300.0, 1.5);
    simulator.setArrivalRates(50.0, 5.0, 25.0); // Higher limit/cancel relative to market
    simulator.setInformedRatio(0.15);

    // Warm up the book slightly with some liquidity so spreads aren't huge initially
    // We can inject direct orders or just let the simulation naturally build the book.
    // Let's inject a few manual orders right around 300 to bootstrap
    for (int i = 0; i < 20; ++i) {
        book.addLimitOrder(1000000 + i, true, 100, 290 - i);
        book.addLimitOrder(2000000 + i, false, 100, 310 + i);
    }

    simulator.run(steps);

    std::cout << "Simulation completed. Check simulation_results.csv and latency_report.txt\n";
    
    // Print the latency report to stdout as well
    std::cout << "\n" << profiler.report() << "\n";

    return 0;
}
