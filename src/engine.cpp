#include "backtest/engine.h"

#include "backtest/strategy.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <ranges>
#include <stdexcept>
#include <utility>

namespace backtest {

Engine::Engine(const EngineConfig& config) : config_(config) {}

void Engine::add_data(const std::string& symbol, BarSeries bars) {
    if (!std::ranges::is_sorted(bars.timestamp))
        throw std::invalid_argument("Bars for " + symbol +
                                    " are not in ascending timestamp order");

    auto [it, inserted] = ids_.try_emplace(symbol, static_cast<SymbolId>(data_.size()));
    if (inserted) {
        names_.push_back(symbol);
        data_.push_back(std::move(bars));
    } else {
        data_[it->second] = std::move(bars);
    }
}

SymbolId Engine::require(const std::string& symbol) const {
    auto it = ids_.find(symbol);
    if (it == ids_.end())
        throw std::runtime_error("No data for symbol: " + symbol);
    return it->second;
}

PerformanceReport Engine::finish(Portfolio& portfolio) const {
    PerformanceReport r = Metrics::compute(portfolio.equity_curve(),
                                           portfolio.equity_timestamps(),
                                           portfolio.trade_stats(),
                                           config_.initial_cash);
    r.equity_curve      = portfolio.take_equity_curve();
    r.equity_timestamps = portfolio.take_equity_timestamps();
    return r;
}

// ---------------------------------------------------------------------------
// Event-driven
// ---------------------------------------------------------------------------

PerformanceReport Engine::run(Strategy& strategy) {
    if (data_.empty())
        throw std::runtime_error("No data loaded");

    const std::size_t n_sym = data_.size();
    std::size_t total_bars = 0;
    for (const auto& bars : data_) total_bars += bars.size();

    Portfolio portfolio(config_.initial_cash, n_sym, total_bars);
    Broker    broker(portfolio, config_.commission, config_.slippage);
    strategy.broker_    = &broker;
    strategy.portfolio_ = &portfolio;
    strategy.names_     = &names_;
    strategy.ids_       = &ids_;

    std::vector<std::size_t> cursor(n_sym, 0);
    std::vector<SymbolId>    batch;
    std::vector<double>      prices(n_sym, 0.0);
    std::vector<Fill>        fills;
    batch.reserve(n_sym);
    fills.reserve(Broker::kReservedOrders);

    strategy.on_init();

    // k-way merge of the per-symbol series (each already sorted): every step
    // takes all symbols whose next bar has the smallest timestamp.
    constexpr Timestamp kDone = std::numeric_limits<Timestamp>::max();
    for (;;) {
        Timestamp ts = kDone;
        for (std::size_t s = 0; s < n_sym; ++s)
            if (cursor[s] < data_[s].size())
                ts = std::min(ts, data_[s].timestamp[cursor[s]]);
        if (ts == kDone) break;

        batch.clear();
        for (std::size_t s = 0; s < n_sym; ++s)
            if (cursor[s] < data_[s].size() && data_[s].timestamp[cursor[s]] == ts)
                batch.push_back(static_cast<SymbolId>(s));

        // Phase 1: fill pending orders against each symbol's new bar.
        fills.clear();
        for (SymbolId s : batch)
            broker.process_bar(s, data_[s][cursor[s]], fills);
        for (const Fill& f : fills) strategy.on_fill(f);

        // Phase 2: let the strategy react.
        strategy.current_ts_ = ts;
        for (SymbolId s : batch) {
            Bar bar   = data_[s][cursor[s]];
            prices[s] = bar.close;
            strategy.current_symbol_ = s;
            strategy.on_bar(names_[s], bar);
        }

        portfolio.snapshot(ts, prices);
        for (SymbolId s : batch) ++cursor[s];
    }

    strategy.on_stop();
    strategy.broker_    = nullptr;
    strategy.portfolio_ = nullptr;

    return finish(portfolio);
}

// ---------------------------------------------------------------------------
// Vectorized
// ---------------------------------------------------------------------------

PerformanceReport Engine::run_vector(const std::string& symbol,
                                     VectorStrategy& strategy) {
    return run_signals(symbol, strategy.compute(data_[require(symbol)]));
}

PerformanceReport Engine::run_signals(const std::string& symbol,
                                      std::span<const double> positions) {
    const SymbolId   sym  = require(symbol);
    const BarSeries& bars = data_[sym];

    Portfolio portfolio(config_.initial_cash, data_.size(), bars.size());
    std::vector<double> prices(data_.size(), 0.0);
    double current_pos = 0;

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

                Fill f;
                f.symbol     = sym;
                f.side       = side;
                f.price      = fp;
                f.quantity   = qty;
                f.commission = config_.commission.compute(fp, qty);
                f.timestamp  = bars.timestamp[i];
                portfolio.apply_fill(f);

                current_pos = target;
            }
        }

        prices[sym] = bars.close[i];
        portfolio.snapshot(bars.timestamp[i], prices);
    }

    return finish(portfolio);
}

} // namespace backtest
