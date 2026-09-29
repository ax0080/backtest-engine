#include "backtest/data_feed.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

namespace backtest {

namespace {

Timestamp parse_timestamp(const std::string& s) {
    if (s.empty()) return 0;

    // All digits → milliseconds since epoch.
    if (std::all_of(s.begin(), s.end(), ::isdigit))
        return std::stoll(s);

    // YYYY-MM-DD (with optional Thh:mm:ss suffix).
    std::tm tm{};
    if (std::sscanf(s.c_str(), "%d-%d-%d", &tm.tm_year, &tm.tm_mon, &tm.tm_mday) >= 3) {
        tm.tm_year -= 1900;
        tm.tm_mon  -= 1;
#ifdef _WIN32
        auto epoch = _mkgmtime(&tm);
#else
        auto epoch = timegm(&tm);
#endif
        return static_cast<Timestamp>(epoch) * 1000;
    }
    return 0;
}

std::vector<std::string> split(const std::string& line, char delim) {
    std::vector<std::string> cols;
    std::istringstream ss(line);
    std::string cell;
    while (std::getline(ss, cell, delim)) cols.push_back(cell);
    return cols;
}

} // namespace

BarSeries CsvBarFeed::load(const std::string& path, const CsvConfig& cfg) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot open " + path);

    BarSeries bars;
    std::string line;

    if (cfg.has_header && !std::getline(file, line))
        return bars;

    while (std::getline(file, line)) {
        if (line.empty()) continue;
        auto cols = split(line, cfg.delimiter);

        int need = 1 + std::max({cfg.ts_col, cfg.open_col, cfg.high_col,
                                 cfg.low_col, cfg.close_col, cfg.volume_col});
        if (static_cast<int>(cols.size()) < need) continue;

        Bar bar;
        bar.timestamp = parse_timestamp(cols[cfg.ts_col]);
        bar.open      = std::stod(cols[cfg.open_col]);
        bar.high      = std::stod(cols[cfg.high_col]);
        bar.low       = std::stod(cols[cfg.low_col]);
        bar.close     = std::stod(cols[cfg.close_col]);
        bar.volume    = std::stod(cols[cfg.volume_col]);
        bars.push_back(bar);
    }
    return bars;
}

BarSeries SyntheticFeed::generate(std::size_t count, double start_price,
                                  uint32_t seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> norm(0.0, 1.0);

    constexpr double mu    = 0.10;
    constexpr double sigma = 0.20;
    constexpr double dt    = 1.0 / 252.0;

    BarSeries bars;
    bars.reserve(count);

    double    price = start_price;
    Timestamp ts    = 946684800000;  // 2000-01-01 UTC in millis

    for (std::size_t i = 0; i < count; ++i) {
        double z     = norm(rng);
        double ret   = (mu - 0.5 * sigma * sigma) * dt + sigma * std::sqrt(dt) * z;
        double close = price * std::exp(ret);
        double open  = price * (1.0 + 0.001 * norm(rng));
        double high  = std::max(open, close) * (1.0 + 0.005 * std::abs(norm(rng)));
        double low   = std::min(open, close) * (1.0 - 0.005 * std::abs(norm(rng)));
        double vol   = 1'000'000 + 500'000 * std::abs(norm(rng));

        bars.push_back({ts, open, high, low, close, vol});
        price = close;
        ts += 86'400'000;
    }
    return bars;
}

} // namespace backtest
