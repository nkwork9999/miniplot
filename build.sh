#!/bin/bash
set -e

echo "=== Building Miniplot Extension (100% Offline) ==="

# Step 1: Plotly.jsを埋め込み
echo ""
echo "Step 1: Embedding Plotly.js..."
bash scripts/embed_plotly.sh  # chmod不要で直接bashで実行

# Step 2: DuckDBエクステンションをビルド
echo ""
echo "Step 2: Building DuckDB extension..."
make release

echo ""
echo "=== Build Complete ==="
echo ""
ls -lh build/release/extension/miniplot/miniplot.duckdb_extension
echo ""
echo "✓ Extension built successfully"
echo "✓ 100% offline (no CDN dependencies)"
echo "✓ Plotly.js embedded (~3MB)"