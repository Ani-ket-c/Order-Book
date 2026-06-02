#ifndef GATEWAY_SPSC_QUEUE_HPP
#define GATEWAY_SPSC_QUEUE_HPP

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>

#include "Limit_Order_Book/Order.hpp"

#if defined(_MSC_VER)
#include <intrin.h>
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#else
#error "SPSCQueue requires x86 TSC (__rdtsc)"
#endif

namespace gateway {

namespace detail {

inline std::uint64_t rdtsc() noexcept {
#if defined(_MSC_VER)
    return static_cast<std::uint64_t>(__rdtsc());
#else
    return static_cast<std::uint64_t>(__rdtsc());
#endif
}

}  // namespace detail

// ---------------------------------------------------------------------------
// TSC calibration and gateway latency measurement
// ---------------------------------------------------------------------------

class LatencyTracker {
public:
    static void calibrate() {
        if (calibrated_.load(std::memory_order_acquire)) {
            return;
        }

        constexpr int samples = 32;
        double total_ratio = 0.0;
        int valid = 0;

        for (int i = 0; i < samples; ++i) {
            const auto t0 = std::chrono::steady_clock::now();
            const std::uint64_t c0 = detail::rdtsc();

            // Busy-spin ~2 ms (off hot path; avoids coarse OS sleep timers).
            const auto deadline = t0 + std::chrono::milliseconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
            }

            const std::uint64_t c1 = detail::rdtsc();
            const auto t1 = std::chrono::steady_clock::now();

            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
            const std::uint64_t cycles = c1 - c0;
            if (cycles > 0 && ns > 0) {
                total_ratio += static_cast<double>(ns) / static_cast<double>(cycles);
                ++valid;
            }
        }

        if (valid > 0) {
            ns_per_cycle_.store(total_ratio / static_cast<double>(valid),
                                std::memory_order_release);
            calibrated_.store(true, std::memory_order_release);
        }
    }

    [[nodiscard]] static bool is_calibrated() noexcept {
        return calibrated_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static double ns_per_cycle() noexcept {
        return ns_per_cycle_.load(std::memory_order_acquire);
    }

    [[nodiscard]] static std::uint64_t now_cycles() noexcept {
        return detail::rdtsc();
    }

    [[nodiscard]] static double cycles_to_ns(std::uint64_t cycles) noexcept {
        return static_cast<double>(cycles) * ns_per_cycle();
    }

    [[nodiscard]] std::uint64_t stamp_enqueue() noexcept {
        last_enqueue_cycles_ = detail::rdtsc();
        return last_enqueue_cycles_;
    }

    // Returns end-to-end latency in nanoseconds for the last dequeue.
    [[nodiscard]] double record_dequeue(std::uint64_t enqueue_cycles) noexcept {
        const std::uint64_t dequeue_cycles = detail::rdtsc();
        const std::uint64_t delta =
            dequeue_cycles > enqueue_cycles ? dequeue_cycles - enqueue_cycles : 0U;

        last_latency_ns_ = cycles_to_ns(delta);
        total_cycles_ += delta;
        ++sample_count_;
        return last_latency_ns_;
    }

    [[nodiscard]] double last_latency_ns() const noexcept {
        return last_latency_ns_;
    }

    [[nodiscard]] double mean_latency_ns() const noexcept {
        if (sample_count_ == 0) {
            return 0.0;
        }
        return cycles_to_ns(total_cycles_) / static_cast<double>(sample_count_);
    }

    [[nodiscard]] std::uint64_t sample_count() const noexcept {
        return sample_count_;
    }

    void reset() noexcept {
        last_enqueue_cycles_ = 0;
        last_latency_ns_ = 0.0;
        total_cycles_ = 0;
        sample_count_ = 0;
    }

private:
    static inline std::atomic<double> ns_per_cycle_{1.0};
    static inline std::atomic<bool> calibrated_{false};

    std::uint64_t last_enqueue_cycles_{0};
    double last_latency_ns_{0.0};
    std::uint64_t total_cycles_{0};
    std::uint64_t sample_count_{0};
};

// ---------------------------------------------------------------------------
// Transport types for gateway tests or external feeds
// ---------------------------------------------------------------------------

struct GatewayOrder {
    int idNumber{0};
    bool buyOrSell{false};
    int shares{0};
    int limit{0};
};

struct GatewayExecutionReport {
    int makerOrderId{0};
    int takerOrderId{0};
    int execPrice{0};
    int execQty{0};
};

static_assert(std::is_trivially_copyable_v<GatewayOrder>);
static_assert(std::is_trivially_copyable_v<GatewayExecutionReport>);

// ---------------------------------------------------------------------------
// Lock-free SPSC ring buffer (power-of-2 capacity, cache-line padded indices)
// ---------------------------------------------------------------------------

template <typename T, std::size_t Capacity>
class SPSCQueue {
    static_assert((Capacity > 0) && ((Capacity & (Capacity - 1)) == 0),
                  "SPSCQueue capacity must be a power of two");
    static_assert(std::is_nothrow_destructible_v<T>,
                  "SPSCQueue element type must be nothrow destructible");
    static_assert(std::is_nothrow_move_constructible_v<T> ||
                      std::is_nothrow_copy_constructible_v<T>,
                  "SPSCQueue element type must be nothrow move/copy constructible");
    static_assert(std::is_nothrow_move_assignable_v<T> ||
                      std::is_nothrow_copy_assignable_v<T>,
                  "SPSCQueue element type must be nothrow move/copy assignable");

public:
    static constexpr std::size_t capacity() noexcept { return Capacity; }

