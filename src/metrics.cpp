#include "backtest/metrics.h"

#include "backtest/kernels.h"

#include <cmath>

namespace backtest {

PerformanceReport Metrics::compute(
        std::span<const double>    equity_curve,
        std::span<const Timestamp> timestamps,
        const TradeStats&          trades,
        double initial_cash,
        double risk_free_rate,
        int    trading_days_per_year) {

    PerformanceReport r;

    r.total_trades = trades.count;
    if (trades.count > 0) {
        r.win_rate      = static_cast<double>(trades.wins) / trades.count;
        r.profit_factor = trades.gross_loss > 0 ? trades.gross_profit / trades.gross_loss : 0;
        r.avg_trade_pnl = trades.total_pnl / trades.count;
    }

    if (equity_curve.size() < 2) return r;

    // ---- Return ----------------------------------------------------------
    r.total_return = (equity_curve.back() - initial_cash) / initial_cash;

    double ms_span = static_cast<double>(timestamps.back() - timestamps.front());
    double years   = ms_span / (365.25 * 24 * 3600 * 1000.0);
    if (years > 0)
        r.annualized_return = std::pow(1.0 + r.total_return, 1.0 / years) - 1.0;

    // ---- Risk-adjusted + drawdown (one pass, SIMD when available) --------
    if (equity_curve.front() <= 0) return r;

    const double daily_rfr = std::pow(1.0 + risk_free_rate,
                                      1.0 / trading_days_per_year) - 1.0;
    const auto   s = kernels::equity_stats(equity_curve, daily_rfr);

    const double std_dev      = std::sqrt(s.return_variance);
    const double downside_dev = std::sqrt(s.downside_variance);
    const double annualize    = std::sqrt(static_cast<double>(trading_days_per_year));
    r.sharpe_ratio  = std_dev > 0 ? (s.mean_return - daily_rfr) / std_dev * annualize : 0;
    r.sortino_ratio = downside_dev > 0 ? (s.mean_return - daily_rfr) / downside_dev * annualize : 0;
    r.max_drawdown  = s.max_drawdown;

    r.calmar_ratio = r.max_drawdown < -1e-12
        ? r.annualized_return / std::abs(r.max_drawdown) : 0;

    return r;
}

} // namespace backtest
