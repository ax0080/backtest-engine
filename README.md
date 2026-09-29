# backtest-engine

[![CI](https://github.com/ax0080/backtest-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/ax0080/backtest-engine/actions/workflows/ci.yml)

A dual-mode backtesting engine in C++20: event-driven and vectorized, sharing the same data, broker and metrics layer.

- **Event-driven mode:** the strategy receives one bar at a time and may submit orders through a simulated broker. Market orders fill at the next bar's open; limit orders fill when the bar's range crosses the limit price. Multiple symbols are merged by timestamp.
- **Vectorized mode:** the strategy receives the full bar series as struct-of-arrays and returns a target position vector (or pass any lambda to `run_fn`). The engine converts position changes to fills at the next bar's open. No virtual calls per bar.
- **Fixed allocation count:** every run sizes its buffers before the bar loop. A test counts `operator new` calls and checks the total is the same for 1,000 and 100,000 bars.
- **AVX2 metric kernel:** Sharpe, Sortino and max drawdown come from one SIMD pass over the equity curve, with a scalar fallback for other CPUs.
- **Indicators:** SMA, EMA, RSI, crossover. All take `std::span<const double>`.
- **Broker:** commission (flat, per-share, percentage) and slippage (fixed, percentage). Fill simulation respects OHLC range.
- **Metrics:** total / annualized return, Sharpe, Sortino, Calmar, max drawdown, win rate, profit factor, per-trade PnL.
- **Data feeds:** CSV loader (configurable columns) and synthetic bar generator (geometric Brownian motion) for benchmarks.
- **No look-ahead bias:** vectorized signals computed at bar _i_ execute at bar _i+1_'s open, identical to event-driven timing.

## Quick start

```bash
git clone https://github.com/ax0080/backtest-engine.git
cd backtest-engine
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build                # unit, kernel and allocation tests
./build/sma_crossover                 # SMA crossover in all three modes
./build/bench_engine
```

Requires CMake 3.20+ and a C++20 compiler. GoogleTest and Google Benchmark are fetched automatically. Release builds use `-march=native`, which enables the AVX2 kernel on CPUs that support it.

```cpp
#include "backtest/data_feed.h"
#include "backtest/engine.h"

using namespace backtest;

EngineConfig cfg;
cfg.initial_cash = 1'000'000;
cfg.commission.per_share = 0.005;

Engine engine(cfg);
engine.add_data("AAPL", CsvBarFeed::load("aapl_daily.csv"));

auto r = engine.run_fn("AAPL", [](const BarSeries& bars) {
    auto fast = sma(bars.close, 10);
    auto slow = sma(bars.close, 30);
    std::vector<double> pos(bars.size(), 0);
    for (std::size_t i = 30; i < bars.size(); ++i)
        pos[i] = fast[i] > slow[i] ? 100 : -100;
    return pos;
});
// r.sharpe_ratio, r.max_drawdown, r.total_trades, r.equity_curve ...
```

Event-driven strategies derive from `Strategy` and override `on_bar`; see [examples/sma_crossover.cpp](examples/sma_crossover.cpp).

## Design

```
Engine
 ├─ data_     SymbolId -> BarSeries (struct-of-arrays: timestamp[], open[], high[], low[], close[], volume[])
 ├─ run()            event-driven: k-way merge by timestamp -> Broker -> Strategy callbacks
 └─ run_signals()    vectorized:   position vector -> fills at next open -> Portfolio

Broker             pending orders -> process_bar() -> fills appended to a reused buffer
Portfolio          positions (average cost) by SymbolId, cash, equity curve, streaming trade stats
Metrics::compute   equity curve -> one-pass kernel (AVX2 or scalar) -> PerformanceReport
```

| Decision | Why |
|---|---|
| Struct-of-arrays `BarSeries` | Vectorized mode iterates one field at a time (e.g. all close prices). Contiguous layout means the CPU prefetcher pulls in useful data, not unused OHLC fields |
| Symbols become integer ids | Orders, fills and positions carry a `uint32_t`, not a `std::string`. Positions and last prices are flat arrays indexed by id instead of hash maps |
| Buffers sized before the loop | The equity curve is reserved for the total bar count, the broker's order list and the fill buffer are reserved and reused, and trade statistics are accumulated instead of stored. A run makes 8 heap allocations (event-driven) or 4 (vectorized) whatever its length |
| k-way merge instead of a sorted timeline | Each series is already in time order, so the event loop walks one cursor per symbol. The previous version copied and sorted a (timestamp, symbol, index) entry per bar |
| One-pass metrics kernel | Return moments are accumulated around a shift (the first return), so mean and variance need one pass. Drawdown ratios are compared by cross-multiplying (a/b < c/d ⇔ a·d < c·b), so the drawdown needs no division |
| Hand-written AVX2 | The compiler will not vectorize a floating-point sum without `-ffast-math`, because reordering additions changes the result. The kernel keeps 4 partial sums per accumulator and computes the running peak with an in-register prefix max. Tests check it against the scalar version and a straightforward reference |
| Signals delayed by one bar | `positions[i]` uses `close[i]`, so executing at `open[i]` would be look-ahead. Filling at `open[i+1]` matches event-driven semantics and avoids the bias |
| Average-cost position tracking | Handles partial closes and position flips correctly. Round-trip PnL is recorded at each close for win-rate / profit-factor metrics |
| Synthetic GBM data for benchmarks | Deterministic (seeded), realistic drift and vol, no external dependency. Same seed on any platform gives the same prices |

## Testing

| Suite | What it checks |
|---|---|
| Engine | PnL of known trades, commission, drawdown, long and short round trips, multi-symbol timestamp merge, unsorted input rejected, indicators |
| Kernels | Scalar and AVX2 results against a two-pass reference: every length from 2 to 40 (all SIMD tail sizes), a 1M-point curve, a zero equity point, a known drawdown |
| Allocations | Replaces global `operator new` and counts calls during event-driven (1 and 4 symbols) and vectorized runs. The count must be equal for 1,000 and 100,000 bars |
| Sanitizers (CI) | The whole suite under ASan and UBSan (this build has no `-march=native`, so it also covers the scalar kernel) |

## Benchmarks

Setup: i5-12600K, GCC 15.2 `-O3 -march=native`, Windows 10, pinned to one P-core, median of 5 runs. SMA(10,30) crossover on 1M synthetic bars. Each run includes loading the bars into a fresh engine.

| Engine | Time | Per bar | Throughput |
|---|---|---|---|
| **Vectorized** | 30 ms | **30 ns** | 33M bars/s |
| **Event-driven** | 46 ms | **46 ns** | 22M bars/s |
| [Backtrader](https://github.com/mementum/backtrader) (Python) | 76.7 s | 76,694 ns | 13k bars/s |

Before the allocation work, the same runs took 58 ns/bar (vectorized) and 138 ns/bar (event-driven). Most of the event-driven gain came from dropping the per-bar sorted timeline and the string-keyed maps.

### vs Backtrader

Same strategy (SMA 10/30 crossover), same workload (1M bars of synthetic GBM data), same machine.

- Event-driven mode: **1,680× faster**
- Vectorized mode: **2,540× faster**

The gap comes from compiled code with no per-bar interpreter work or object creation, flat struct-of-arrays data, and no heap allocation inside the bar loop. The benchmark source is [benchmarks/sma_backtrader.py](benchmarks/sma_backtrader.py).

### Metrics kernel

Sharpe, Sortino and max drawdown over a 1M-point equity curve.

| Implementation | Time | Speedup |
|---|---|---|
| Previous: three passes, two divisions per point | 7.14 ms | 1× |
| One pass, scalar | 3.13 ms | 2.3× |
| **One pass, AVX2** | **0.96 ms** | **7.4×** |

## Layout

```
include/backtest/   types.h  series.h  data_feed.h  strategy.h  broker.h  portfolio.h  engine.h  metrics.h  kernels.h
src/                data_feed.cpp  strategy.cpp  broker.cpp  portfolio.cpp  engine.cpp  metrics.cpp  kernels.cpp
tests/              engine, kernel and allocation tests
benchmarks/         Google Benchmark, Backtrader comparison
examples/           sma_crossover.cpp (event-driven, vectorized, lambda)
```

## Roadmap

- CSV loader auto-detection for Yahoo Finance / Binance formats
- Multi-asset vectorized mode with cross-symbol signals
- Tick-level event-driven mode with integration to [orderbook-engine](https://github.com/ax0080/orderbook-engine)
- Equity curve and drawdown chart export

## License

MIT
