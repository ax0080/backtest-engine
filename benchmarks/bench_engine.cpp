#include <benchmark/benchmark.h>

#include "backtest/data_feed.h"
#include "backtest/engine.h"
#include "backtest/kernels.h"
#include "backtest/strategy.h"

#include <vector>

using namespace backtest;

namespace {

class SmaEvent : public Strategy {
public:
    SmaEvent(int fast, int slow, double size)
        : fast_(fast), slow_(slow), size_(size) { prices_.reserve(slow + 1); }

    void on_bar(const std::string& symbol, const Bar& bar) override {
        prices_.push_back(bar.close);
        if (static_cast<int>(prices_.size()) > slow_)
            prices_.erase(prices_.begin());
        if (static_cast<int>(prices_.size()) < slow_) return;

        double fast_sum = 0;
        for (int i = static_cast<int>(prices_.size()) - fast_;
             i < static_cast<int>(prices_.size()); ++i)
            fast_sum += prices_[i];
        double fast_avg = fast_sum / fast_;

        double slow_sum = 0;
        for (auto p : prices_) slow_sum += p;
        double slow_avg = slow_sum / slow_;

        set_target(symbol, fast_avg > slow_avg ? size_ : -size_);
    }

private:
    int    fast_, slow_;
    double size_;
    std::vector<double> prices_;
};

class SmaVector : public VectorStrategy {
public:
    SmaVector(int fast, int slow, double size)
        : fast_(fast), slow_(slow), size_(size) {}

    std::vector<double> compute(const BarSeries& bars) override {
        auto f = sma(bars.close, fast_);
        auto s = sma(bars.close, slow_);
        std::vector<double> pos(bars.size(), 0);
        for (std::size_t i = static_cast<std::size_t>(slow_); i < bars.size(); ++i)
            pos[i] = f[i] > s[i] ? size_ : -size_;
        return pos;
    }

private:
    int    fast_, slow_;
    double size_;
};

// ---- Benchmarks -----------------------------------------------------------

static void BM_EventDriven(benchmark::State& state) {
    auto bars = SyntheticFeed::generate(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        Engine engine;
        engine.add_data("SYN", bars);
        SmaEvent strategy(10, 30, 100);
        auto r = engine.run(strategy);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_EventDriven)->Arg(10'000)->Arg(100'000)->Arg(1'000'000);

static void BM_Vectorized(benchmark::State& state) {
    auto bars = SyntheticFeed::generate(static_cast<std::size_t>(state.range(0)));

    for (auto _ : state) {
        Engine engine;
        engine.add_data("SYN", bars);
        SmaVector strategy(10, 30, 100);
        auto r = engine.run_vector("SYN", strategy);
        benchmark::DoNotOptimize(r);
    }
    state.SetItemsProcessed(state.iterations() * state.range(0));
}
BENCHMARK(BM_Vectorized)->Arg(10'000)->Arg(100'000)->Arg(1'000'000);

// ---- Metric kernels over a 1M-point equity curve --------------------------

const std::vector<double>& equity_curve() {
    static const std::vector<double> e = [] {
        auto bars = SyntheticFeed::generate(1'000'000);
        return bars.close;
    }();
    return e;
}

// The pre-kernel implementation: three passes, two divisions per point.
kernels::EquityStats equity_stats_legacy(const std::vector<double>& e, double thr) {
    const std::size_t n = e.size() - 1;
    std::vector<double> rets;
    rets.reserve(n);
    for (std::size_t i = 1; i <= n; ++i)
        rets.push_back(e[i - 1] != 0 ? (e[i] - e[i - 1]) / e[i - 1] : 0.0);

    double sum = 0;
    for (double r : rets) sum += r;
    double mean = sum / static_cast<double>(n);

    double var = 0, down = 0;
    for (double r : rets) {
        var += (r - mean) * (r - mean);
        double x = r - thr;
        if (x < 0) down += x * x;
    }

    double peak = e.front(), mdd = 0;
    for (double x : e) {
        if (x > peak) peak = x;
        double dd = (x - peak) / peak;
        if (dd < mdd) mdd = dd;
    }
    return {mean, var / n, down / n, mdd};
}

void BM_EquityStats_Legacy(benchmark::State& state) {
    const auto& e = equity_curve();
    for (auto _ : state) benchmark::DoNotOptimize(equity_stats_legacy(e, 0.0));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * e.size()));
}
BENCHMARK(BM_EquityStats_Legacy)->Unit(benchmark::kMicrosecond);

void BM_EquityStats_Scalar(benchmark::State& state) {
    const auto& e = equity_curve();
    for (auto _ : state) benchmark::DoNotOptimize(kernels::equity_stats_scalar(e, 0.0));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * e.size()));
}
BENCHMARK(BM_EquityStats_Scalar)->Unit(benchmark::kMicrosecond);

#ifdef BACKTEST_HAS_AVX2
void BM_EquityStats_AVX2(benchmark::State& state) {
    const auto& e = equity_curve();
    for (auto _ : state) benchmark::DoNotOptimize(kernels::equity_stats_avx2(e, 0.0));
    state.SetItemsProcessed(static_cast<int64_t>(state.iterations() * e.size()));
}
BENCHMARK(BM_EquityStats_AVX2)->Unit(benchmark::kMicrosecond);
#endif

} // namespace

BENCHMARK_MAIN();
