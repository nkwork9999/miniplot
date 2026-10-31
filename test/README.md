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

The native checks also verify numeric/date/time line and area axes, unequal
sampling intervals, near-adjacent doubles, very small/large finite values, invalid
rows with TRY recovery, filtered batches, file output and runnable catalog examples.

Histogram and box-chart checks verify duplicate samples and DOUBLE precision in
the generated Plotly trace, NULL/NaN/infinite/empty input handling, TRY recovery,
filtered batches, per-row temporary paths, and file/HTML output equivalence.
They inspect trace inputs rather than Plotly's rendered histogram/quartile results.

## Additional APIs, reports, artifacts and large inputs

Optional browser dependencies: `npm ci && npx playwright install chromium`.
After building, run the existing native suite, then:

```sh
python3 test/additional_integration.py --duckdb /path/to/duckdb --extension /path/to/miniplot.duckdb_extension --fixtures build/validation/charts
python3 test/report_export.py --duckdb /path/to/duckdb --extension /path/to/miniplot.duckdb_extension --output build/validation/export
python3 test/large_integration.py --duckdb /path/to/duckdb --extension /path/to/miniplot.duckdb_extension
npm run test:browser
```

The additional suite checks ten independent expected Plotly traces, dimensions,
invalid values, filtered batches, file equivalence and all 30 new catalog overloads.
`parse5` rejects malformed generated HTML; JavaScript checks catch script breakout.
The report suite verifies escaped prose, exact embedded JSON, malformed specs,
external-access restrictions, real SQL PNG/PDF output and failure preservation.
The browser suite uses the real local Plotly bundle, checks ten nonblank images,
and validates exported report dimensions and PDF page count. WebGL availability
is required. Performance scripts record measurements rather than impose flaky
wall-clock thresholds. The million-point test checks data retention, not a claim
that every browser/GPU can interact smoothly with a million points.
