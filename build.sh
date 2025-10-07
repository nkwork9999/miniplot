#!/bin/bash
set -e

echo "=== Building Miniplot Extension with Static Linking ==="

echo "Step 1: Building Rust library and standalone binary..."
cd chart_viewer
cargo build --release --lib
cargo build --release --bin chart_viewer_standalone
cd ..

echo "Step 2: Copying standalone binary to local directory..."
# /usr/local/bin ではなくプロジェクト内にコピー
mkdir -p bin
cp chart_viewer/target/release/chart_viewer_standalone bin/

echo "Step 3: Building DuckDB extension..."
make release

echo ""
echo "=== Build Complete ==="
echo "Extension: build/release/extension/miniplot/miniplot.duckdb_extension"
echo "Standalone: bin/chart_viewer_standalone"
ls -lh build/release/extension/miniplot/miniplot.duckdb_extension
ls -lh bin/chart_viewer_standalone