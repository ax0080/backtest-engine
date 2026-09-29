#pragma once

#include <span>

#if defined(__AVX2__)
#define BACKTEST_HAS_AVX2 1
#endif

namespace backtest::kernels {

// Statistics of an equity curve, from one pass over it. Returns are simple
// returns between consecutive points (0 where the previous point is 0);
// variances are population variances.
struct EquityStats {
    double mean_return       = 0;
    double return_variance   = 0;
    double downside_variance = 0;   // of min(return - threshold, 0)
    double max_drawdown      = 0;   // <= 0, e.g. -0.25 for a 25% drawdown
};

// Requires equity.size() >= 2 and equity[0] > 0.
EquityStats equity_stats_scalar(std::span<const double> equity, double threshold);

#ifdef BACKTEST_HAS_AVX2
EquityStats equity_stats_avx2(std::span<const double> equity, double threshold);
#endif

inline EquityStats equity_stats(std::span<const double> equity, double threshold) {
#ifdef BACKTEST_HAS_AVX2
    return equity_stats_avx2(equity, threshold);
#else
    return equity_stats_scalar(equity, threshold);
#endif
}

} // namespace backtest::kernels
