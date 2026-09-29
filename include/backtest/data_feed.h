#pragma once

#include "series.h"

#include <cstdint>
#include <string>

namespace backtest {

struct CsvConfig {
    char delimiter  = ',';
    bool has_header = true;
    int  ts_col     = 0;
    int  open_col   = 1;
    int  high_col   = 2;
    int  low_col    = 3;
    int  close_col  = 4;
    int  volume_col = 5;
};

class CsvBarFeed {
public:
    static BarSeries load(const std::string& path, const CsvConfig& cfg = {});
};

class SyntheticFeed {
public:
    static BarSeries generate(std::size_t count,
                              double start_price = 100.0,
                              uint32_t seed = 42);
};

} // namespace backtest
