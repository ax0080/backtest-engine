#pragma once

#include "types.h"

#include <span>

namespace backtest {

class Metrics {
public:
    // Fills every field except equity_curve / equity_timestamps.
    // Does not allocate.
    static PerformanceReport compute(
        std::span<const double>    equity_curve,
        std::span<const Timestamp> timestamps,
        const TradeStats&          trades,
        double initial_cash,
        double risk_free_rate = 0.0,
        int    trading_days_per_year = 252);
};

} // namespace backtest
