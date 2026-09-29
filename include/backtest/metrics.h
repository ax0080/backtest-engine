#pragma once

#include "types.h"

#include <vector>

namespace backtest {

class Metrics {
public:
    static PerformanceReport compute(
        const std::vector<double>&    equity_curve,
        const std::vector<Timestamp>& timestamps,
        const std::vector<RoundTrip>& trades,
        double initial_cash,
        double risk_free_rate = 0.0,
        int    trading_days_per_year = 252);
};

} // namespace backtest
