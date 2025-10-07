#!/bin/bash
set -e

echo "=== Building Miniplot Extension (Offline Plotly.js) ==="

# Plotly.jsの存在確認
if [ ! -f "chart_viewer/assets/plotly.min.js" ]; then
    echo "Error: plotly.min.js not found!"
    echo "Please download it first:"
    echo "  mkdir -p chart_viewer/assets"
    echo "  curl -o chart_viewer/assets/plotly.min.js https://cdn.plot.ly/plotly-2.27.0.min.js"
    exit 1
fi

echo "Step 1: Building Rust library..."
cd chart_viewer
cargo build --release --lib
cd ..

echo "Step 2: Building DuckDB extension..."
make release

echo ""
echo "=== Build Complete ==="
echo "Extension: build/release/extension/miniplot/miniplot.duckdb_extension"
ls -lh build/release/extension/miniplot/miniplot.duckdb_extension

echo ""
echo "Plotly.js embedded: $(ls -lh chart_viewer/assets/plotly.min.js | awk '{print $5}')"