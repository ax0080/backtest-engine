#pragma once

#include "broker.h"
#include "metrics.h"
#include "portfolio.h"
#include "series.h"
#include "types.h"

#include <string>
#include <unordered_map>

namespace backtest {

class Strategy;
class VectorStrategy;

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

private:
    EngineConfig config_;
    std::unordered_map<std::string, BarSeries> data_;
};

} // namespace backtest
