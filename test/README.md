# Regression tests

Run `bash scripts/test-unit.sh` from the repository root. The tests compile the
production C++ helpers without downloading DuckDB or browser libraries.
The tests cover all chart types, title escaping, script termination, control
bytes, double precision, and decimal locales. DuckDB SQL tests under `test/sql`
require the extension build and run with `make test`.

`test/sql/plotly.test` covers JSON shape errors, NULLs, date axes, TRY recovery,
multi-batch inputs and metadata for every overload. After building, run the
native integration checks with a matching DuckDB CLI and Node.js:

```sh
python3 test/native_integration.py --duckdb /path/to/duckdb \
  --extension build/release/extension/miniplot/miniplot.duckdb_extension
```

This executes SQL and the generated JavaScript, verifies all 16 date/numeric axis
combinations, checks custom and temporary output paths, and executes every pure
catalog example. The JavaScript check records the arguments delivered to Plotly;
it does not load Plotly or claim browser rendering coverage.
