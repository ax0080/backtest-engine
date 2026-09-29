#include <gtest/gtest.h>

#include "backtest/data_feed.h"
#include "backtest/engine.h"
#include "backtest/strategy.h"

using namespace backtest;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static BarSeries make_bars(const std::vector<double>& closes) {
    BarSeries bars;
    Timestamp ts = 946684800000;
    for (double c : closes) {
        bars.push_back({ts, c, c * 1.01, c * 0.99, c, 1000});
        ts += 86400000;
    }
    return bars;
}

class BuyAndHold : public Strategy {
    double size_;
    bool   entered_ = false;
public:
    explicit BuyAndHold(double size) : size_(size) {}
    void on_bar(const std::string& symbol, const Bar&) override {
        if (!entered_) { buy(symbol, size_); entered_ = true; }
    }
};

class AlwaysFlat : public VectorStrategy {
public:
    std::vector<double> compute(const BarSeries& bars) override {
        return std::vector<double>(bars.size(), 0.0);
    }
};

class LongOnly : public VectorStrategy {
    double size_;
public:
    explicit LongOnly(double s) : size_(s) {}
    std::vector<double> compute(const BarSeries& bars) override {
        // Signal goes long from bar 0 onward.  Because of the 1-bar execution
        // delay, the buy fills at bar 1's open.
        std::vector<double> pos(bars.size(), size_);
        return pos;
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST(EngineTest, BuyAndHoldPnL) {
    auto bars = make_bars({100, 100, 110, 110, 120});
    Engine engine(EngineConfig{1'000'000, {}, {}});
    engine.add_data("X", bars);

    BuyAndHold strategy(100);
    auto r = engine.run(strategy);

    EXPECT_EQ(r.equity_curve.size(), 5u);
    // Bought 100 shares at 100 on bar 1 (open of bar 1 = 100).
    // Final equity = cash (1M - 100*100) + 100 * 120 = 1,002,000
    EXPECT_NEAR(r.equity_curve.back(), 1'002'000, 1);
}

TEST(EngineTest, FlatStrategyNoTrades) {
    auto bars = make_bars({100, 110, 120, 130});
    Engine engine(EngineConfig{1'000'000, {}, {}});
    engine.add_data("X", bars);

    AlwaysFlat strategy;
    auto r = engine.run_vector("X", strategy);

    EXPECT_EQ(r.total_trades, 0);
    EXPECT_NEAR(r.total_return, 0.0, 1e-9);
}

TEST(EngineTest, VectorizedLongPnL) {
    auto bars = make_bars({100, 100, 110, 120, 130});
    Engine engine(EngineConfig{1'000'000, {}, {}});
    engine.add_data("X", bars);

    LongOnly strategy(100);
    auto r = engine.run_vector("X", strategy);

    // Signal[0] = 100 → fill at bar 1 open = 100.  Holds to end, close = 130.
    // equity = (1M - 100*100) + 100*130 = 1,003,000
    EXPECT_NEAR(r.equity_curve.back(), 1'003'000, 1);
}

TEST(EngineTest, CommissionReducesReturn) {
    auto bars = make_bars({100, 100, 110, 120, 130});

    EngineConfig cfg_no_comm;
    cfg_no_comm.initial_cash = 1'000'000;

    EngineConfig cfg_comm;
    cfg_comm.initial_cash = 1'000'000;
    cfg_comm.commission.per_share = 0.01;

    Engine e1(cfg_no_comm); e1.add_data("X", bars);
    Engine e2(cfg_comm);    e2.add_data("X", bars);

    LongOnly s1(100), s2(100);
    auto r1 = e1.run_vector("X", s1);
    auto r2 = e2.run_vector("X", s2);

    EXPECT_LT(r2.equity_curve.back(), r1.equity_curve.back());
}

TEST(EngineTest, MaxDrawdownNegative) {
    // Price rises then drops: should produce a drawdown.
    auto bars = make_bars({100, 100, 120, 110, 105, 115});
    Engine engine(EngineConfig{1'000'000, {}, {}});
    engine.add_data("X", bars);

    LongOnly strategy(100);
    auto r = engine.run_vector("X", strategy);

    EXPECT_LT(r.max_drawdown, 0);
}

TEST(EngineTest, SyntheticFeedDeterministic) {
    auto a = SyntheticFeed::generate(1000, 100, 123);
    auto b = SyntheticFeed::generate(1000, 100, 123);
    ASSERT_EQ(a.size(), b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        EXPECT_DOUBLE_EQ(a.close[i], b.close[i]);
    }
}

TEST(EngineTest, SyntheticFeedSize) {
    auto bars = SyntheticFeed::generate(500);
    EXPECT_EQ(bars.size(), 500u);
}

TEST(SeriesTest, SmaBasic) {
    std::vector<double> data = {1, 2, 3, 4, 5};
    auto result = sma(data, 3);
    EXPECT_DOUBLE_EQ(result[2], 2.0);   // (1+2+3)/3
    EXPECT_DOUBLE_EQ(result[3], 3.0);   // (2+3+4)/3
    EXPECT_DOUBLE_EQ(result[4], 4.0);   // (3+4+5)/3
}

TEST(SeriesTest, EmaBasic) {
    std::vector<double> data = {10, 10, 10};
    auto result = ema(data, 3);
    for (auto v : result) EXPECT_NEAR(v, 10.0, 1e-9);
}

TEST(SeriesTest, CrossoverDetection) {
    std::vector<double> a = {1, 3, 2, 4};
    std::vector<double> b = {2, 2, 3, 3};
    auto result = crossover(a, b);
    EXPECT_EQ(result[1],  1);   // a crosses above b
    EXPECT_EQ(result[2], -1);   // a crosses below b
    EXPECT_EQ(result[3],  1);   // a crosses above b again
}

TEST(PortfolioTest, RoundTripPnL) {
    auto bars = make_bars({100, 100, 120, 120, 120});

    EngineConfig cfg;
    cfg.initial_cash = 100'000;

    // Signal[0]=50 → buy fills at bar 1 open (100).
    // Signal[2]=0  → sell fills at bar 3 open (120).
    std::vector<double> positions = {50, 50, 0, 0, 0};

    Engine engine(cfg);
    engine.add_data("X", bars);
    auto r = engine.run_signals("X", positions);

    EXPECT_EQ(r.total_trades, 1);
    EXPECT_GT(r.total_return, 0);
}

TEST(PortfolioTest, ShortRoundTrip) {
    auto bars = make_bars({100, 100, 80, 80, 80});

    // Signal[0]=-50 → short fills at bar 1 open (100).
    // Signal[2]=0   → cover fills at bar 3 open (80).
    std::vector<double> positions = {-50, -50, 0, 0, 0};

    Engine engine(EngineConfig{100'000, {}, {}});
    engine.add_data("X", bars);
    auto r = engine.run_signals("X", positions);

    EXPECT_EQ(r.total_trades, 1);
    EXPECT_GT(r.total_return, 0);
}

TEST(MetricsTest, SharpeSign) {
    // Strictly rising equity -> positive Sharpe.
    auto bars = make_bars({100, 101, 102, 103, 104, 105, 106, 107, 108, 109, 110});
    Engine engine(EngineConfig{1'000'000, {}, {}});
    engine.add_data("X", bars);

    LongOnly strategy(100);
    auto r = engine.run_vector("X", strategy);
    EXPECT_GT(r.sharpe_ratio, 0);
}

// Two symbols with partly overlapping timestamps: one snapshot per distinct
// timestamp, and each symbol's orders fill only on that symbol's bars.
TEST(EngineTest, MultiSymbolMergedTimeline) {
    BarSeries a, b;
    const Timestamp day = 86400000;
    for (int i = 0; i < 4; ++i) a.push_back({i * day, 100, 101, 99, 100, 1000});   // days 0-3
    for (int i = 2; i < 6; ++i) b.push_back({i * day, 50, 51, 49, 60, 1000});      // days 2-5

    class BuyBothOnce : public Strategy {
        bool done_a_ = false, done_b_ = false;
    public:
        std::vector<std::string> filled;
        void on_bar(const std::string& symbol, const Bar&) override {
            if (symbol == "A" && !done_a_) { buy(symbol, 10); done_a_ = true; }
            if (symbol == "B" && !done_b_) { buy(symbol, 10); done_b_ = true; }
        }
        void on_fill(const Fill& f) override { filled.push_back(symbol_name(f.symbol)); }
    } strategy;

    Engine engine(EngineConfig{100'000, {}, {}});
    engine.add_data("A", a);
    engine.add_data("B", b);
    auto r = engine.run(strategy);

    EXPECT_EQ(r.equity_curve.size(), 6u);
    EXPECT_EQ(strategy.filled, (std::vector<std::string>{"A", "B"}));
    // A: bought 10 @ 100, marked at 100. B: bought 10 @ 50, marked at 60.
    EXPECT_NEAR(r.equity_curve.back(), 100'000 + 10 * (60 - 50), 1e-9);
}

TEST(EngineTest, RejectsUnsortedBars) {
    BarSeries bars;
    bars.push_back({2, 1, 1, 1, 1, 1});
    bars.push_back({1, 1, 1, 1, 1, 1});
    Engine engine;
    EXPECT_THROW(engine.add_data("X", bars), std::invalid_argument);
}

TEST(MetricsTest, WinRateRange) {
    auto bars = SyntheticFeed::generate(2000, 100, 99);
    Engine engine(EngineConfig{1'000'000, {}, {}});
    engine.add_data("X", bars);

    // SMA crossover generates many trades.
    class SmaCross : public VectorStrategy {
        std::vector<double> compute(const BarSeries& bars) override {
            auto f = sma(bars.close, 5);
            auto s = sma(bars.close, 20);
            std::vector<double> pos(bars.size(), 0);
            for (std::size_t i = 20; i < bars.size(); ++i)
                pos[i] = f[i] > s[i] ? 100 : -100;
            return pos;
        }
    } strategy;
    auto r = engine.run_vector("X", strategy);

    EXPECT_GT(r.total_trades, 10);
    EXPECT_GE(r.win_rate, 0.0);
    EXPECT_LE(r.win_rate, 1.0);
}
