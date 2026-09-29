# backtest-engine

[![CI](https://github.com/ax0080/backtest-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/ax0080/backtest-engine/actions/workflows/ci.yml)

A dual-mode backtesting engine in C++20: event-driven and vectorized, sharing the same data, broker and metrics layer.

- **Event-driven mode:** the strategy receives one bar at a time and may submit orders through a simulated broker. Market orders fill at the next bar's open; limit orders fill when the bar's range crosses the limit price.
- **Vectorized mode:** the strategy receives the full bar series as struct-of-arrays and returns a target position vector. The engine converts position changes to fills at the next bar's open. No virtual calls per bar.
- **Indicators:** SMA, EMA, RSI, crossover — all operate on contiguous `double` vectors.
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
ctest --test-dir build                # unit tests
./build/sma_crossover                 # SMA crossover in both modes
./build/bench_engine
```

Requires CMake 3.20+ and a C++20 compiler. GoogleTest and Google Benchmark are fetched automatically.

```cpp
#include "backtest/data_feed.h"
#include "backtest/engine.h"
#include "backtest/strategy.h"

using namespace backtest;

class SmaCross : public VectorStrategy {
    std::vector<double> compute(const BarSeries& bars) override {
        auto fast = sma(bars.close, 10);
        auto slow = sma(bars.close, 30);
        std::vector<double> pos(bars.size(), 0);
        for (std::size_t i = 30; i < bars.size(); ++i)
            pos[i] = fast[i] > slow[i] ? 100 : -100;
        return pos;
    }
};

EngineConfig cfg;
cfg.initial_cash = 1'000'000;
cfg.commission.per_share = 0.005;

Engine engine(cfg);
engine.add_data("AAPL", CsvBarFeed::load("aapl_daily.csv"));

SmaCross strategy;
auto r = engine.run_vector("AAPL", strategy);
// r.sharpe_ratio, r.max_drawdown, r.total_trades, r.equity_curve ...
```

## Design

```
Engine
 ├─ data_     symbol -> BarSeries (struct-of-arrays: timestamp[], open[], high[], low[], close[], volume[])
 ├─ run()            event-driven: merged timeline -> Broker -> Strategy callbacks
 └─ run_vector()     vectorized:   Strategy.compute(BarSeries) -> position deltas -> Portfolio

Broker             pending orders -> process_bar() -> fills at next open
Portfolio          positions (average cost), cash, equity curve, round-trip trades
Metrics::compute   equity curve + trades -> PerformanceReport
```

| Decision | Why |
|---|---|
| Struct-of-arrays `BarSeries` | Vectorized mode iterates one field at a time (e.g. all close prices). Contiguous layout means the CPU prefetcher pulls in useful data, not unused OHLC fields |
| Signals delayed by one bar | `positions[i]` uses `close[i]`, so executing at `open[i]` would be look-ahead. Filling at `open[i+1]` matches event-driven semantics and avoids the bias |
| Average-cost position tracking | Handles partial closes and position flips correctly. Round-trip PnL is recorded at each close for win-rate / profit-factor metrics |
| Merged timeline for multi-symbol | All symbols' bars at the same timestamp are filled and processed together, so cross-symbol strategies see consistent state |
| Synthetic GBM data for benchmarks | Deterministic (seeded), realistic drift and vol, no external dependency. Same seed on any platform gives the same prices |

## Benchmarks

Setup: i5-12600K, GCC 15.2 `-O3 -march=native`, Windows 10, pinned to one P-core. SMA(10,30) crossover on 1M synthetic bars.

| Engine | Time | Per bar | Throughput |
|---|---|---|---|
| **Vectorized** | 50 ms | **50 ns** | 20M bars/s |
| **Event-driven** | 127 ms | **128 ns** | 7.9M bars/s |
| [Backtrader](https://github.com/mementum/backtrader) (Python) | 76.7 s | 76,694 ns | 13k bars/s |

### vs Backtrader

Same strategy (SMA 10/30 crossover), same workload (1M bars of synthetic GBM data), same machine.

- Event-driven mode: **599× faster**
- Vectorized mode: **1,534× faster**

The gap comes from: no interpreter overhead, struct-of-arrays layout for vectorized mode, pooled allocations, and no per-bar Python object creation. The benchmark source is [benchmarks/sma_backtrader.py](benchmarks/sma_backtrader.py).

## Layout

```
include/backtest/   types.h  series.h  data_feed.h  strategy.h  broker.h  portfolio.h  engine.h  metrics.h
src/                data_feed.cpp  strategy.cpp  broker.cpp  portfolio.cpp  engine.cpp  metrics.cpp
tests/              unit tests
benchmarks/         Google Benchmark
examples/           sma_crossover.cpp (both modes)
```

## Roadmap

- CSV loader auto-detection for Yahoo Finance / Binance formats
- Multi-asset vectorized mode with cross-symbol signals
- Tick-level event-driven mode with integration to [orderbook-engine](https://github.com/ax0080/orderbook-engine)
- Equity curve and drawdown chart export

## License

MIT
