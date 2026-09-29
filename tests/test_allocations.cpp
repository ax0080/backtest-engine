// Counts global operator new calls made while a backtest runs. The engine
// sizes its buffers before the bar loop, so the count must not depend on how
// many bars are processed.

#include <gtest/gtest.h>

#include "backtest/data_feed.h"
#include "backtest/engine.h"
#include "backtest/strategy.h"

#include <array>
#include <cstdio>
#include <cstdlib>
#include <new>

namespace {
bool        g_counting = false;
std::size_t g_allocs   = 0;
} // namespace

void* operator new(std::size_t n) {
    if (g_counting) ++g_allocs;
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) { return ::operator new(n); }
void* operator new(std::size_t n, const std::nothrow_t&) noexcept {
    if (g_counting) ++g_allocs;
    return std::malloc(n ? n : 1);
}
void* operator new[](std::size_t n, const std::nothrow_t& t) noexcept { return ::operator new(n, t); }
void  operator delete(void* p) noexcept { std::free(p); }
void  operator delete[](void* p) noexcept { std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void  operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void  operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

using namespace backtest;

namespace {

template <typename F>
std::size_t count_allocs(F&& f) {
    g_allocs   = 0;
    g_counting = true;
    f();
    g_counting = false;
    return g_allocs;
}

// SMA crossover over a fixed ring buffer, so the strategy itself never
// allocates and every counted allocation belongs to the engine.
class RingSmaCross : public Strategy {
    static constexpr int kFast = 10, kSlow = 30;
    std::array<double, kSlow> ring_{};
    int    n_ = 0;
    double fast_sum_ = 0, slow_sum_ = 0;

public:
    void on_bar(const std::string& symbol, const Bar& bar) override {
        slow_sum_ += bar.close - (n_ >= kSlow ? ring_[n_ % kSlow] : 0.0);
        fast_sum_ += bar.close - (n_ >= kFast ? ring_[(n_ - kFast) % kSlow] : 0.0);
        ring_[n_ % kSlow] = bar.close;
        if (++n_ < kSlow) return;
        set_target(symbol, fast_sum_ / kFast > slow_sum_ / kSlow ? 100 : -100);
    }
};

EngineConfig config() {
    EngineConfig cfg;
    cfg.commission.per_share = 0.005;
    cfg.slippage.pct         = 0.0001;
    return cfg;
}

std::size_t event_driven_allocs(std::size_t bars, int symbols) {
    Engine engine(config());
    for (int s = 0; s < symbols; ++s)
        engine.add_data("S" + std::to_string(s),
                        SyntheticFeed::generate(bars, 100.0, 42 + s));
    RingSmaCross strategy;
    PerformanceReport r;
    std::size_t n = count_allocs([&] { r = engine.run(strategy); });
    EXPECT_GT(r.total_trades, 0);
    return n;
}

std::size_t vectorized_allocs(std::size_t bars) {
    Engine engine(config());
    auto series = SyntheticFeed::generate(bars);
    auto fast = sma(series.close, 10);
    auto slow = sma(series.close, 30);
    std::vector<double> pos(bars, 0.0);
    for (std::size_t i = 30; i < bars; ++i) pos[i] = fast[i] > slow[i] ? 100 : -100;
    engine.add_data("SYN", std::move(series));

    PerformanceReport r;
    std::size_t n = count_allocs([&] { r = engine.run_signals("SYN", pos); });
    EXPECT_GT(r.total_trades, 0);
    return n;
}

} // namespace

TEST(AllocationTest, EventDrivenCountIndependentOfBars) {
    std::size_t small = event_driven_allocs(1'000, 1);
    std::size_t large = event_driven_allocs(100'000, 1);
    EXPECT_EQ(small, large);
    EXPECT_LE(large, 10u);
    std::printf("  heap allocations per run: %zu\n", large);
}

TEST(AllocationTest, EventDrivenMultiSymbolCountIndependentOfBars) {
    std::size_t small = event_driven_allocs(1'000, 4);
    std::size_t large = event_driven_allocs(100'000, 4);
    EXPECT_EQ(small, large);
    EXPECT_LE(large, 10u);
    std::printf("  heap allocations per run: %zu\n", large);
}

TEST(AllocationTest, VectorizedCountIndependentOfBars) {
    std::size_t small = vectorized_allocs(1'000);
    std::size_t large = vectorized_allocs(100'000);
    EXPECT_EQ(small, large);
    EXPECT_LE(large, 10u);
    std::printf("  heap allocations per run: %zu\n", large);
}
