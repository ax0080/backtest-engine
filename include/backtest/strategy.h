#pragma once

#include "series.h"
#include "types.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace backtest {

class Broker;
class Portfolio;

// Event-driven strategy: receives one bar at a time and may submit orders.
class Strategy {
public:
    virtual ~Strategy() = default;

    virtual void on_init() {}
    virtual void on_bar(const std::string& symbol, const Bar& bar) = 0;
    virtual void on_fill(const Fill& fill) { (void)fill; }
    virtual void on_stop() {}

protected:
    void   buy(const std::string& symbol, double qty);
    void   sell(const std::string& symbol, double qty);
    void   set_target(const std::string& symbol, double target_qty);
    double position(const std::string& symbol) const;
    double cash() const;

    const std::string& symbol_name(SymbolId id) const { return (*names_)[id]; }

private:
    friend class Engine;

    SymbolId id_of(const std::string& symbol) const;

    Broker*    broker_     = nullptr;
    Portfolio* portfolio_  = nullptr;
    Timestamp  current_ts_ = 0;
    SymbolId   current_symbol_ = 0;
    const std::vector<std::string>*                     names_ = nullptr;
    const std::unordered_map<std::string, SymbolId>*    ids_   = nullptr;
};

// Vectorized strategy: receives the full bar series and returns a target
// position for each bar.  The engine converts position changes to fills.
class VectorStrategy {
public:
    virtual ~VectorStrategy() = default;
    virtual std::vector<double> compute(const BarSeries& bars) = 0;
};

} // namespace backtest
