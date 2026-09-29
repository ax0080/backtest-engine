#include "backtest/strategy.h"

#include "backtest/broker.h"
#include "backtest/portfolio.h"

#include <cmath>

namespace backtest {

void Strategy::buy(const std::string& symbol, double qty) {
    broker_->submit_market(symbol, Side::Buy, qty, current_ts_);
}

void Strategy::sell(const std::string& symbol, double qty) {
    broker_->submit_market(symbol, Side::Sell, qty, current_ts_);
}

void Strategy::set_target(const std::string& symbol, double target_qty) {
    double current = position(symbol);
    double delta   = target_qty - current;
    if (delta > 1e-9)       buy(symbol, delta);
    else if (delta < -1e-9) sell(symbol, -delta);
}

double Strategy::position(const std::string& symbol) const {
    return portfolio_->position_qty(symbol);
}

double Strategy::cash() const {
    return portfolio_->cash();
}

} // namespace backtest
