#include "backtest/portfolio.h"

#include <algorithm>
#include <cmath>

namespace backtest {

Portfolio::Portfolio(double initial_cash, std::size_t num_symbols,
                     std::size_t max_snapshots)
    : cash_(initial_cash), positions_(num_symbols) {
    equity_curve_.reserve(max_snapshots);
    equity_ts_.reserve(max_snapshots);
}

void Portfolio::apply_fill(const Fill& fill) {
    double signed_qty = fill.side == Side::Buy ? fill.quantity : -fill.quantity;
    auto&  pos        = positions_[fill.symbol];

    bool same_direction = (pos.quantity >= 0 && signed_qty > 0) ||
                          (pos.quantity <= 0 && signed_qty < 0);

    if (pos.quantity == 0 || same_direction) {
        // Opening or adding.
        double total_cost = pos.quantity * pos.avg_price + signed_qty * fill.price;
        pos.quantity += signed_qty;
        if (std::abs(pos.quantity) > 1e-12)
            pos.avg_price = total_cost / pos.quantity;
        if (pos.entry_time == 0) pos.entry_time = fill.timestamp;
    } else {
        // Reducing or flipping.
        double close_qty = std::min(std::abs(signed_qty), std::abs(pos.quantity));
        double direction = pos.quantity > 0 ? 1.0 : -1.0;
        double pnl       = close_qty * (fill.price - pos.avg_price) * direction;
        pos.realized_pnl += pnl;
        stats_.record(pnl - fill.commission);

        double remaining = std::abs(signed_qty) - close_qty;
        if (remaining < 1e-12) {
            pos.quantity += signed_qty;
            if (std::abs(pos.quantity) < 1e-12) {
                pos.quantity   = 0;
                pos.avg_price  = 0;
                pos.entry_time = 0;
            }
        } else {
            pos.quantity   = signed_qty > 0 ? remaining : -remaining;
            pos.avg_price  = fill.price;
            pos.entry_time = fill.timestamp;
        }
    }

    cash_ -= signed_qty * fill.price + fill.commission;
}

double Portfolio::equity(std::span<const double> prices) const {
    double eq = cash_;
    for (std::size_t s = 0; s < positions_.size(); ++s)
        eq += positions_[s].quantity * prices[s];
    return eq;
}

void Portfolio::snapshot(Timestamp ts, std::span<const double> prices) {
    equity_curve_.push_back(equity(prices));
    equity_ts_.push_back(ts);
}

} // namespace backtest
