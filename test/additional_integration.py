#!/usr/bin/env python3
"""Exercise ten chart APIs through SQL and execute their generated JavaScript."""

import argparse
import json
import subprocess
import tempfile
from pathlib import Path

# Literal, independently specified Plotly boundary contracts.
CASES = [
    ('pie', "['A','B'], [1.0,2.0]", {'type': 'pie', 'labels': ['A', 'B'], 'values': [1, 2]}, {}),
    ('donut', "['A','B'], [1.0,2.0]", {'type': 'pie', 'labels': ['A', 'B'], 'values': [1, 2], 'hole': 0.5}, {}),
    ('horizontal_bar', "['A','B'], [1.0,2.0]", {'type': 'bar', 'y': ['A', 'B'], 'x': [1, 2], 'orientation': 'h'}, {}),
    ('violin', '[1.0,2.0,2.0]', {'type': 'violin', 'y': [1, 2, 2]}, {}),
    (
        'bubble',
        '[1.0,2.0], [3.0,4.0], [5.0,6.0]',
        {'type': 'scatter', 'mode': 'markers', 'x': [1, 2], 'y': [3, 4], 'marker': {'size': [5, 6]}},
        {},
    ),
    (
        'heatmap',
        '[1.0,2.0], [3.0,4.0], [[5.0,6.0],[7.0,8.0]]',
        {'type': 'heatmap', 'x': [1, 2], 'y': [3, 4], 'z': [[5, 6], [7, 8]]},
        {},
    ),
    (
        'contour',
        '[1.0,2.0], [3.0,4.0], [[5.0,6.0],[7.0,8.0]]',
        {'type': 'contour', 'x': [1, 2], 'y': [3, 4], 'z': [[5, 6], [7, 8]]},
        {},
    ),
    (
        'step',
        '[1.0,2.0], [3.0,4.0]',
        {'type': 'scatter', 'mode': 'lines', 'x': [1, 2], 'y': [3, 4], 'line': {'shape': 'hv'}},
        {'xaxis': {'type': 'linear'}, 'yaxis': {'type': 'linear'}},
    ),
    ('radar', "['A','B'], [1.0,2.0]", {'type': 'scatterpolar', 'theta': ['A', 'B'], 'r': [1, 2], 'fill': 'toself'}, {}),
    (
        'scatter_gl',
        '[1.0,2.0], [3.0,4.0]',
        {'type': 'scattergl', 'mode': 'markers', 'x': [1, 2], 'y': [3, 4]},
        {'xaxis': {'type': 'linear'}, 'yaxis': {'type': 'linear'}},
    ),
]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--duckdb', required=True)
    parser.add_argument('--extension', required=True)
    parser.add_argument('--fixtures')
    args = parser.parse_args()

    def lit(s):
        return "'" + s.replace("'", "''") + "'"

    load = 'LOAD ' + lit(str(Path(args.extension).resolve())) + ';\n'

    def run(q):
        return subprocess.run(
            [args.duckdb, '-unsigned', '-json', ':memory:'],
            input='.bail on\n' + load + q,
            text=True,
            capture_output=True,
            timeout=60,
        )

    def sql(q):
        r = run(q)
        assert r.returncode == 0, (q, r.stderr)
        return json.loads(r.stdout) if r.stdout.strip() else []

    charts = []
    missing = []
    title = "日本語 </title><script>injected=1</script> & ' \"\n"
    with tempfile.TemporaryDirectory() as tmp:
        for name, inputs, trace, axes in CASES:
            expr = f'{name}_chart_html({inputs}, {lit(title)})'
            r = run('SELECT ' + expr + ' AS html;')
            if r.returncode:
                missing.append((name, r.stderr))
                continue
            html = json.loads(r.stdout)[0]['html']
            charts.append(
                {
                    'name': name,
                    'html': html,
                    'expected': [[trace], dict(title={'text': title}, **axes), {'responsive': True}],
                }
            )
            path = Path(tmp) / (name + '.html')
            rows = sql(f'SELECT {name}_chart({inputs}, {lit(title)}, {lit(str(path))}) AS path;')
            assert rows[0]['path'] == str(path) and path.read_text() == html
            # Typed NULL title preserves per-row NULL contract.
            assert sql(f'SELECT {name}_chart_html({inputs}, NULL) IS NULL AS n;')[0]['n']
            assert (
                sql(
                    f"SELECT count(*) AS n FROM (SELECT {name}_chart_html({inputs}, i::VARCHAR) AS h FROM range(4097) t(i) WHERE i%2=0) WHERE h LIKE '%Plotly.newPlot%';"
                )[0]['n']
                == 2049
            )
        assert not missing, missing
        invalid = [
            "pie_chart_html(['A'],[-1.0],'bad')",
            "donut_chart_html(['A'],[0.0],'bad')",
            "horizontal_bar_chart_html(['A'],[1.0,2.0],'bad')",
            "radar_chart_html(['A'],[-1.0],'bad')",
            "violin_chart_html(['NaN'::DOUBLE],'bad')",
            "bubble_chart_html([1.0],[2.0],[-1.0],'bad')",
            "heatmap_chart_html([1.0,2.0],[3.0,4.0],[[1.0],[2.0]],'bad')",
            "heatmap_chart_html([1.0],[3.0], [NULL::DOUBLE[]],'bad')",
            "contour_chart_html([2.0,1.0],[3.0,4.0],[[1.0,2.0],[3.0,4.0]],'bad')",
            "step_chart_html([1.0],[2.0,3.0],'bad')",
            "scatter_gl_chart_html([1.0],['Infinity'::DOUBLE],'bad')",
        ]
        for expr in invalid:
            assert sql('SELECT TRY(' + expr + ') IS NULL AS bad;')[0]['bad'], expr
        for name, inputs, _, _ in CASES:
            path = Path(tmp) / (name + '-invalid.html')
            # Empty first list must fail before creating output.
            empty = '[]::' + ('VARCHAR[]' if name in ['pie', 'donut', 'horizontal_bar', 'radar'] else 'DOUBLE[]')
            rest = inputs[inputs.index(']') + 1 :]
            r = run(f"SELECT {name}_chart({empty}{rest}, 'bad', {lit(str(path))});")
            assert r.returncode and not path.exists(), (name, r.stderr)
    subprocess.run(
        ['node', str(Path(__file__).with_name('plotly-output.cjs'))], input=json.dumps(charts), text=True, check=True
    )
    if args.fixtures:
        Path(args.fixtures).mkdir(parents=True, exist_ok=True)
        for c in charts:
            (Path(args.fixtures) / (c['name'] + '.html')).write_text(c['html'])
    names = ','.join(lit(n + '_chart' + s) for n, _, _, _ in CASES for s in ['', '_html'])
    rows = sql(
        f'SELECT function_name, parameters, parameter_types, description, examples, has_side_effects FROM duckdb_functions() WHERE function_name IN ({names});'
    )
    assert len(rows) == 30
    for r in rows:
        assert r['description'] and r['examples'] and len(r['parameters']) == len(r['parameter_types']), r
        assert r['has_side_effects'] != r['function_name'].endswith('_html'), r
        if r['function_name'].endswith('_html'):
            sql('SELECT ' + r['examples'][0] + ';')
    print('Ten charts: HTML, files, batches, invalid inputs, 30 catalog overloads passed')


if __name__ == '__main__':
    main()
