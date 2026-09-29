#include "backtest/engine.h"

#include "backtest/strategy.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace backtest {

Engine::Engine(const EngineConfig& config) : config_(config) {}

void Engine::add_data(const std::string& symbol, BarSeries bars) {
    data_[symbol] = std::move(bars);
}

// ---------------------------------------------------------------------------
// Event-driven
// ---------------------------------------------------------------------------

PerformanceReport Engine::run(Strategy& strategy) {
    if (data_.empty())
        throw std::runtime_error("No data loaded");

    Portfolio portfolio(config_.initial_cash);
    Broker    broker(portfolio, config_.commission, config_.slippage);
    strategy.broker_    = &broker;
    strategy.portfolio_ = &portfolio;

    // Build a merged timeline: (timestamp, symbol, index).
    struct Entry { Timestamp ts; std::string symbol; std::size_t idx; };
    std::vector<Entry> timeline;
    for (const auto& [sym, bars] : data_)
        for (std::size_t i = 0; i < bars.size(); ++i)
            timeline.push_back({bars.timestamp[i], sym, i});

    std::sort(timeline.begin(), timeline.end(),
              [](const Entry& a, const Entry& b) { return a.ts < b.ts; });

    strategy.on_init();

    std::unordered_map<std::string, double> prices;

    for (std::size_t i = 0; i < timeline.size(); ) {
        Timestamp ts = timeline[i].ts;

        // Collect all entries at this timestamp.
        std::size_t j = i;
        while (j < timeline.size() && timeline[j].ts == ts) ++j;

        // Phase 1: fill pending orders against each symbol's new bar.
        for (std::size_t k = i; k < j; ++k) {
            const auto& e = timeline[k];
            Bar bar = data_[e.symbol][e.idx];
            auto fills = broker.process_bar(e.symbol, bar);
            for (const auto& f : fills) strategy.on_fill(f);
        }

        // Phase 2: let the strategy react.
        for (std::size_t k = i; k < j; ++k) {
            const auto& e = timeline[k];
            Bar bar = data_[e.symbol][e.idx];
            prices[e.symbol]   = bar.close;
            strategy.current_ts_ = ts;
            strategy.on_bar(e.symbol, bar);
        }

        portfolio.snapshot(ts, prices);
        i = j;
    }

    strategy.on_stop();
    strategy.broker_    = nullptr;
    strategy.portfolio_ = nullptr;

    return Metrics::compute(portfolio.equity_curve(),
                            portfolio.equity_timestamps(),
                            portfolio.trades(),
                            config_.initial_cash);
}

// ---------------------------------------------------------------------------
// Vectorized
// ---------------------------------------------------------------------------

PerformanceReport Engine::run_vector(const std::string& symbol,
                                     VectorStrategy& strategy) {
    auto it = data_.find(symbol);
    if (it == data_.end())
        throw std::runtime_error("No data for symbol: " + symbol);

    return run_signals(symbol, strategy.compute(it->second));
}

PerformanceReport Engine::run_signals(const std::string& symbol,
                                      const std::vector<double>& positions) {
    auto it = data_.find(symbol);
    if (it == data_.end())
        throw std::runtime_error("No data for symbol: " + symbol);

    const auto& bars = it->second;
    Portfolio portfolio(config_.initial_cash);
    double current_pos = 0;
    std::unordered_map<std::string, double> prices;

    for (std::size_t i = 0; i < bars.size(); ++i) {
        // positions[i] is the signal computed from data up to bar i (including
        // close[i]).  To avoid look-ahead bias the trade executes at bar i+1's
        // open, so here we apply the PREVIOUS bar's signal.
        double target = (i > 0 && i - 1 < positions.size())
                            ? positions[i - 1] : 0;

        if (i > 0) {
            double delta = target - current_pos;
            if (std::abs(delta) > 1e-9) {
                Side   side = delta > 0 ? Side::Buy : Side::Sell;
                double fp   = bars.open[i] +
                              config_.slippage.compute(bars.open[i], side);
                double qty  = std::abs(delta);
                double comm = config_.commission.compute(fp, qty);

                Fill f;
                f.symbol     = symbol;
                f.side       = side;
                f.price      = fp;
                f.quantity   = qty;
                f.commission = comm;
                f.timestamp  = bars.timestamp[i];
                portfolio.apply_fill(f);

                current_pos = target;
            }
        }

        prices[symbol] = bars.close[i];
        portfolio.snapshot(bars.timestamp[i], prices);
    }

    return Metrics::compute(portfolio.equity_curve(),
                            portfolio.equity_timestamps(),
                            portfolio.trades(),
                            config_.initial_cash);
}

} // namespace backtest
