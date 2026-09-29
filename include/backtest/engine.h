#pragma once

#include "broker.h"
#include "metrics.h"
#include "portfolio.h"
#include "series.h"
#include "types.h"

#include <concepts>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>

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

class Engine {
public:
    explicit Engine(const EngineConfig& config = {});

    void add_data(const std::string& symbol, BarSeries bars);

    // Event-driven backtest.
    PerformanceReport run(Strategy& strategy);

    // Vectorized backtest (single symbol).
    PerformanceReport run_vector(const std::string& symbol,
                                VectorStrategy& strategy);

    // Vectorized from raw position array.
    PerformanceReport run_signals(const std::string& symbol,
                                 const std::vector<double>& positions);

    // Vectorized from any callable (lambda, function object).
    template <SignalFunction F>
    PerformanceReport run_fn(const std::string& symbol, F&& fn) {
        auto it = data_.find(symbol);
        if (it == data_.end())
            throw std::runtime_error("No data for symbol: " + symbol);
        return run_signals(symbol,
                           std::invoke(std::forward<F>(fn), it->second));
    }

private:
    EngineConfig config_;
    std::unordered_map<std::string, BarSeries> data_;
};

} // namespace backtest
