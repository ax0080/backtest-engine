#pragma once

#include "types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace backtest {

class Portfolio {
public:
    explicit Portfolio(double initial_cash);

    void apply_fill(const Fill& fill);

    double   cash() const { return cash_; }
    double   position_qty(const std::string& symbol) const;
    Position position(const std::string& symbol) const;

    double equity(const std::unordered_map<std::string, double>& prices) const;
    void   snapshot(Timestamp ts,
                    const std::unordered_map<std::string, double>& prices);

    const std::vector<double>&    equity_curve()      const { return equity_curve_; }
    const std::vector<Timestamp>& equity_timestamps() const { return equity_ts_; }
    const std::vector<RoundTrip>& trades()            const { return trades_; }

private:
    double cash_;
    std::unordered_map<std::string, Position> positions_;
    std::vector<double>    equity_curve_;
    std::vector<Timestamp> equity_ts_;
    std::vector<RoundTrip> trades_;
};

} // namespace backtest
