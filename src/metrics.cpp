#include "backtest/metrics.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace backtest {

PerformanceReport Metrics::compute(
        const std::vector<double>&    equity_curve,
        const std::vector<Timestamp>& timestamps,
        const std::vector<RoundTrip>& trades,
        double initial_cash,
        double risk_free_rate,
        int    trading_days_per_year) {

    PerformanceReport r;
    r.equity_curve      = equity_curve;
    r.equity_timestamps = timestamps;

    if (equity_curve.size() < 2) return r;

    // ---- Return ----------------------------------------------------------
    r.total_return = (equity_curve.back() - initial_cash) / initial_cash;

    double ms_span = static_cast<double>(timestamps.back() - timestamps.front());
    double years   = ms_span / (365.25 * 24 * 3600 * 1000.0);
    if (years > 0)
        r.annualized_return = std::pow(1.0 + r.total_return, 1.0 / years) - 1.0;

    // ---- Daily returns ---------------------------------------------------
    std::vector<double> rets;
    rets.reserve(equity_curve.size() - 1);
    for (std::size_t i = 1; i < equity_curve.size(); ++i) {
        if (equity_curve[i - 1] != 0)
            rets.push_back((equity_curve[i] - equity_curve[i - 1]) /
                           equity_curve[i - 1]);
        else
            rets.push_back(0.0);
    }

    double mean_ret = std::accumulate(rets.begin(), rets.end(), 0.0) /
                      static_cast<double>(rets.size());

    double daily_rfr = std::pow(1.0 + risk_free_rate,
                                1.0 / trading_days_per_year) - 1.0;

    // Standard deviation.
    double var = 0;
    for (double r_ : rets) var += (r_ - mean_ret) * (r_ - mean_ret);
    double std_dev = std::sqrt(var / static_cast<double>(rets.size()));

    // Downside deviation (below rfr).
    double down_var = 0;
    int    down_n   = 0;
    for (double r_ : rets) {
        double excess = r_ - daily_rfr;
        if (excess < 0) { down_var += excess * excess; ++down_n; }
    }
    double downside_dev = down_n > 0
        ? std::sqrt(down_var / static_cast<double>(rets.size()))
        : 0;

    double annualize = std::sqrt(static_cast<double>(trading_days_per_year));
    r.sharpe_ratio  = std_dev > 0
        ? (mean_ret - daily_rfr) / std_dev * annualize : 0;
    r.sortino_ratio = downside_dev > 0
        ? (mean_ret - daily_rfr) / downside_dev * annualize : 0;

    // ---- Max drawdown ----------------------------------------------------
    double peak = equity_curve.front();
    r.max_drawdown = 0;
    for (double eq : equity_curve) {
        if (eq > peak) peak = eq;
        double dd = (eq - peak) / peak;
        if (dd < r.max_drawdown) r.max_drawdown = dd;
    }

    r.calmar_ratio = r.max_drawdown < -1e-12
        ? r.annualized_return / std::abs(r.max_drawdown) : 0;

    // ---- Trade stats -----------------------------------------------------
    r.total_trades = static_cast<int>(trades.size());
    if (r.total_trades > 0) {
        double wins = 0, gross_profit = 0, gross_loss = 0, total_pnl = 0;
        for (const auto& t : trades) {
            total_pnl += t.pnl;
            if (t.pnl > 0) { ++wins; gross_profit += t.pnl; }
            else            { gross_loss += std::abs(t.pnl); }
        }
        r.win_rate       = wins / r.total_trades;
        r.profit_factor  = gross_loss > 0 ? gross_profit / gross_loss : 0;
        r.avg_trade_pnl  = total_pnl / r.total_trades;
    }

    return r;
}

} // namespace backtest
