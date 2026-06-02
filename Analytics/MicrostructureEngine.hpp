#ifndef MICROSTRUCTURE_ENGINE_HPP
#define MICROSTRUCTURE_ENGINE_HPP

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace analytics {

struct MetricsSnapshot {
    std::uint64_t version{0};
    int best_bid_price{0};
    int best_ask_price{0};
    int best_bid_qty{0};
    int best_ask_qty{0};
    double spread_ticks{0.0};
    double spread_bps{0.0};
    double mid_price{0.0};
    double micro_price{0.0};
    double ofi{0.0};
    double vwap{0.0};
    double kyle_lambda{0.0};
    double queue_imbalance{0.5};
    double signal_strength{0.0};
};

class MicrostructureEngine {
public:
    static constexpr std::size_t kTradeWindow = 1000;

    MicrostructureEngine();

    void onTrade(bool aggressor_buy,
                 int exec_price,
                 int exec_qty,
                 int best_bid_price,
                 int best_ask_price,
                 int best_bid_qty,
                 int best_ask_qty) noexcept;

    void onCancel(int best_bid_price,
                  int best_ask_price,
                  int best_bid_qty,
                  int best_ask_qty) noexcept;

    MetricsSnapshot readSnapshot() const noexcept;

    double signalStrength(const MetricsSnapshot& snapshot) const noexcept;

private:
    void updateFromBook(int best_bid_price,
                        int best_ask_price,
                        int best_bid_qty,
                        int best_ask_qty) noexcept;

    void updateTradeStats(bool aggressor_buy, int exec_price, int exec_qty) noexcept;

    void publishSnapshot() noexcept;

    MetricsSnapshot buffers_[2]{};
    std::atomic<std::uint32_t> active_index_{0};
    std::atomic<std::uint64_t> version_{0};

    int last_best_bid_price_{0};
    int last_best_ask_price_{0};
    int last_best_bid_qty_{0};
    int last_best_ask_qty_{0};

    std::array<double, kTradeWindow> vwap_px_qty_{};
    std::array<double, kTradeWindow> vwap_qty_{};
    std::size_t vwap_index_{0};
    std::size_t vwap_count_{0};
    double vwap_sum_px_qty_{0.0};
    double vwap_sum_qty_{0.0};

    std::array<double, kTradeWindow> kyle_x_{};
    std::array<double, kTradeWindow> kyle_y_{};
    std::size_t kyle_index_{0};
    std::size_t kyle_count_{0};
    double kyle_sum_x_{0.0};
    double kyle_sum_y_{0.0};
    double kyle_sum_x2_{0.0};
    double kyle_sum_xy_{0.0};
    double last_trade_price_{0.0};

    MetricsSnapshot current_{};
};

}  // namespace analytics

#endif  // MICROSTRUCTURE_ENGINE_HPP
