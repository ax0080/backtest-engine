#include "backtest/broker.h"

#include "backtest/portfolio.h"

namespace backtest {

Broker::Broker(Portfolio& portfolio, CommissionModel commission,
               SlippageModel slippage)
    : portfolio_(portfolio), commission_(commission), slippage_(slippage) {
    pending_.reserve(kReservedOrders);
}

uint64_t Broker::submit_market(SymbolId symbol, Side side, double qty,
                               Timestamp ts) {
    uint64_t id = next_id_++;
    pending_.push_back({id, symbol, side, OrderType::Market, qty, 0.0, ts});
    return id;
}

uint64_t Broker::submit_limit(SymbolId symbol, Side side, double qty,
                              double price, Timestamp ts) {
    uint64_t id = next_id_++;
    pending_.push_back({id, symbol, side, OrderType::Limit, qty, price, ts});
    return id;
}

void Broker::process_bar(SymbolId symbol, const Bar& bar, std::vector<Fill>& out) {
    // Stable in-place compaction: unfilled orders keep their relative order.
    std::size_t keep = 0;
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        const Order& o = pending_[i];

        bool   filled     = false;
        double fill_price = 0;
        if (o.symbol == symbol) {
            if (o.type == OrderType::Market) {
                fill_price = bar.open + slippage_.compute(bar.open, o.side);
                filled     = true;
            } else if (o.side == Side::Buy ? bar.low  <= o.limit_price
                                           : bar.high >= o.limit_price) {
                fill_price = o.limit_price;
                filled     = true;
            }
        }

        if (!filled) {
            pending_[keep++] = o;
            continue;
        }

        Fill f;
        f.order_id   = o.id;
        f.symbol     = o.symbol;
        f.side       = o.side;
        f.price      = fill_price;
        f.quantity   = o.quantity;
        f.commission = commission_.compute(fill_price, o.quantity);
        f.timestamp  = bar.timestamp;
        portfolio_.apply_fill(f);
        out.push_back(f);
    }
    pending_.resize(keep);
}

void Broker::cancel_all() { pending_.clear(); }

} // namespace backtest
