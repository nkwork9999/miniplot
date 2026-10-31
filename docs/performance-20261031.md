# Performance measurements: 2026-10-31

Measured on macOS 26.2 arm64 with the official DuckDB 1.5.6 CLI and matching
SDK `069cc9f9b5be802405797faecc284961b07c70ef`, using Release loadable extensions.
Baseline source is commit `1210228`. [Raw samples](performance-20261031.json)
include every repetition and generated HTML length.

## SQL aggregation and HTML generation

Each measurement launches the CLI, aggregates generated samples and returns the
HTML length. Five repetitions were run sequentially per case; the table shows
medians. Browser rendering is excluded. Numeric serialization reuses one classic
locale stream per array rather than constructing a stream for each number.

| Chart | Points | Before (s) | After (s) | Reduction |
|---|---:|---:|---:|---:|
| Scatter | 100,000 | 0.353 | 0.308 | 12.8% |
| Histogram | 100,000 | 0.191 | 0.176 | 7.7% |
| Scatter | 1,000,000 | 4.707 | 3.709 | 21.2% |
| Histogram | 1,000,000 | 3.221 | 1.929 | 40.1% |

The x samples are `i::DOUBLE`; scatter y samples are `(i % 100)::DOUBLE`.
The measurement includes process startup, SQL aggregation and HTML serialization,
so it does not isolate serializer CPU time. After HTML includes 42 bytes of new
generator metadata. Precision and million-point retention are checked separately
by the integration suites. No points are silently sampled.

## Real WebGL PNG export

A separate single run per size generated `scatter_gl_chart` HTML and rendered it
with local Plotly.js 2.27.0 in headless Chromium 141 / Playwright 1.56.1, using
SwiftShader software WebGL. PNG export includes browser launch, Plotly load,
drawing and full-page screenshot at a 1200-pixel viewport.

| Points | SQL to HTML (s) | Chromium to PNG (s) | PNG bytes |
|---:|---:|---:|---:|
| 100,000 | 0.362 | 36.100 | 17,174 |
| 1,000,000 | 3.681 | 54.322 | 16,571 |

These dense point sets overlap heavily in the image, explaining the small file
sizes. Both recorded exports succeeded and the million-point image was inspected.
An earlier exploratory browser run timed out at `page.setContent` (30 seconds).
Therefore these single-run results do not establish stable latency or interactive
frame rate. GPU, browser, concurrent load and data shape matter. Filter or
aggregate in SQL when plotting all observations would obscure the result.

## Reproduce

```sh
npm ci
npx playwright install chromium
python3 test/benchmark.py --duckdb /path/to/duckdb --extension /path/to/miniplot.duckdb_extension --output benchmark.json --repeats 5
node test/render_benchmark.cjs /path/to/duckdb /path/to/miniplot.duckdb_extension
```

The render benchmark records errors as well as successes. Performance scripts
have no wall-clock pass thresholds; functional tests check precision, validity,
counts, artifact output and error recovery. Measurements describe this host and
these queries, rather than a guarantee for arbitrary datasets.
