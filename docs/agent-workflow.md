# SQL-to-artifact workflow for agents

Prefer a structured specification over assembling executable JavaScript or raw HTML.
Miniplot does not invoke an LLM. These APIs let an existing agent create, inspect,
reproduce and export artifacts through SQL.

1. Discover the installed API. The installed binary is authoritative, especially
   when using an older community build:

   ```sql
   SELECT function_name, parameters, parameter_types, description, examples,
          has_side_effects
   FROM duckdb_functions()
   WHERE function_name IN ('chart_report_html', 'chart_report', 'chart_export',
                           'scatter_gl_chart_html', 'heatmap_chart_html');
   ```

2. Build JSON from DuckDB structs with `to_json`. Validate against
   [report.schema.json](report.schema.json) when producing a report specification.
   Report fields are plain strings and Plotly trace/layout/config JSON. Raw HTML
   and Markdown are not interpreted. Invalid SQL inputs produce errors;
   `TRY(chart_report_html(spec))` can recover per row.
3. Use pure `*_html` functions while iterating. They do not create files or open
   a browser. Query the function catalog for runnable examples rather than infer
   signatures from another plotting library.
4. Save explicitly with `chart_report(spec, 'report.html')` or a chart's optional
   output path. For native PNG/PDF artifacts, install the optional local exporter
   and call `chart_export(chart_report_html(spec), 'report.pdf')`.
5. Preserve the specification. Generated reports contain its exact JSON values
   in `script#miniplot-report[type="application/json"]`. Parse that text as JSON;
   never evaluate it. This retains provenance fields supplied in the specification
   as well as chart data, without inferring values from pixels.

For large datasets, filter/aggregate in SQL or choose `scatter_gl_chart_html`.
No API silently samples data. Reports retain both their specification and plotting
instructions, increasing HTML size relative to a standalone chart.

Native export uses a separate Node/Chromium process, with local Plotly resources
and network requests blocked. It waits for chart promises/fonts and writes the
destination only after successful rendering. Browser startup occurs for every
export call. File/browser/export functions are volatile and honor DuckDB's
external-access setting; pure HTML generation remains available when disabled.

## Primary references used in the design

- [DuckDB function catalog](https://duckdb.org/docs/current/sql/functions/overview):
  native discovery through `duckdb_functions()`.
- [Plotly configuration](https://plotly.com/javascript/configuration-options/):
  chart configuration remains Plotly JSON rather than a second incompatible option system.
- [Playwright Page API](https://playwright.dev/docs/api/class-page):
  screenshot/PDF output and page/viewport behavior.

The report contract, embedded specification and SQL export integration are
project design choices; the references document the underlying capabilities.
