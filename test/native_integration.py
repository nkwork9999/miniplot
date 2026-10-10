#!/usr/bin/env python3
"""Run against a built extension and its matching DuckDB CLI; no Python packages needed."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile


def literal(value):
    return "'" + value.replace("'", "''") + "'"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--duckdb", required=True)
    parser.add_argument("--extension", required=True)
    args = parser.parse_args()
    extension = str(Path(args.extension).resolve())

    def sql(query):
        result = subprocess.run(
            [args.duckdb, "-unsigned", "-json", ":memory:"],
            input=".bail on\nLOAD " + literal(extension) + ";\n" + query,
            text=True,
            capture_output=True,
        )
        if result.returncode:
            raise AssertionError(result.stderr + "\nSQL: " + query)
        return json.loads(result.stdout) if result.stdout.strip() else []

    def html(expression):
        return sql("SELECT " + expression + " AS html;")[0]["html"]

    charts = []
    data = [
        {"x": [1, 2], "y": [3, None], "type": "scatter", "name": "first"},
        {"x": [1], "y": [4], "type": "bar", "name": "second"},
    ]
    layout = {
        "title": {"text": "日本語 </script><script>injected=1</script> ' \\\n\u2028\u2029"},
        "xaxis": {"title": "Time", "range": [0, 3]},
        "showlegend": True,
        "__proto__": {"polluted": True},
    }
    config = {"responsive": False, "displayModeBar": False}
    values = ",".join(literal(json.dumps(x, ensure_ascii=False)) for x in [data, layout, config])
    charts.append(
        {
            "name": "arbitrary JSON and hostile strings",
            "html": html("plotly_new_html(" + values + ")"),
            "expected": [data, layout, config],
        }
    )
    charts.append({"name": "config default", "html": html("plotly_new_html('[]', '{}')"), "expected": [[], {}, {}]})
    charts.append(
        {
            "name": "DuckDB JSON from nested structs",
            "html": html(
                "plotly_new_html(to_json([{'x': [1,2], 'y': [3,4], 'type': 'scatter'}]), "
                "to_json({'title': {'text': 'From structs'}}))"
            ),
            "expected": [[{"x": [1, 2], "y": [3, 4], "type": "scatter"}], {"title": {"text": "From structs"}}, {}],
        }
    )

    failures = sql(
        "SELECT TRY(plotly_new_html(data, layout, config)) IS NULL AS failed FROM (VALUES "
        "('bad','{}','{}'), ('{}','{}','{}'), ('[null]','{}','{}'), "
        "('[]','[]','{}'), ('[]','{}','null'), ('[{\"x\":[NaN]}]','{}','{}'), "
        "('[]','{}','{}'), ('[{}]','{}','{}')) t(data, layout, config);"
    )
    assert [r["failed"] for r in failures] == [True] * 6 + [False, False]
    nulls = sql(
        "SELECT plotly_new_html(NULL, '{}') IS NULL AS a, "
        "plotly_new_html('[]', '{}', NULL) IS NULL AS b, "
        "scatter_chart_html(NULL::TIMESTAMP[], [1.0], 'Time') IS NULL AS c;"
    )
    assert all(nulls[0].values())
    batch = sql(
        "SELECT count(*) AS n FROM (SELECT plotly_new_html(CASE WHEN i % 2 = 0 THEN '[]' ELSE '[{}]' END, '{}') AS html "
        "FROM range(4097) t(i)) WHERE html LIKE '%Plotly.newPlot%';"
    )
    assert batch[0]["n"] == 4097

    axes = [
        ("[1.0, 2.0]", [1, 2], "linear"),
        ("[DATE '2026-01-01', DATE '2026-01-03']", ["2026-01-01", "2026-01-03"], "date"),
        (
            "[TIMESTAMP '2026-01-01 10:00:00.123456', TIMESTAMP '2026-01-01 11:00:00']",
            ["2026-01-01 10:00:00.123456", "2026-01-01 11:00:00"],
            "date",
        ),
        (
            "[TIMESTAMPTZ '2026-01-01 09:00:00+09', TIMESTAMPTZ '2026-01-01 02:00:00+01']",
            ["2026-01-01 00:00:00", "2026-01-01 01:00:00"],
            "date",
        ),
    ]
    for x_expr, x, x_type in axes:
        for y_expr, y, y_type in axes:
            charts.append(
                {
                    "name": x_expr + " / " + y_expr,
                    "html": html(f"scatter_chart_html({x_expr}, {y_expr}, 'Time')"),
                    "expected": [
                        [{"type": "scatter", "mode": "markers", "x": x, "y": y}],
                        {"title": {"text": "Time"}, "xaxis": {"type": x_type}, "yaxis": {"type": y_type}},
                        {"responsive": True},
                    ],
                }
            )
    failures = sql(
        "SELECT TRY(scatter_chart_html(x, y, 'Time')) IS NULL AS failed FROM (VALUES "
        "([NULL::TIMESTAMP], [1.0]), ([TIMESTAMP 'infinity'], [1.0]), "
        "([]::TIMESTAMP[], []::DOUBLE[]), ([TIMESTAMP '2026-01-01'], [1.0,2.0]), "
        "([TIMESTAMP '2026-01-01'], [1.0])) t(x,y);"
    )
    assert [r["failed"] for r in failures] == [True] * 4 + [False]
    subprocess.run(
        ["node", str(Path(__file__).with_name("plotly-output.cjs"))], input=json.dumps(charts), text=True, check=True
    )

    # File mode must agree with the pure HTML function and must not launch a browser.
    with tempfile.TemporaryDirectory(prefix="miniplot-integration-") as directory:
        output = str(Path(directory) / "chart.html")
        rows = sql(f"SELECT plotly_new({values}, {literal(output)}) AS path;")
        assert rows[0]["path"] == output
        assert Path(output).read_text() == charts[0]["html"]
        generated = sql("SELECT plotly_new('[]', '{}', '{}', 'tmp') AS path FROM range(3);")
        assert len({r["path"] for r in generated}) == 3, "file functions must run once per row"
        for row in generated:
            path = Path(row["path"])
            try:
                assert path.read_text() == charts[1]["html"]
            finally:
                path.unlink()
        output = str(Path(directory) / "dated.html")
        sql("SELECT scatter_chart([DATE '2026-01-01'], [1.0], 'Time', " + literal(output) + ");")
        assert Path(output).read_text() == html("scatter_chart_html([DATE '2026-01-01'], [1.0], 'Time')")

    metadata = sql(
        "SELECT function_name, parameters, parameter_types, description, examples, categories "
        "FROM duckdb_functions() WHERE function_name IN "
        "('miniplot','miniplot_openssl_version','bar_chart','bar_chart_html','line_chart','line_chart_html',"
        "'area_chart','area_chart_html','scatter_chart','scatter_chart_html','scatter_3d_chart',"
        "'scatter_3d_chart_html','plotly_new','plotly_new_html');"
    )
    assert len({r["function_name"] for r in metadata}) == 14
    for row in metadata:
        assert row["description"] and row["examples"] and row["categories"], row
        assert len(row["parameters"]) == len(row["parameter_types"]), row
        assert all(not name.startswith("col") for name in row["parameters"]), row
    # Execute every documented pure expression, so examples cannot silently rot.
    examples = [
        row["examples"][0]
        for row in metadata
        if row["function_name"].endswith("_html") or row["function_name"].startswith("miniplot")
    ]
    example_results = sql(
        "SELECT " + ",".join(f"length({example}) > 0 AS example_{i}" for i, example in enumerate(examples)) + ";"
    )
    assert all(example_results[0].values())
    print(f"File output, unique per-row paths and {len(metadata)} documented overloads passed")


if __name__ == "__main__":
    main()
