#pragma once

#include "types.h"

#include <cmath>
#include <string>
#include <vector>

namespace backtest {

class Portfolio;

struct CommissionModel {
    double per_trade = 0;
    double per_share = 0;
    double pct       = 0;
    double minimum   = 0;

    double compute(double price, double qty) const {
        double c = per_trade + per_share * std::abs(qty) + pct * price * std::abs(qty);
        return std::max(c, minimum);
    }
};

struct SlippageModel {
    double fixed = 0;
    double pct   = 0;

    double compute(double price, Side side) const {
        double s = fixed + pct * price;
        return side == Side::Buy ? s : -s;
    }
};

class Broker {
public:
    Broker(Portfolio& portfolio,
           CommissionModel commission = {},
           SlippageModel slippage = {});

    uint64_t submit_market(const std::string& symbol, Side side,
                           double qty, Timestamp ts);
    uint64_t submit_limit(const std::string& symbol, Side side,
                          double qty, double price, Timestamp ts);

    std::vector<Fill> process_bar(const std::string& symbol, const Bar& bar);

    void cancel_all();

private:
    Portfolio&      portfolio_;
    CommissionModel commission_;
    SlippageModel   slippage_;
    std::vector<Order> pending_;
    uint64_t        next_id_ = 1;
};

} // namespace backtest
