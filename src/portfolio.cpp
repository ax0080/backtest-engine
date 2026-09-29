#include "backtest/portfolio.h"

#include <cmath>

namespace backtest {

Portfolio::Portfolio(double initial_cash) : cash_(initial_cash) {}

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

        trades_.push_back({fill.symbol,
                           pos.quantity > 0 ? Side::Buy : Side::Sell,
                           close_qty,
                           pos.avg_price,
                           fill.price,
                           pos.entry_time,
                           fill.timestamp,
                           pnl - fill.commission});

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

double Portfolio::position_qty(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    return it == positions_.end() ? 0.0 : it->second.quantity;
}

Position Portfolio::position(const std::string& symbol) const {
    auto it = positions_.find(symbol);
    return it == positions_.end() ? Position{} : it->second;
}

double Portfolio::equity(
        const std::unordered_map<std::string, double>& prices) const {
    double eq = cash_;
    for (const auto& [sym, pos] : positions_) {
        auto pit = prices.find(sym);
        if (pit != prices.end())
            eq += pos.quantity * pit->second;
    }
    return eq;
}

void Portfolio::snapshot(
        Timestamp ts,
        const std::unordered_map<std::string, double>& prices) {
    equity_curve_.push_back(equity(prices));
    equity_ts_.push_back(ts);
}

} // namespace backtest
