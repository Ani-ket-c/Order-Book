#include "MicrostructureEngine.hpp"

#include <algorithm>

namespace analytics {

namespace {

double clamp(double value, double lo, double hi) noexcept {
    return value < lo ? lo : (value > hi ? hi : value);
}

}  // namespace

MicrostructureEngine::MicrostructureEngine() {
    buffers_[0] = current_;
    buffers_[1] = current_;
}

void MicrostructureEngine::onTrade(bool aggressor_buy,
                                   int exec_price,
                                   int exec_qty,
                                   int best_bid_price,
                                   int best_ask_price,
                                   int best_bid_qty,
                                   int best_ask_qty) noexcept {
    updateTradeStats(aggressor_buy, exec_price, exec_qty);
    updateFromBook(best_bid_price, best_ask_price, best_bid_qty, best_ask_qty);
    publishSnapshot();
}

void MicrostructureEngine::onCancel(int best_bid_price,
                                    int best_ask_price,
                                    int best_bid_qty,
                                    int best_ask_qty) noexcept {
    updateFromBook(best_bid_price, best_ask_price, best_bid_qty, best_ask_qty);
    publishSnapshot();
}

MetricsSnapshot MicrostructureEngine::readSnapshot() const noexcept {
    const std::uint32_t index = active_index_.load(std::memory_order_acquire);
    return buffers_[index];
}

double MicrostructureEngine::signalStrength(const MetricsSnapshot& snapshot) const noexcept {
    const double imbalance = (snapshot.queue_imbalance * 2.0) - 1.0;
    return clamp((snapshot.ofi + imbalance) * 0.5, -1.0, 1.0);
}

void MicrostructureEngine::updateFromBook(int best_bid_price,
                                          int best_ask_price,
                                          int best_bid_qty,
                                          int best_ask_qty) noexcept {
    current_.best_bid_price = best_bid_price;
    current_.best_ask_price = best_ask_price;
    current_.best_bid_qty = best_bid_qty;
    current_.best_ask_qty = best_ask_qty;

    const bool has_bbo = best_bid_price > 0 && best_ask_price > 0;
    if (has_bbo) {
        const double spread_ticks = static_cast<double>(best_ask_price - best_bid_price);
        const double mid_price = 0.5 * (static_cast<double>(best_ask_price) +
                                        static_cast<double>(best_bid_price));
        current_.spread_ticks = spread_ticks;
        current_.mid_price = mid_price;
        current_.spread_bps = mid_price > 0.0 ? (spread_ticks / mid_price) * 10000.0 : 0.0;

        const double depth_total = static_cast<double>(best_bid_qty + best_ask_qty);
        current_.queue_imbalance = depth_total > 0.0
            ? static_cast<double>(best_bid_qty) / depth_total
            : 0.5;

        current_.micro_price = depth_total > 0.0
            ? (static_cast<double>(best_bid_price) * static_cast<double>(best_ask_qty) +
               static_cast<double>(best_ask_price) * static_cast<double>(best_bid_qty)) /
              depth_total
            : mid_price;
    } else {
        current_.spread_ticks = 0.0;
        current_.spread_bps = 0.0;
        current_.mid_price = 0.0;
        current_.micro_price = 0.0;
        current_.queue_imbalance = 0.5;
    }

    double bid_delta = 0.0;
    double ask_delta = 0.0;
    if (last_best_bid_price_ != 0 || last_best_ask_price_ != 0) {
        if (best_bid_price > last_best_bid_price_) {
            bid_delta = static_cast<double>(best_bid_qty);
        } else if (best_bid_price < last_best_bid_price_) {
            bid_delta = -static_cast<double>(last_best_bid_qty_);
        } else {
            bid_delta = static_cast<double>(best_bid_qty - last_best_bid_qty_);
        }

        if (best_ask_price < last_best_ask_price_) {
            ask_delta = static_cast<double>(best_ask_qty);
        } else if (best_ask_price > last_best_ask_price_) {
            ask_delta = -static_cast<double>(last_best_ask_qty_);
        } else {
            ask_delta = static_cast<double>(best_ask_qty - last_best_ask_qty_);
        }
    }

    const double denom = bid_delta + ask_delta;
    current_.ofi = denom != 0.0 ? (bid_delta - ask_delta) / denom : 0.0;

    last_best_bid_price_ = best_bid_price;
    last_best_ask_price_ = best_ask_price;
    last_best_bid_qty_ = best_bid_qty;
    last_best_ask_qty_ = best_ask_qty;

    current_.signal_strength = signalStrength(current_);
}

void MicrostructureEngine::updateTradeStats(bool aggressor_buy,
                                            int exec_price,
                                            int exec_qty) noexcept {
    const double qty = static_cast<double>(exec_qty);
    const double px_qty = static_cast<double>(exec_price) * qty;

    if (vwap_count_ == kTradeWindow) {
        vwap_sum_px_qty_ -= vwap_px_qty_[vwap_index_];
        vwap_sum_qty_ -= vwap_qty_[vwap_index_];
    } else {
        ++vwap_count_;
    }

    vwap_px_qty_[vwap_index_] = px_qty;
    vwap_qty_[vwap_index_] = qty;
    vwap_sum_px_qty_ += px_qty;
    vwap_sum_qty_ += qty;
    vwap_index_ = (vwap_index_ + 1) % kTradeWindow;

    current_.vwap = vwap_sum_qty_ > 0.0 ? vwap_sum_px_qty_ / vwap_sum_qty_ : 0.0;

    if (last_trade_price_ != 0.0) {
        const double signed_qty = (aggressor_buy ? 1.0 : -1.0) * qty;
        const double price_delta = static_cast<double>(exec_price) - last_trade_price_;

        if (kyle_count_ == kTradeWindow) {
            kyle_sum_x_ -= kyle_x_[kyle_index_];
            kyle_sum_y_ -= kyle_y_[kyle_index_];
            kyle_sum_x2_ -= kyle_x_[kyle_index_] * kyle_x_[kyle_index_];
            kyle_sum_xy_ -= kyle_x_[kyle_index_] * kyle_y_[kyle_index_];
        } else {
            ++kyle_count_;
        }

        kyle_x_[kyle_index_] = signed_qty;
        kyle_y_[kyle_index_] = price_delta;
        kyle_sum_x_ += signed_qty;
        kyle_sum_y_ += price_delta;
        kyle_sum_x2_ += signed_qty * signed_qty;
        kyle_sum_xy_ += signed_qty * price_delta;
        kyle_index_ = (kyle_index_ + 1) % kTradeWindow;

        if (kyle_count_ >= 2) {
            const double count = static_cast<double>(kyle_count_);
            const double mean_x = kyle_sum_x_ / count;
            const double mean_y = kyle_sum_y_ / count;
            const double denom = kyle_sum_x2_ - count * mean_x * mean_x;
            current_.kyle_lambda = denom != 0.0
                ? (kyle_sum_xy_ - count * mean_x * mean_y) / denom
                : 0.0;
        } else {
            current_.kyle_lambda = 0.0;
        }
    }

    last_trade_price_ = static_cast<double>(exec_price);
}

void MicrostructureEngine::publishSnapshot() noexcept {
    const std::uint64_t next_version =
        version_.fetch_add(1, std::memory_order_acq_rel) + 1;
    current_.version = next_version;

    const std::uint32_t next_index = 1U - active_index_.load(std::memory_order_relaxed);
    buffers_[next_index] = current_;
    active_index_.store(next_index, std::memory_order_release);
}

}  // namespace analytics
