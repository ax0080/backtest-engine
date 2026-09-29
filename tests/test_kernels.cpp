#include <gtest/gtest.h>

#include "backtest/kernels.h"

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

using namespace backtest::kernels;

namespace {

// Straightforward two-pass version with a division per drawdown point.
EquityStats reference(const std::vector<double>& e, double thr) {
    const std::size_t n = e.size() - 1;
    auto ret = [&](std::size_t i) { return e[i - 1] != 0 ? e[i] / e[i - 1] - 1.0 : 0.0; };

    double sum = 0;
    for (std::size_t i = 1; i <= n; ++i) sum += ret(i);
    const double mean = sum / n;

    double var = 0, down = 0;
    for (std::size_t i = 1; i <= n; ++i) {
        double r = ret(i);
        var += (r - mean) * (r - mean);
        double x = std::min(r - thr, 0.0);
        down += x * x;
    }

    double peak = e[0], mdd = 0;
    for (double x : e) {
        peak = std::max(peak, x);
        mdd  = std::min(mdd, (x - peak) / peak);
    }
    return {mean, var / n, down / n, mdd};
}

std::vector<double> random_equity(std::size_t n, uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> z(0.0003, 0.01);
    std::vector<double> e(n);
    e[0] = 1'000'000;
    for (std::size_t i = 1; i < n; ++i) e[i] = e[i - 1] * (1.0 + z(rng));
    return e;
}

void expect_close(const EquityStats& a, const EquityStats& b, double rel) {
    auto near = [rel](double x, double y) {
        return std::abs(x - y) <= rel * std::max(std::abs(y), 1e-300) + 1e-18;
    };
    EXPECT_TRUE(near(a.mean_return, b.mean_return))             << a.mean_return << " vs " << b.mean_return;
    EXPECT_TRUE(near(a.return_variance, b.return_variance))     << a.return_variance << " vs " << b.return_variance;
    EXPECT_TRUE(near(a.downside_variance, b.downside_variance)) << a.downside_variance << " vs " << b.downside_variance;
    EXPECT_TRUE(near(a.max_drawdown, b.max_drawdown))           << a.max_drawdown << " vs " << b.max_drawdown;
}

} // namespace

TEST(KernelsTest, KnownDrawdown) {
    std::vector<double> e = {100, 120, 90, 110, 130, 104};
    auto s = equity_stats_scalar(e, 0.0);
    EXPECT_DOUBLE_EQ(s.max_drawdown, -0.25);   // 120 -> 90
    expect_close(equity_stats(e, 0.0), reference(e, 0.0), 1e-12);
}

TEST(KernelsTest, ZeroEquityPoint) {
    std::vector<double> e = {100, 50, 0, 10, 20, 40, 80, 60, 30};
    auto s = equity_stats(e, 0.0);
    EXPECT_DOUBLE_EQ(s.max_drawdown, -1.0);
    expect_close(s, reference(e, 0.0), 1e-12);
}

// Every length from 2 to 40 so the SIMD path hits each tail size.
TEST(KernelsTest, MatchesReferenceAllShortLengths) {
    for (std::size_t n = 2; n <= 40; ++n) {
        auto e = random_equity(n, static_cast<uint32_t>(n));
        SCOPED_TRACE(n);
        expect_close(equity_stats_scalar(e, 0.0001), reference(e, 0.0001), 1e-9);
        expect_close(equity_stats(e, 0.0001),        reference(e, 0.0001), 1e-9);
    }
}

TEST(KernelsTest, MatchesReferenceLongCurve) {
    auto e = random_equity(1'000'003, 11);
    auto ref = reference(e, 0.0);
    expect_close(equity_stats_scalar(e, 0.0), ref, 1e-9);
    expect_close(equity_stats(e, 0.0), ref, 1e-9);
}

TEST(KernelsTest, Avx2MatchesScalar) {
#ifdef BACKTEST_HAS_AVX2
    for (uint32_t seed = 0; seed < 20; ++seed) {
        auto e = random_equity(10'000 + seed, seed);
        expect_close(equity_stats_avx2(e, 0.0002), equity_stats_scalar(e, 0.0002), 1e-10);
    }
#else
    GTEST_SKIP() << "built without AVX2";
#endif
}
