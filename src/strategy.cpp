#include "backtest/strategy.h"

#include "backtest/broker.h"
#include "backtest/portfolio.h"

#include <stdexcept>

namespace backtest {

// on_bar() passes a reference to the engine's own name string, so a strategy
// that trades the bar's symbol resolves it by address, without hashing.
SymbolId Strategy::id_of(const std::string& symbol) const {
    if (&symbol == &(*names_)[current_symbol_]) return current_symbol_;
    auto it = ids_->find(symbol);
    if (it == ids_->end())
        throw std::invalid_argument("Unknown symbol: " + symbol);
    return it->second;
}

void Strategy::buy(const std::string& symbol, double qty) {
    broker_->submit_market(id_of(symbol), Side::Buy, qty, current_ts_);
}

void Strategy::sell(const std::string& symbol, double qty) {
    broker_->submit_market(id_of(symbol), Side::Sell, qty, current_ts_);
}

void Strategy::set_target(const std::string& symbol, double target_qty) {
    SymbolId id    = id_of(symbol);
    double   delta = target_qty - portfolio_->position_qty(id);
    if (delta > 1e-9)       broker_->submit_market(id, Side::Buy,  delta,  current_ts_);
    else if (delta < -1e-9) broker_->submit_market(id, Side::Sell, -delta, current_ts_);
}

double Strategy::position(const std::string& symbol) const {
    return portfolio_->position_qty(id_of(symbol));
}

double Strategy::cash() const {
    return portfolio_->cash();
}

} // namespace backtest