    SPSCQueue() : ring_(std::make_unique<Storage[]>(Capacity)) {}

    ~SPSCQueue() { clear(); }

    SPSCQueue(const SPSCQueue&) = delete;
    SPSCQueue& operator=(const SPSCQueue&) = delete;

    SPSCQueue(SPSCQueue&&) = delete;
    SPSCQueue& operator=(SPSCQueue&&) = delete;

    [[nodiscard]] bool try_push(const T& item) noexcept {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        T* slot = slot_ptr(head);
        std::construct_at(slot, item);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_push(T&& item) noexcept {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        T* slot = slot_ptr(head);
        std::construct_at(slot, std::move(item));
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    template <typename... Args>
    [[nodiscard]] bool try_emplace(Args&&... args) noexcept {
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        if (head - tail_.load(std::memory_order_acquire) >= Capacity) {
            return false;
        }
        T* slot = slot_ptr(head);
        std::construct_at(slot, std::forward<Args>(args)...);
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& item) noexcept {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return false;
        }
        T* slot = slot_ptr(tail);
        item = std::move(*slot);
        std::destroy_at(slot);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] std::optional<T> try_pop() noexcept {
        const std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T* slot = slot_ptr(tail);
        std::optional<T> out(std::in_place, std::move(*slot));
        std::destroy_at(slot);
        tail_.store(tail + 1, std::memory_order_release);
        return out;
    }

    [[nodiscard]] std::size_t size_approx() const noexcept {
        const std::uint64_t head = head_.load(std::memory_order_acquire);
        const std::uint64_t tail = tail_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(head - tail);
    }

    [[nodiscard]] bool empty() const noexcept { return size_approx() == 0; }

    [[nodiscard]] bool full() const noexcept { return size_approx() >= Capacity; }

    void clear() noexcept {
        std::uint64_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint64_t head = head_.load(std::memory_order_relaxed);
        while (tail != head) {
            T* slot = slot_ptr(tail);
            std::destroy_at(slot);
            ++tail;
        }
        tail_.store(head, std::memory_order_relaxed);
    }

private:
    using Storage = std::aligned_storage_t<sizeof(T), alignof(T)>;

    static constexpr std::size_t mask_ = Capacity - 1;

    T* slot_ptr(std::uint64_t index) noexcept {
        return std::launder(reinterpret_cast<T*>(&ring_[index & mask_]));
    }

    alignas(64) std::atomic<std::uint64_t> head_{0};
    alignas(64) std::atomic<std::uint64_t> tail_{0};
    // One-time heap allocation at construction (not on push/pop hot path).
    std::unique_ptr<Storage[]> ring_;
};

// Default queue message: use the existing Order struct from the core engine.
using DefaultOrderMessage = ::Order;
using DefaultExecutionReport = GatewayExecutionReport;

template <typename OrderMessage>
struct StampedMessage {
    OrderMessage payload;
    std::uint64_t enqueue_tsc{0};

