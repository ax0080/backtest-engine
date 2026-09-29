#include "backtest/data_feed.h"
#include "backtest/engine.h"
#include "backtest/strategy.h"

#include <cstdio>
#include <deque>
#include <numeric>
#include <string>
#include <vector>

using namespace backtest;

// ---------------------------------------------------------------------------
// Event-driven SMA crossover
// ---------------------------------------------------------------------------

class SmaEventDriven : public Strategy {
public:
    SmaEventDriven(int fast, int slow, double size)
        : fast_(fast), slow_(slow), size_(size) {}

    void on_bar(const std::string& symbol, const Bar& bar) override {
        prices_.push_back(bar.close);
        if (static_cast<int>(prices_.size()) > slow_)
            prices_.pop_front();
        if (static_cast<int>(prices_.size()) < slow_) return;

        auto avg = [](auto begin, auto end) {
            return std::accumulate(begin, end, 0.0) /
                   std::distance(begin, end);
        };

        double fast_sma = avg(prices_.end() - fast_, prices_.end());
        double slow_sma = avg(prices_.begin(), prices_.end());

        if (fast_sma > slow_sma)
            set_target(symbol, size_);
        else
            set_target(symbol, -size_);
    }

private:
    int    fast_, slow_;
    double size_;
    std::deque<double> prices_;
};

// ---------------------------------------------------------------------------
// Vectorized SMA crossover
// ---------------------------------------------------------------------------

class SmaVectorized : public VectorStrategy {
public:
    SmaVectorized(int fast, int slow, double size)
        : fast_(fast), slow_(slow), size_(size) {}

    std::vector<double> compute(const BarSeries& bars) override {
        auto fast = sma(bars.close, fast_);
        auto slow = sma(bars.close, slow_);

        std::vector<double> positions(bars.size(), 0.0);
        for (std::size_t i = static_cast<std::size_t>(slow_); i < bars.size(); ++i) {
            positions[i] = fast[i] > slow[i] ? size_ : -size_;
        }
        return positions;
    }

private:
    int    fast_, slow_;
    double size_;
};

// ---------------------------------------------------------------------------

static void print_report(const char* label, const PerformanceReport& r) {
    std::printf("\n=== %s ===\n", label);
    std::printf("  Total Return:      %+.2f%%\n", r.total_return * 100);
    std::printf("  Annualized Return: %+.2f%%\n", r.annualized_return * 100);
    std::printf("  Sharpe Ratio:      %.2f\n",    r.sharpe_ratio);
    std::printf("  Sortino Ratio:     %.2f\n",    r.sortino_ratio);
    std::printf("  Max Drawdown:      %.2f%%\n",  r.max_drawdown * 100);
    std::printf("  Calmar Ratio:      %.2f\n",    r.calmar_ratio);
    std::printf("  Win Rate:          %.1f%%\n",  r.win_rate * 100);
    std::printf("  Profit Factor:     %.2f\n",    r.profit_factor);
    std::printf("  Total Trades:      %d\n",      r.total_trades);
    std::printf("  Avg Trade PnL:     $%.2f\n",   r.avg_trade_pnl);
}

int main() {
    auto bars = SyntheticFeed::generate(5000, 100.0, 42);
    std::printf("Generated %zu bars (%.0f days / ~%.1f years)\n",
                bars.size(),
                static_cast<double>(bars.size()),
                bars.size() / 252.0);

    EngineConfig cfg;
    cfg.initial_cash = 1'000'000;
    cfg.commission.per_share = 0.005;   // $0.005 per share
    cfg.slippage.pct = 0.0001;          // 1 bp slippage

    // Event-driven
    {
        Engine engine(cfg);
        engine.add_data("SYN", bars);
        SmaEventDriven strategy(10, 30, 100);
        auto report = engine.run(strategy);
        print_report("Event-Driven SMA(10,30)", report);
    }

    // Vectorized
    {
        Engine engine(cfg);
        engine.add_data("SYN", bars);
        SmaVectorized strategy(10, 30, 100);
        auto report = engine.run_vector("SYN", strategy);
        print_report("Vectorized SMA(10,30)", report);
    }

    return 0;
}
