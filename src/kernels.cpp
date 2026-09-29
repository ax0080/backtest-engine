#include "backtest/kernels.h"

#include <algorithm>
#include <cstddef>

#ifdef BACKTEST_HAS_AVX2
#include <immintrin.h>
#endif

namespace backtest::kernels {

// Both versions make a single pass and do one division per point:
//  - Return moments are accumulated around a shift K (the first return), so
//    mean and variance come from one pass without the cancellation of a raw
//    sum-of-squares.
//  - Drawdown (x - peak) / peak is minimized by cross-multiplying,
//    a/b < c/d  <=>  a*d < c*b  for b, d > 0, so it needs no division.
//    Peaks are positive because they start at equity[0] > 0.

namespace {

struct Acc {
    double s1 = 0, s2 = 0, down = 0;   // sum(r-K), sum((r-K)^2), sum(min(r-thr,0)^2)
    double peak = 0;
    double dd_num = 0, dd_den = 1;     // most negative (x - peak) / peak so far
};

inline void step(Acc& a, double prev, double cur, double k, double thr) {
    double r = prev != 0 ? cur / prev - 1.0 : 0.0;
    double d = r - k;
    a.s1 += d;
    a.s2 += d * d;
    double x = std::min(r - thr, 0.0);
    a.down += x * x;

    a.peak     = std::max(a.peak, cur);
    double num = cur - a.peak;
    if (num * a.dd_den < a.dd_num * a.peak) { a.dd_num = num; a.dd_den = a.peak; }
}

inline double first_return(std::span<const double> e) {
    return e[0] != 0 ? e[1] / e[0] - 1.0 : 0.0;
}

EquityStats finish(const Acc& a, double k, std::size_t n) {
    const double inv_n = 1.0 / static_cast<double>(n);
    const double m1    = a.s1 * inv_n;
    EquityStats s;
    s.mean_return       = k + m1;
    s.return_variance   = std::max(a.s2 * inv_n - m1 * m1, 0.0);
    s.downside_variance = a.down * inv_n;
    s.max_drawdown      = a.dd_num / a.dd_den;
    return s;
}

} // namespace

EquityStats equity_stats_scalar(std::span<const double> e, double thr) {
    const std::size_t n = e.size() - 1;
    const double      k = first_return(e);
    Acc a;
    a.peak = e[0];
    for (std::size_t i = 1; i <= n; ++i) step(a, e[i - 1], e[i], k, thr);
    return finish(a, k, n);
}

#ifdef BACKTEST_HAS_AVX2

namespace {

inline double hsum(__m256d v) {
    __m128d lo = _mm256_castpd256_pd128(v);
    __m128d hi = _mm256_extractf128_pd(v, 1);
    lo = _mm_add_pd(lo, hi);
    return _mm_cvtsd_f64(_mm_add_sd(lo, _mm_unpackhi_pd(lo, lo)));
}

inline __m256d fmadd(__m256d a, __m256d b, __m256d c) {
#ifdef __FMA__
    return _mm256_fmadd_pd(a, b, c);
#else
    return _mm256_add_pd(_mm256_mul_pd(a, b), c);
#endif
}

} // namespace

EquityStats equity_stats_avx2(std::span<const double> eq, double thr) {
    const std::size_t n = eq.size() - 1;
    const double*     e = eq.data();
    const double      k = first_return(eq);

    const __m256d vk   = _mm256_set1_pd(k);
    const __m256d vthr = _mm256_set1_pd(thr);
    const __m256d one  = _mm256_set1_pd(1.0);
    const __m256d zero = _mm256_setzero_pd();

    __m256d s1 = zero, s2 = zero, down = zero;
    __m256d carry  = _mm256_set1_pd(e[0]);   // running peak, broadcast
    __m256d dd_num = zero, dd_den = one;     // per-lane best drawdown

    std::size_t i = 1;
    for (; i + 3 <= n; i += 4) {
        const __m256d prev = _mm256_loadu_pd(e + i - 1);
        const __m256d cur  = _mm256_loadu_pd(e + i);

        // Returns; lanes where prev == 0 are forced to 0.
        __m256d r = _mm256_sub_pd(_mm256_div_pd(cur, prev), one);
        r = _mm256_and_pd(r, _mm256_cmp_pd(prev, zero, _CMP_NEQ_OQ));

        const __m256d d = _mm256_sub_pd(r, vk);
        s1 = _mm256_add_pd(s1, d);
        s2 = fmadd(d, d, s2);
        const __m256d x = _mm256_min_pd(_mm256_sub_pd(r, vthr), zero);
        down = fmadd(x, x, down);

        // In-register prefix max: [c0, c1, c2, c3] -> [c0, max(c0..c1), ...].
        __m256d t = _mm256_max_pd(cur, _mm256_permute4x64_pd(cur, _MM_SHUFFLE(2, 1, 0, 0)));
        t = _mm256_max_pd(t, _mm256_permute4x64_pd(t, _MM_SHUFFLE(1, 0, 0, 0)));
        const __m256d peak = _mm256_max_pd(t, carry);
        carry = _mm256_permute4x64_pd(peak, _MM_SHUFFLE(3, 3, 3, 3));

        const __m256d num    = _mm256_sub_pd(cur, peak);
        const __m256d better = _mm256_cmp_pd(_mm256_mul_pd(num, dd_den),
                                             _mm256_mul_pd(dd_num, peak), _CMP_LT_OQ);
        dd_num = _mm256_blendv_pd(dd_num, num, better);
        dd_den = _mm256_blendv_pd(dd_den, peak, better);
    }

    Acc a;
    a.s1   = hsum(s1);
    a.s2   = hsum(s2);
    a.down = hsum(down);
    a.peak = _mm256_cvtsd_f64(carry);

    alignas(32) double nums[4], dens[4];
    _mm256_store_pd(nums, dd_num);
    _mm256_store_pd(dens, dd_den);
    for (int l = 0; l < 4; ++l)
        if (nums[l] * a.dd_den < a.dd_num * dens[l]) { a.dd_num = nums[l]; a.dd_den = dens[l]; }

    for (; i <= n; ++i) step(a, e[i - 1], e[i], k, thr);
    return finish(a, k, n);
}

#endif // BACKTEST_HAS_AVX2

} // namespace backtest::kernels
