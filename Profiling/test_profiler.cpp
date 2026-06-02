// ==========================================================================
//  Module 3: Smoke test for LatencyProfiler
//  Verifies histogram bucketing, percentiles, and report generation.
//
//  Build: cmake --build build --target lob_profiler_test
//  Run:   build/Release/lob_profiler_test.exe
// ==========================================================================

#ifndef LATENCY_PROFILING
    #define LATENCY_PROFILING  // Ensure profiling is on for this test
#endif

#include "Profiling/LatencyProfiler.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>

static void test_histogram_basic() {
    profiling::HdrHistogram h;

    // Record a few known values
    h.record(100);
    h.record(200);
    h.record(300);
    h.record(400);
    h.record(500);

    assert(h.count() == 5);
    assert(h.min() <= 100.0);
    assert(h.max() >= 500.0);

    // Mean should be ~300
    double m = h.mean();
    assert(m >= 250.0 && m <= 350.0);
    (void)m;

    // p50 should be near 300
    double med = h.p50();
    assert(med >= 200.0 && med <= 400.0);
    (void)med;

    // p99 should be near 500
    double p99 = h.p99();
    assert(p99 >= 400.0 && p99 <= 600.0);
    (void)p99;

    std::printf("[PASS] test_histogram_basic\n");
}

static void test_histogram_linear_region() {
    profiling::HdrHistogram h;

    // Fill linear region: 0..1023 ns at 1 ns resolution
    for (std::uint64_t i = 0; i < 1000; ++i) {
        h.record(i);
    }

    assert(h.count() == 1000);
    assert(h.min() <= 1.0);
    assert(h.max() >= 999.0);

    // p50 should be near 500
    double med = h.p50();
    assert(med >= 450.0 && med <= 550.0);
    (void)med;

    std::printf("[PASS] test_histogram_linear_region\n");
}

static void test_histogram_exponential_region() {
    profiling::HdrHistogram h;

    // Record values in the microsecond range
    for (int i = 0; i < 10000; ++i) {
        h.record(5000 + static_cast<std::uint64_t>(i)); // 5us to 15us
    }

    assert(h.count() == 10000);
    double med = h.p50();
    assert(med >= 4000.0 && med <= 11000.0);
    (void)med;

    std::printf("[PASS] test_histogram_exponential_region\n");
}

static void test_profiler_per_kind() {
    profiling::LatencyProfiler profiler;
    using Kind = profiling::LatencyKind;

    // Record different latency distributions per kind
    for (int i = 0; i < 1000; ++i) {
        profiler.record(Kind::OrderAdd, 100 + static_cast<std::uint64_t>(i));
        profiler.record(Kind::OrderCancel, 50 + static_cast<std::uint64_t>(i));
        profiler.record(Kind::Match, 30 + static_cast<std::uint64_t>(i));
        profiler.record(Kind::EndToEnd, 200 + static_cast<std::uint64_t>(i));
    }

    // Each kind should have 1000 samples
    assert(profiler.count(Kind::OrderAdd) == 1000);
    assert(profiler.count(Kind::OrderCancel) == 1000);
    assert(profiler.count(Kind::Match) == 1000);
    assert(profiler.count(Kind::EndToEnd) == 1000);

    // OrderAdd p50 should be higher than Match p50
    assert(profiler.p50(Kind::OrderAdd) > profiler.p50(Kind::Match));

    // EndToEnd should have the highest latencies
    assert(profiler.p50(Kind::EndToEnd) > profiler.p50(Kind::OrderAdd));

    std::printf("[PASS] test_profiler_per_kind\n");
}

static void test_scoped_timer() {
    profiling::LatencyProfiler profiler;
    using Kind = profiling::LatencyKind;

    // ScopedTimer should record automatically on scope exit
    for (int i = 0; i < 100; ++i) {
        profiling::ScopedTimer timer(profiler, Kind::Match);
        // Simulate ~10ns of work
        volatile int dummy = 0;
        for (int j = 0; j < 10; ++j) ++dummy;
    }

    assert(profiler.count(Kind::Match) == 100);
    assert(profiler.p50(Kind::Match) > 0.0);

    std::printf("[PASS] test_scoped_timer\n");
}

static void test_report_output() {
    profiling::LatencyProfiler profiler;
    using Kind = profiling::LatencyKind;

    for (int i = 0; i < 5000; ++i) {
        profiler.record(Kind::OrderAdd, 120 + static_cast<std::uint64_t>(i % 200));
        profiler.record(Kind::OrderCancel, 80 + static_cast<std::uint64_t>(i % 150));
        profiler.record(Kind::Match, 40 + static_cast<std::uint64_t>(i % 100));
        profiler.record(Kind::EndToEnd, 300 + static_cast<std::uint64_t>(i % 500));
    }

    std::string report = profiler.report();

    // Should contain all kind names
    assert(report.find("Add") != std::string::npos);
    assert(report.find("Cancel") != std::string::npos);
    assert(report.find("Match") != std::string::npos);
    assert(report.find("EndToEnd") != std::string::npos);

    // Should contain header columns
    assert(report.find("p50") != std::string::npos);
    assert(report.find("p99.99") != std::string::npos);

    std::printf("[PASS] test_report_output\n");
    std::printf("\n--- Sample Report ---\n%s\n", report.c_str());
}

static void test_reset() {
    profiling::LatencyProfiler profiler;
    using Kind = profiling::LatencyKind;

    profiler.record(Kind::Match, 100);
    assert(profiler.count(Kind::Match) == 1);

    profiler.reset();
    assert(profiler.count(Kind::Match) == 0);

    std::printf("[PASS] test_reset\n");
}

int main() {
    std::printf("=== Module 3: LatencyProfiler Test Suite ===\n\n");

    test_histogram_basic();
    test_histogram_linear_region();
    test_histogram_exponential_region();
    test_profiler_per_kind();
    test_scoped_timer();
    test_report_output();
    test_reset();

    std::printf("\n=== All tests passed! ===\n");
    return 0;
}
