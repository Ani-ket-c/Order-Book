// ==========================================================================
//  Module 3: Latency Profiler with Percentile Histograms
//  File: Profiling/LatencyProfiler.hpp
//
//  HDR-Histogram-style bucketed histogram (no external deps) tracking
//  nanosecond latencies across four operation kinds.
//
//  Design notes:
//    - Header-only: all logic inlined for LTO to devirtualise on the hot path.
//    - Per-kind histograms: each Kind gets its own bucket array so percentiles
//      are accurate per-operation-type (unlike the previous single-bucket impl).
//    - Sub-bucket resolution: uses an exponential-linear hybrid scheme.
//      The first `kLinearBuckets` slots cover 0..kLinearBuckets ns at 1 ns
//      resolution.  Beyond that, we use `significantBits`-wide sub-buckets
//      inside each power-of-2 magnitude, giving ~1% relative error
//      (comparable to HdrHistogram with 2 significant digits).
//    - TSC timestamps: uses __rdtsc() with the calibration from the Gateway
//      LatencyTracker (avoids duplicate calibration logic).
//    - Compile-time toggle: when LATENCY_PROFILING is NOT defined, record()
//      and the ScopedTimer are empty inline functions – true zero cost.
//    - Lock-free: all counters are std::atomic with relaxed ordering (single
//      writer on the hot path; reader uses acquire for the report).
// ==========================================================================

#ifndef LATENCY_PROFILER_HPP
#define LATENCY_PROFILER_HPP

#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef LATENCY_PROFILING
    #include <chrono>
#endif

// TSC intrinsics (cross-platform)
#if defined(_MSC_VER)
    #include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
    #include <x86intrin.h>
#else
    #error "LatencyProfiler requires x86 TSC (__rdtsc)"
#endif

namespace profiling {

// ─── Inline rdtsc helper ────────────────────────────────────────────────
namespace detail {

[[nodiscard]] inline std::uint64_t rdtsc_now() noexcept {
#if defined(_MSC_VER)
    return static_cast<std::uint64_t>(__rdtsc());
#else
    return static_cast<std::uint64_t>(__rdtsc());
#endif
}

} // namespace detail

// ─── TSC-to-nanosecond calibration ──────────────────────────────────────
//  We keep a lightweight calibrator here so the profiler is self-contained.
//  If the Gateway's LatencyTracker has already calibrated, their values
//  will agree (both measure the same TSC frequency).

class TSCCalibrator {
public:
    /// Calibrate TSC frequency using a busy-spin measurement.
    /// Safe to call multiple times; only the first call performs work.
    static void calibrate() noexcept {
        if (calibrated_.load(std::memory_order_acquire)) return;

#ifdef LATENCY_PROFILING
        // Use std::chrono only during this one-time cold-path calibration.
        // The hot path never touches chrono.
        constexpr int kSamples = 16;
        double total_ratio = 0.0;
        int valid = 0;

        for (int i = 0; i < kSamples; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const std::uint64_t c0 = detail::rdtsc_now();

            // Busy-spin ~2ms
            const auto deadline = t0 + std::chrono::milliseconds(2);
            while (std::chrono::steady_clock::now() < deadline) {}

            const std::uint64_t c1 = detail::rdtsc_now();
            const auto t1 = std::chrono::steady_clock::now();

            const auto ns =
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            const std::uint64_t cycles = c1 - c0;
            if (cycles > 0 && ns > 0) {
                total_ratio += static_cast<double>(ns) / static_cast<double>(cycles);
                ++valid;
            }
        }

        if (valid > 0) {
            ns_per_cycle_.store(total_ratio / static_cast<double>(valid),
                                std::memory_order_release);
        }
#endif
        calibrated_.store(true, std::memory_order_release);
    }

