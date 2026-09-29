#pragma once

#include "types.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

namespace backtest {

// Struct-of-arrays layout: each field is a contiguous vector.
// Vectorized strategies iterate one field at a time (e.g. all close prices),
// which is cache-friendly compared to an array of Bar structs.
struct BarSeries {
    std::vector<Timestamp> timestamp;
    std::vector<double>    open;
    std::vector<double>    high;
    std::vector<double>    low;
    std::vector<double>    close;
    std::vector<double>    volume;

    std::size_t size()  const { return close.size(); }
    bool        empty() const { return close.empty(); }

    void reserve(std::size_t n) {
        timestamp.reserve(n);
        open.reserve(n);
        high.reserve(n);
        low.reserve(n);
        close.reserve(n);
        volume.reserve(n);
    }

    void push_back(const Bar& bar) {
        timestamp.push_back(bar.timestamp);
        open.push_back(bar.open);
        high.push_back(bar.high);
        low.push_back(bar.low);
        close.push_back(bar.close);
        volume.push_back(bar.volume);
    }

    Bar operator[](std::size_t i) const {
        return {timestamp[i], open[i], high[i], low[i], close[i], volume[i]};
    }
};

// ---- Indicators (operate on contiguous double vectors) ---------------------

inline std::vector<double> sma(const std::vector<double>& data, int period) {
    std::vector<double> out(data.size(), 0.0);
    if (period <= 0 || data.size() < static_cast<std::size_t>(period)) return out;

    double sum = 0;
    for (int i = 0; i < period; ++i) sum += data[i];
    out[period - 1] = sum / period;

    for (std::size_t i = static_cast<std::size_t>(period); i < data.size(); ++i) {
        sum += data[i] - data[i - period];
        out[i] = sum / period;
    }
    return out;
}

inline std::vector<double> ema(const std::vector<double>& data, int period) {
    std::vector<double> out(data.size(), 0.0);
    if (period <= 0 || data.empty()) return out;

    double k = 2.0 / (period + 1);
    out[0] = data[0];
    for (std::size_t i = 1; i < data.size(); ++i)
        out[i] = data[i] * k + out[i - 1] * (1.0 - k);
    return out;
}

inline std::vector<double> rsi(const std::vector<double>& data, int period = 14) {
    std::vector<double> out(data.size(), 50.0);
    if (period <= 0 || data.size() < 2) return out;

    double avg_gain = 0, avg_loss = 0;
    for (int i = 1; i <= period && i < static_cast<int>(data.size()); ++i) {
        double ch = data[i] - data[i - 1];
        if (ch > 0) avg_gain += ch; else avg_loss -= ch;
    }
    avg_gain /= period;
    avg_loss /= period;

    auto rs_to_rsi = [](double g, double l) {
        return l == 0 ? 100.0 : 100.0 - 100.0 / (1.0 + g / l);
    };
    if (static_cast<std::size_t>(period) < data.size())
        out[period] = rs_to_rsi(avg_gain, avg_loss);

    for (std::size_t i = static_cast<std::size_t>(period) + 1; i < data.size(); ++i) {
        double ch = data[i] - data[i - 1];
        avg_gain = (avg_gain * (period - 1) + std::max(ch, 0.0)) / period;
        avg_loss = (avg_loss * (period - 1) + std::max(-ch, 0.0)) / period;
        out[i] = rs_to_rsi(avg_gain, avg_loss);
    }
    return out;
}

// +1 where a crosses above b, -1 where below, 0 otherwise.
inline std::vector<int> crossover(const std::vector<double>& a,
                                  const std::vector<double>& b) {
    std::vector<int> out(a.size(), 0);
    std::size_t n = std::min(a.size(), b.size());
    for (std::size_t i = 1; i < n; ++i) {
        if (a[i] > b[i] && a[i - 1] <= b[i - 1]) out[i] =  1;
        else if (a[i] < b[i] && a[i - 1] >= b[i - 1]) out[i] = -1;
    }
    return out;
}

} // namespace backtest
