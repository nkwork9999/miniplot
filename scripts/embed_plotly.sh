#!/bin/bash
set -e

echo "=== Embedding Plotly.js into C++ header ==="

PLOTLY_FILE="src/include/plotly.min.js"
OUTPUT_FILE="src/include/plotly_data.inc"

# Plotly.jsをダウンロード（まだ存在しない場合のみ）
if [ ! -f "$PLOTLY_FILE" ]; then
    echo "Downloading Plotly.js..."
    mkdir -p src/include
    curl -L -o "$PLOTLY_FILE" https://cdn.plot.ly/plotly-2.27.0.min.js
    echo "✓ Downloaded Plotly.js"
fi

echo "Generating C++ include file..."

# C++のraw string literalとして埋め込む
cat > "$OUTPUT_FILE" << 'HEADER_START'
// Auto-generated file - DO NOT EDIT
// Generated from plotly.min.js

namespace duckdb {
namespace plotly_embedded_data {

static const char* PLOTLY_JS_CONTENT = R"PLOTLYJS(
HEADER_START

cat "$PLOTLY_FILE" >> "$OUTPUT_FILE"

cat >> "$OUTPUT_FILE" << 'HEADER_END'
)PLOTLYJS";

} // namespace plotly_embedded_data
} // namespace duckdb
HEADER_END

echo "✓ Plotly.js embedded successfully"
FILE_SIZE=$(wc -c < "$OUTPUT_FILE")
echo "  Output size: $((FILE_SIZE / 1024 / 1024))MB"
echo "  Location: $OUTPUT_FILE"