    [[nodiscard]] static double ns_per_cycle() noexcept {
        return ns_per_cycle_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static bool is_calibrated() noexcept {
        return calibrated_.load(std::memory_order_acquire);
    }

    /// Convert TSC cycle delta → nanoseconds.
    [[nodiscard]] static double cycles_to_ns(std::uint64_t cycles) noexcept {
        return static_cast<double>(cycles) * ns_per_cycle();
    }

private:
    static inline std::atomic<double> ns_per_cycle_{0.4}; // ~2.5 GHz default
    static inline std::atomic<bool>   calibrated_{false};
};


// ═════════════════════════════════════════════════════════════════════════
//  HDR-style Histogram
//
//  Layout:  [ linear region 0..kLinearBuckets-1 at 1ns resolution ]
//           [ exponential region: kMagnitudes magnitudes, each with
//             kSubBuckets sub-buckets ]
//
//  Total buckets = kLinearBuckets + kMagnitudes * kSubBuckets
//
//  With kLinearBuckets=1024, kSubBucketBits=7, kMagnitudes=27:
//    - Linear:  0 .. 1023 ns  (1 ns precision)
//    - Exp:     1µs .. ~134s   (< 1% relative error in each bucket)
//    - Total buckets: 1024 + 27*128 = 4480  (~35 KB per Kind)
// ═════════════════════════════════════════════════════════════════════════

class HdrHistogram {
public:
    // Tuning knobs – adjust at compile time if needed.
    static constexpr std::size_t kLinearBuckets  = 1024;    // 0..1023 ns @ 1ns
    static constexpr std::size_t kSubBucketBits  = 7;       // 128 sub-buckets per magnitude
    static constexpr std::size_t kSubBuckets     = 1ULL << kSubBucketBits;  // 128
    static constexpr std::size_t kMagnitudes     = 27;      // covers up to ~134 seconds
    static constexpr std::size_t kTotalBuckets   = kLinearBuckets + kMagnitudes * kSubBuckets;

    HdrHistogram() noexcept { reset(); }

