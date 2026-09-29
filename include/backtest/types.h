#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace backtest {

using Timestamp = int64_t;  // milliseconds since epoch
using SymbolId  = uint32_t; // index in the order symbols were added to the Engine

enum class Side { Buy, Sell };

struct Bar {
    Timestamp timestamp = 0;
    double open   = 0;
    double high   = 0;
    double low    = 0;
    double close  = 0;
    double volume = 0;
};

enum class OrderType { Market, Limit };

struct Order {
    uint64_t  id = 0;
    SymbolId  symbol = 0;
    Side      side = Side::Buy;
    OrderType type = OrderType::Market;
    double    quantity    = 0;
    double    limit_price = 0;
    Timestamp timestamp   = 0;
};

struct Fill {
    uint64_t  order_id = 0;
    SymbolId  symbol = 0;
    Side      side = Side::Buy;
    double    price      = 0;
    double    quantity   = 0;
    double    commission = 0;
    Timestamp timestamp  = 0;
};

struct Position {
    double    quantity     = 0;
    double    avg_price    = 0;
    double    realized_pnl = 0;
    Timestamp entry_time   = 0;

    double unrealized_pnl(double current_price) const {
        return quantity * (current_price - avg_price);
    }
};

// Round-trip statistics, accumulated as positions are reduced or closed.
struct TradeStats {
    int    count        = 0;
    int    wins         = 0;
    double gross_profit = 0;
    double gross_loss   = 0;   // positive
    double total_pnl    = 0;

    void record(double pnl) {
        ++count;
        total_pnl += pnl;
        if (pnl > 0) { ++wins; gross_profit += pnl; }
        else         { gross_loss -= pnl; }
    }
};

struct PerformanceReport {
    double total_return      = 0;
    double annualized_return = 0;
    double sharpe_ratio      = 0;
    double sortino_ratio     = 0;
    double max_drawdown      = 0;
    double win_rate          = 0;
    double profit_factor     = 0;
    double calmar_ratio      = 0;
    int    total_trades      = 0;
    double avg_trade_pnl     = 0;

    std::vector<double>    equity_curve;
    std::vector<Timestamp> equity_timestamps;
};

} // namespace backtest
