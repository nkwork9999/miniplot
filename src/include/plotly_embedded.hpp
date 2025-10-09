#pragma once

#include <string>

// plotly_data.incはビルド時に生成される
#include "plotly_data.inc"

namespace duckdb {

// Plotly.js埋め込みデータを取得
inline std::string GetPlotlyJS() {
    return std::string(plotly_embedded_data::PLOTLY_JS_CONTENT);
}

} // namespace duckdb