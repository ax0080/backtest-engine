#pragma once

#include "types.h"

#include <cstddef>
#include <span>
#include <utility>
#include <vector>

namespace backtest {

// Positions are indexed by SymbolId. All storage is sized up front, so
// applying fills and taking snapshots never allocates.
class Portfolio {
public:
    Portfolio(double initial_cash, std::size_t num_symbols,
              std::size_t max_snapshots);

    void apply_fill(const Fill& fill);

    double   cash() const { return cash_; }
    double   position_qty(SymbolId symbol) const { return positions_[symbol].quantity; }
    Position position(SymbolId symbol) const     { return positions_[symbol]; }

    // prices[s] is the latest price of symbol s.
    double equity(std::span<const double> prices) const;
    void   snapshot(Timestamp ts, std::span<const double> prices);

    const std::vector<double>&    equity_curve()      const { return equity_curve_; }
    const std::vector<Timestamp>& equity_timestamps() const { return equity_ts_; }
    const TradeStats&             trade_stats()       const { return stats_; }

    std::vector<double>    take_equity_curve()      { return std::move(equity_curve_); }
    std::vector<Timestamp> take_equity_timestamps() { return std::move(equity_ts_); }

private:
    double                 cash_;
    std::vector<Position>  positions_;
    std::vector<double>    equity_curve_;
    std::vector<Timestamp> equity_ts_;
    TradeStats             stats_;
};

} // namespace backtest