    StampedMessage(const OrderMessage& value, std::uint64_t tsc)
        : payload(value), enqueue_tsc(tsc) {}

    StampedMessage(OrderMessage&& value, std::uint64_t tsc)
        : payload(std::move(value)), enqueue_tsc(tsc) {}
};

template <typename OrderMessage = DefaultOrderMessage,
          typename ExecutionReport = DefaultExecutionReport,
          std::size_t InboundCapacity = 65536,
          std::size_t OutboundCapacity = 65536>
class OrderGateway {
public:
    static constexpr std::size_t kDefaultInboundCapacity = InboundCapacity;
    static constexpr std::size_t kDefaultOutboundCapacity = OutboundCapacity;

    OrderGateway() { LatencyTracker::calibrate(); }

    [[nodiscard]] bool enqueue_order(const OrderMessage& order) noexcept {
        return inbound_.try_emplace(order, latency_.stamp_enqueue());
    }

    [[nodiscard]] bool enqueue_order(OrderMessage&& order) noexcept {
        return inbound_.try_emplace(std::move(order), latency_.stamp_enqueue());
    }

    [[nodiscard]] bool dequeue_order(OrderMessage& order) noexcept {
        auto stamped = inbound_.try_pop();
        if (!stamped) {
            return false;
        }
        latency_.record_dequeue(stamped->enqueue_tsc);
        order = std::move(stamped->payload);
        return true;
    }

    [[nodiscard]] bool publish_execution(const ExecutionReport& report) noexcept {
        return outbound_.try_emplace(report, outbound_latency_.stamp_enqueue());
    }

    [[nodiscard]] bool publish_execution(ExecutionReport&& report) noexcept {
        return outbound_.try_emplace(std::move(report), outbound_latency_.stamp_enqueue());
    }

    [[nodiscard]] bool dequeue_execution(ExecutionReport& report) noexcept {
        auto stamped = outbound_.try_pop();
        if (!stamped) {
            return false;
        }
        outbound_latency_.record_dequeue(stamped->enqueue_tsc);
        report = std::move(stamped->payload);
        return true;
    }

    [[nodiscard]] LatencyTracker& inbound_latency_tracker() noexcept { return latency_; }
    [[nodiscard]] const LatencyTracker& inbound_latency_tracker() const noexcept {
        return latency_;
    }

    [[nodiscard]] LatencyTracker& outbound_latency_tracker() noexcept {
        return outbound_latency_;
    }

    [[nodiscard]] const LatencyTracker& outbound_latency_tracker() const noexcept {
        return outbound_latency_;
    }

    [[nodiscard]] std::size_t inbound_depth() const noexcept {
        return inbound_.size_approx();
    }

    [[nodiscard]] std::size_t outbound_depth() const noexcept {
        return outbound_.size_approx();
    }

private:
    SPSCQueue<StampedMessage<OrderMessage>, InboundCapacity> inbound_{};
    SPSCQueue<StampedMessage<ExecutionReport>, OutboundCapacity> outbound_{};
    LatencyTracker latency_{};
    LatencyTracker outbound_latency_{};
};

}  // namespace gateway

#endif  // GATEWAY_SPSC_QUEUE_HPP
