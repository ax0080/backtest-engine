"""Benchmark: SMA crossover on synthetic data using backtrader.

Generates the same GBM price series as the C++ SyntheticFeed (seed=42),
runs SMA(10,30) crossover, and prints wall-clock time.

Usage:
    python sma_backtrader.py [bars]       (default: 1000000)
"""

import math
import random
import sys
import time

import backtrader as bt


def generate_bars(count: int, start_price: float = 100.0, seed: int = 42):
    """Geometric Brownian Motion — mirrors SyntheticFeed::generate()."""
    rng = random.Random(seed)
    mu, sigma, dt = 0.10, 0.20, 1.0 / 252.0

    bars = []
    price = start_price
    for _ in range(count):
        z = rng.gauss(0, 1)
        ret = (mu - 0.5 * sigma * sigma) * dt + sigma * math.sqrt(dt) * z
        close = price * math.exp(ret)
        o = price * (1.0 + 0.001 * rng.gauss(0, 1))
        h = max(o, close) * (1.0 + 0.005 * abs(rng.gauss(0, 1)))
        lo = min(o, close) * (1.0 - 0.005 * abs(rng.gauss(0, 1)))
        vol = 1_000_000 + 500_000 * abs(rng.gauss(0, 1))
        bars.append((o, h, lo, close, vol))
        price = close
    return bars


class SyntheticData(bt.feeds.DataBase):
    """In-memory data feed from generated bars."""

    def __init__(self, bars, **kwargs):
        super().__init__(**kwargs)
        self._bars = bars
        self._idx = -1

    def start(self):
        super().start()
        self._idx = -1

    def _load(self):
        self._idx += 1
        if self._idx >= len(self._bars):
            return False
        o, h, lo, c, v = self._bars[self._idx]
        self.lines.open[0] = o
        self.lines.high[0] = h
        self.lines.low[0] = lo
        self.lines.close[0] = c
        self.lines.volume[0] = v
        self.lines.datetime[0] = bt.date2num(
            bt.datetime.datetime(2000, 1, 1) + bt.datetime.timedelta(days=self._idx)
        )
        return True


class SmaCrossover(bt.Strategy):
    params = dict(fast=10, slow=30, size=100)

    def __init__(self):
        fast_sma = bt.indicators.SMA(self.data.close, period=self.p.fast)
        slow_sma = bt.indicators.SMA(self.data.close, period=self.p.slow)
        self.crossover = bt.indicators.CrossOver(fast_sma, slow_sma)

    def next(self):
        pos = self.getposition().size
        if self.crossover > 0:
            if pos < 0:
                self.buy(size=-pos)
            if pos <= 0:
                self.buy(size=self.p.size)
        elif self.crossover < 0:
            if pos > 0:
                self.sell(size=pos)
            if pos >= 0:
                self.sell(size=self.p.size)


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 1_000_000

    print(f"Generating {count:,} bars …")
    bars = generate_bars(count)

    cerebro = bt.Cerebro()
    cerebro.addstrategy(SmaCrossover)
    cerebro.adddata(SyntheticData(bars))
    cerebro.broker.setcash(1_000_000)
    cerebro.broker.setcommission(commission=0.005, commtype=bt.CommInfoBase.COMM_FIXED)

    print("Running backtrader …")
    t0 = time.perf_counter()
    cerebro.run()
    elapsed = time.perf_counter() - t0

    rate = count / elapsed
    print(f"  {count:,} bars in {elapsed:.3f}s  ({rate:,.0f} bars/s)")
    print(f"  {elapsed / count * 1e9:.0f} ns/bar")
    print(f"  Final equity: ${cerebro.broker.getvalue():,.2f}")


if __name__ == "__main__":
    main()
