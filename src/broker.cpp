#include "backtest/broker.h"

#include "backtest/portfolio.h"

#include <algorithm>

namespace backtest {

Broker::Broker(Portfolio& portfolio, CommissionModel commission,
               SlippageModel slippage)
    : portfolio_(portfolio), commission_(commission), slippage_(slippage) {}

uint64_t Broker::submit_market(const std::string& symbol, Side side,
                               double qty, Timestamp ts) {
    uint64_t id = next_id_++;
    pending_.push_back({id, symbol, side, OrderType::Market, qty, 0.0, ts});
    return id;
}

uint64_t Broker::submit_limit(const std::string& symbol, Side side,
                              double qty, double price, Timestamp ts) {
    uint64_t id = next_id_++;
    pending_.push_back({id, symbol, side, OrderType::Limit, qty, price, ts});
    return id;
}

std::vector<Fill> Broker::process_bar(const std::string& symbol,
                                      const Bar& bar) {
    std::vector<Fill> fills;

    auto it = pending_.begin();
    while (it != pending_.end()) {
        if (it->symbol != symbol) { ++it; continue; }

        bool   filled    = false;
        double fill_price = 0;

        if (it->type == OrderType::Market) {
            fill_price = bar.open + slippage_.compute(bar.open, it->side);
            filled = true;
        } else {
            if (it->side == Side::Buy && bar.low <= it->limit_price) {
                fill_price = it->limit_price;
                filled = true;
            } else if (it->side == Side::Sell && bar.high >= it->limit_price) {
                fill_price = it->limit_price;
                filled = true;
            }
        }

        if (filled) {
            Fill f;
            f.order_id   = it->id;
            f.symbol     = it->symbol;
            f.side       = it->side;
            f.price      = fill_price;
            f.quantity   = it->quantity;
            f.commission = commission_.compute(fill_price, it->quantity);
            f.timestamp  = bar.timestamp;
            portfolio_.apply_fill(f);
            fills.push_back(f);
            it = pending_.erase(it);
        } else {
            ++it;
        }
    }
    return fills;
}

void Broker::cancel_all() { pending_.clear(); }

} // namespace backtest
