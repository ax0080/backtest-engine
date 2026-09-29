#pragma once

#include "broker.h"
#include "metrics.h"
#include "portfolio.h"
#include "series.h"
#include "types.h"

#include <concepts>
#include <functional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>

namespace backtest {

class Strategy;
class VectorStrategy;

template <typename F>
concept SignalFunction = std::invocable<F, const BarSeries&> &&
    std::convertible_to<std::invoke_result_t<F, const BarSeries&>,
                        std::vector<double>>;

struct EngineConfig {
    double          initial_cash = 1'000'000;
    CommissionModel commission;
    SlippageModel   slippage;
};

// All run modes size their buffers before the bar loop starts. The number of
// heap allocations per run is fixed, independent of the number of bars.
class Engine {
public:
    explicit Engine(const EngineConfig& config = {});

    // Bars must be in ascending timestamp order.
    void add_data(const std::string& symbol, BarSeries bars);

    // Event-driven backtest over every symbol, merged by timestamp.
    PerformanceReport run(Strategy& strategy);

    // Vectorized backtest (single symbol).
    PerformanceReport run_vector(const std::string& symbol,
                                 VectorStrategy& strategy);

    // Vectorized from a target position per bar.
    PerformanceReport run_signals(const std::string& symbol,
                                  std::span<const double> positions);

    // Vectorized from any callable (lambda, function object).
    template <SignalFunction F>
    PerformanceReport run_fn(const std::string& symbol, F&& fn) {
        const auto& bars = data_[require(symbol)];
        return run_signals(symbol, std::invoke(std::forward<F>(fn), bars));
    }

    const std::string& symbol_name(SymbolId id) const { return names_[id]; }

private:
    SymbolId          require(const std::string& symbol) const;
    PerformanceReport finish(Portfolio& portfolio) const;

    EngineConfig                              config_;
    std::vector<std::string>                  names_;
    std::vector<BarSeries>                    data_;
    std::unordered_map<std::string, SymbolId> ids_;
};

} // namespace backtest