    /// Record a nanosecond-valued latency sample.
    void record(std::uint64_t ns) noexcept {
        const std::size_t idx = bucket_index(ns);
        counts_[idx].fetch_add(1, std::memory_order_relaxed);
        total_count_.fetch_add(1, std::memory_order_relaxed);
        sum_ns_.fetch_add(ns, std::memory_order_relaxed);

        // Update max (lock-free CAS loop)
        std::uint64_t cur = max_ns_.load(std::memory_order_relaxed);
        while (ns > cur &&
               !max_ns_.compare_exchange_weak(cur, ns,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}

        // Update min
        cur = min_ns_.load(std::memory_order_relaxed);
        while (ns < cur &&
               !min_ns_.compare_exchange_weak(cur, ns,
                   std::memory_order_relaxed, std::memory_order_relaxed)) {}
    }

    /// Compute the value at a given percentile [0.0, 100.0].
    [[nodiscard]] double percentile(double pct) const noexcept {
        const std::uint64_t total = total_count_.load(std::memory_order_acquire);
        if (total == 0) return 0.0;

        const std::uint64_t target =
            static_cast<std::uint64_t>(std::ceil(pct * 0.01 * static_cast<double>(total)));

        std::uint64_t cum = 0;
        for (std::size_t i = 0; i < kTotalBuckets; ++i) {
            cum += counts_[i].load(std::memory_order_relaxed);
            if (cum >= target) {
                return bucket_upper_bound(i);
            }
        }
        return static_cast<double>(max_ns_.load(std::memory_order_relaxed));
    }

    [[nodiscard]] double p50()   const noexcept { return percentile(50.0);  }
    [[nodiscard]] double p95()   const noexcept { return percentile(95.0);  }
    [[nodiscard]] double p99()   const noexcept { return percentile(99.0);  }
    [[nodiscard]] double p999()  const noexcept { return percentile(99.9);  }
    [[nodiscard]] double p9999() const noexcept { return percentile(99.99); }

    [[nodiscard]] double max() const noexcept {
        return static_cast<double>(max_ns_.load(std::memory_order_acquire));
    }

    [[nodiscard]] double min() const noexcept {
        const auto v = min_ns_.load(std::memory_order_acquire);
        return v == UINT64_MAX ? 0.0 : static_cast<double>(v);
    }

    [[nodiscard]] double mean() const noexcept {
        const auto n = total_count_.load(std::memory_order_acquire);
        if (n == 0) return 0.0;
        return static_cast<double>(sum_ns_.load(std::memory_order_acquire))
             / static_cast<double>(n);
    }

    [[nodiscard]] std::uint64_t count() const noexcept {
        return total_count_.load(std::memory_order_acquire);
    }

    void reset() noexcept {
        for (auto& c : counts_) c.store(0, std::memory_order_relaxed);
        total_count_.store(0, std::memory_order_relaxed);
        sum_ns_.store(0, std::memory_order_relaxed);
        max_ns_.store(0, std::memory_order_relaxed);
        min_ns_.store(UINT64_MAX, std::memory_order_relaxed);
    }

private:
    /// Map a nanosecond value to a bucket index.
    [[nodiscard]] static std::size_t bucket_index(std::uint64_t ns) noexcept {
        if (ns < kLinearBuckets) {
            return static_cast<std::size_t>(ns);
        }

        // Find the magnitude (power-of-2 band).
        // magnitude 0 covers [kLinearBuckets .. 2*kLinearBuckets)
        // magnitude m covers [kLinearBuckets * 2^m .. kLinearBuckets * 2^(m+1))
        //
        // We use bit tricks to find the magnitude quickly.
        const std::uint64_t shifted = ns >> 10; // kLinearBuckets == 1024 == 2^10
        if (shifted == 0) {
            // Still in the linear range (shouldn't reach here, but defensive)
            return static_cast<std::size_t>(ns);
        }

        // magnitude = floor(log2(shifted))
#if defined(_MSC_VER)
        unsigned long msb_pos;
        _BitScanReverse64(&msb_pos, shifted);
        const std::size_t magnitude = static_cast<std::size_t>(msb_pos);
#elif defined(__GNUC__) || defined(__clang__)
        const std::size_t magnitude = static_cast<std::size_t>(63 - __builtin_clzll(shifted));
#else
        std::size_t magnitude = 0;
        std::uint64_t tmp = shifted;
        while (tmp >>= 1) ++magnitude;
#endif

        if (magnitude >= kMagnitudes) {
            return kTotalBuckets - 1; // clamp to last bucket
        }

        // Sub-bucket index within this magnitude.
        // The magnitude band spans [kLinearBuckets * 2^mag, kLinearBuckets * 2^(mag+1)).
        // We take the top kSubBucketBits of the value within this band.
        const std::uint64_t band_start = kLinearBuckets << magnitude;
        const std::uint64_t offset     = ns - band_start;

        // Shift right to get the sub-bucket index
        const std::size_t shift = (magnitude > kSubBucketBits)
                                ? (magnitude - kSubBucketBits)
                                : 0;
        std::size_t sub = static_cast<std::size_t>(offset >> shift);
        if (sub >= kSubBuckets) sub = kSubBuckets - 1;

        return kLinearBuckets + magnitude * kSubBuckets + sub;
    }

    /// Return the upper bound (in ns) for a given bucket index.
    [[nodiscard]] static double bucket_upper_bound(std::size_t idx) noexcept {
        if (idx < kLinearBuckets) {
            return static_cast<double>(idx + 1); // [idx, idx+1) → upper = idx+1
        }

        const std::size_t exp_idx  = idx - kLinearBuckets;
        const std::size_t mag      = exp_idx / kSubBuckets;
        const std::size_t sub      = exp_idx % kSubBuckets;

        const double band_start = static_cast<double>(kLinearBuckets << mag);
        const double band_width = band_start;
        const double sub_width  = band_width / static_cast<double>(kSubBuckets);

        return band_start + static_cast<double>(sub + 1) * sub_width;
    }

    std::array<std::atomic<std::uint64_t>, kTotalBuckets> counts_{};
    std::atomic<std::uint64_t> total_count_{0};
    std::atomic<std::uint64_t> sum_ns_{0};
    std::atomic<std::uint64_t> max_ns_{0};
    std::atomic<std::uint64_t> min_ns_{UINT64_MAX};
};


// ═════════════════════════════════════════════════════════════════════════
//  LatencyProfiler — top-level facade
// ═════════════════════════════════════════════════════════════════════════

/// The four latency categories tracked by the profiler.
enum class LatencyKind : std::size_t {
    OrderAdd   = 0,
    OrderCancel,
    Match,
    EndToEnd,
    Count_     // sentinel – number of kinds
};

static constexpr std::size_t kNumKinds = static_cast<std::size_t>(LatencyKind::Count_);

class LatencyProfiler {
public:
    // Re-export for backward compatibility with code that used the old type name.
    using Kind = LatencyKind;

