#!/usr/bin/env bash
set -euo pipefail
repo_root="$(cd "$(dirname "$0")/.." && pwd)"
test_dir="$(mktemp -d)"
trap 'rm -rf "$test_dir"' EXIT
"${CXX:-c++}" -std=c++11 -Wall -Wextra -Werror -I "$repo_root/src/include" "$repo_root/test/chart_html_test.cpp" -o "$test_dir/test"
"$test_dir/test"