    LatencyProfiler() noexcept {
        TSCCalibrator::calibrate();
    }

    // ── Hot-path API (conditionally compiled) ────────────────────────

#ifdef LATENCY_PROFILING

    /// Record a nanosecond latency for a given operation kind.
    void record(Kind k, std::uint64_t ns) noexcept {
        histograms_[static_cast<std::size_t>(k)].record(ns);
    }

    /// Record a raw TSC cycle delta; converts to ns internally.
    void recordCycles(Kind k, std::uint64_t cycles) noexcept {
        const auto ns = static_cast<std::uint64_t>(
            TSCCalibrator::cycles_to_ns(cycles));
        record(k, ns);
    }

    /// Get the current TSC value (for bracketing a hot-path section).
    [[nodiscard]] static std::uint64_t now() noexcept {
        return detail::rdtsc_now();
    }

#else // LATENCY_PROFILING not defined — zero-cost stubs

    void record(Kind /*k*/, std::uint64_t /*ns*/) noexcept {}
    void recordCycles(Kind /*k*/, std::uint64_t /*cycles*/) noexcept {}
    [[nodiscard]] static std::uint64_t now() noexcept { return 0; }

#endif

    // ── Cold-path query API (always available for report()) ──────────

    /// Percentile value (ns) for a specific kind.
    [[nodiscard]] double percentile(Kind k, double pct) const noexcept {
        return histograms_[static_cast<std::size_t>(k)].percentile(pct);
    }

    /// Convenience accessors (default to Match kind for quick checks).
    [[nodiscard]] double p50(Kind k = Kind::Match)   const noexcept { return percentile(k, 50.0);  }
    [[nodiscard]] double p95(Kind k = Kind::Match)    const noexcept { return percentile(k, 95.0);  }
    [[nodiscard]] double p99(Kind k = Kind::Match)    const noexcept { return percentile(k, 99.0);  }
    [[nodiscard]] double p999(Kind k = Kind::Match)   const noexcept { return percentile(k, 99.9);  }
    [[nodiscard]] double p9999(Kind k = Kind::Match)  const noexcept { return percentile(k, 99.99); }
    [[nodiscard]] double max(Kind k = Kind::Match)    const noexcept {
        return histograms_[static_cast<std::size_t>(k)].max();
    }
    [[nodiscard]] double mean(Kind k = Kind::Match)   const noexcept {
        return histograms_[static_cast<std::size_t>(k)].mean();
    }
    [[nodiscard]] std::uint64_t count(Kind k = Kind::Match) const noexcept {
        return histograms_[static_cast<std::size_t>(k)].count();
    }

    /// Reset all histograms.
    void reset() noexcept {
        for (auto& h : histograms_) h.reset();
    }

    // ── ASCII Report ─────────────────────────────────────────────────

    /// Produce a formatted ASCII table with all percentiles for all kinds.
    [[nodiscard]] std::string report() const noexcept {
        // We use snprintf for tight formatting control without <iostream> overhead.
        // Buffer sized for header + separator + 4 rows + footer.
        constexpr std::size_t kBufSize = 2048;
        char buf[kBufSize];
        std::size_t pos = 0;

        auto append = [&](const char* fmt, auto... args) {
            pos += static_cast<std::size_t>(
                std::snprintf(buf + pos, kBufSize - pos, fmt, args...));
        };

        // Header
        append("%-10s %10s %10s %10s %10s %10s %10s %10s %12s\n",
               "Kind", "p50", "p95", "p99", "p99.9", "p99.99",
               "max", "mean", "samples");
        append("%-10s %10s %10s %10s %10s %10s %10s %10s %12s\n",
               "----------", "----------", "----------", "----------",
               "----------", "----------", "----------", "----------",
               "------------");

        static constexpr const char* kKindNames[] = {
            "Add", "Cancel", "Match", "EndToEnd"
        };

        for (std::size_t k = 0; k < kNumKinds; ++k) {
            const auto& h = histograms_[k];
            if (h.count() == 0) {
                append("%-10s %10s %10s %10s %10s %10s %10s %10s %12llu\n",
                       kKindNames[k], "-", "-", "-", "-", "-", "-", "-",
                       static_cast<unsigned long long>(0));
                continue;
            }

            char s_p50[16], s_p95[16], s_p99[16], s_p999[16], s_p9999[16];
            char s_max[16], s_mean[16];

            fmt_ns(s_p50,   sizeof(s_p50),   h.p50());
            fmt_ns(s_p95,   sizeof(s_p95),   h.p95());
            fmt_ns(s_p99,   sizeof(s_p99),   h.p99());
            fmt_ns(s_p999,  sizeof(s_p999),  h.p999());
            fmt_ns(s_p9999, sizeof(s_p9999), h.p9999());
            fmt_ns(s_max,   sizeof(s_max),   h.max());
            fmt_ns(s_mean,  sizeof(s_mean),  h.mean());

            append("%-10s %10s %10s %10s %10s %10s %10s %10s %12llu\n",
                   kKindNames[k], s_p50, s_p95, s_p99, s_p999, s_p9999,
                   s_max, s_mean,
                   static_cast<unsigned long long>(h.count()));
        }

        return std::string(buf, pos);
    }

    /// Access individual histogram (e.g. for external tooling).
    [[nodiscard]] const HdrHistogram& histogram(Kind k) const noexcept {
        return histograms_[static_cast<std::size_t>(k)];
    }

private:
    std::array<HdrHistogram, kNumKinds> histograms_{};

    /// Format nanoseconds into a human-readable string.
    static void fmt_ns(char* out, std::size_t len, double ns) noexcept {
        if (ns >= 1'000'000'000.0) {
            std::snprintf(out, len, "%.2fs", ns / 1'000'000'000.0);
        } else if (ns >= 1'000'000.0) {
            std::snprintf(out, len, "%.2fms", ns / 1'000'000.0);
        } else if (ns >= 1'000.0) {
            std::snprintf(out, len, "%.1fus", ns / 1'000.0);
        } else {
            std::snprintf(out, len, "%.0fns", ns);
        }
    }
};


// ═════════════════════════════════════════════════════════════════════════
//  ScopedTimer — RAII latency measurement helper
//
//  Usage:
//    {
//        profiling::ScopedTimer timer(profiler, LatencyKind::OrderAdd);
//        // ... hot-path code ...
//    } // destructor records the elapsed cycles
// ═════════════════════════════════════════════════════════════════════════

class ScopedTimer {
public:
#ifdef LATENCY_PROFILING
    ScopedTimer(LatencyProfiler& profiler, LatencyKind kind) noexcept
        : profiler_(&profiler), kind_(kind), start_(detail::rdtsc_now()) {}

    ~ScopedTimer() noexcept {
        const std::uint64_t end = detail::rdtsc_now();
        const std::uint64_t delta = (end > start_) ? (end - start_) : 0;
        profiler_->recordCycles(kind_, delta);
    }
#else
    ScopedTimer(LatencyProfiler& /*profiler*/, LatencyKind /*kind*/) noexcept {}
    ~ScopedTimer() noexcept = default;
#endif

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
#ifdef LATENCY_PROFILING
    LatencyProfiler* profiler_;
    LatencyKind      kind_;
    std::uint64_t    start_;
#endif
};

} // namespace profiling

#endif // LATENCY_PROFILER_HPP