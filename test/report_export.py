#!/usr/bin/env python3
import argparse, json, os, subprocess
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--duckdb', required=True)
p.add_argument('--extension', required=True)
p.add_argument('--output', required=True)
a = p.parse_args()
out = Path(a.output).resolve()
out.mkdir(parents=True, exist_ok=True)


def lit(s):
    return "'" + s.replace("'", "''") + "'"


env = dict(os.environ, MINIPLOT_EXPORTER=str(Path('scripts/export-chart.cjs').resolve()))


def run(q):
    return subprocess.run(
        [a.duckdb, '-unsigned', '-json', ':memory:'],
        input='.bail on\nLOAD ' + lit(str(Path(a.extension).resolve())) + ';\n' + q,
        text=True,
        capture_output=True,
        timeout=120,
        env=env,
    )


def sql(q):
    r = run(q)
    assert r.returncode == 0, (q, r.stderr)
    return json.loads(r.stdout) if r.stdout.strip() else []


names = {
    r['function_name']
    for r in sql(
        "SELECT DISTINCT function_name FROM duckdb_functions() WHERE function_name IN ('chart_export','chart_report','chart_report_html');"
    )
}
assert names == {'chart_export', 'chart_report', 'chart_report_html'}, names
spec = {
    'title': '分析レポート <script>',
    'sections': [
        {
            'heading': 'Summary',
            'text': '日本語と <script>injected=1</script>\nSecond line & quotes "',
            'data': [{'type': 'bar', 'x': ['A', 'B'], 'y': [1, 2]}],
            'layout': {'title': {'text': 'Sales'}},
        },
        {
            'heading': 'Detail',
            'text': 'All observations retained.',
            'data': [{'type': 'scatter', 'x': [1, 2], 'y': [2, 3]}],
            'layout': {},
        },
    ],
}
spec['sections'][0]['text'] += chr(127)
expr = 'chart_report_html(' + lit(json.dumps(spec, ensure_ascii=False)) + ')'
html = sql('SELECT ' + expr + ' AS html;')[0]['html']
(out / 'report.html').write_text(html)
assert '<script>injected' not in html
subprocess.run(
    [
        'node',
        '-e',
        """
 const {parse}=require('parse5'),assert=require('node:assert/strict'),fs=require('node:fs');
 const {html,spec}=JSON.parse(fs.readFileSync(0,'utf8'));const nodes=[],errors=[];
 function walk(n){nodes.push(n);for(const c of n.childNodes||[])walk(c);}
 walk(parse(html,{onParseError:e=>errors.push(e.code)}));assert.deepEqual(errors,[]);
 const manifest=nodes.find(n=>n.attrs?.some(a=>a.name==='id'&&a.value==='miniplot-report'));
 assert(manifest,'missing machine-readable report');
 assert.deepEqual(JSON.parse(manifest.childNodes[0].value),spec);
 assert.equal(nodes.filter(n=>n.tagName==='script').length,3);
 """,
    ],
    input=json.dumps({'html': html, 'spec': spec}),
    text=True,
    check=True,
)

# All file-producing APIs must respect the host's external-access setting.
for call in [
    "bar_chart(['A'],[1.0],'x',PATH)",
    "pie_chart(['A'],[1.0],'x',PATH)",
    "chart_report(" + lit(json.dumps(spec)) + ",PATH)",
]:
    path = out / 'restricted.html'
    path.unlink(missing_ok=True)
    r = run('SET enable_external_access=false; SELECT ' + call.replace('PATH', lit(str(path))) + ';')
    assert r.returncode and not path.exists(), (call, r.stdout, r.stderr)
for suffix in ['png', 'pdf']:
    path = out / ('report.' + suffix)
    assert sql('SELECT chart_export(' + expr + ', ' + lit(str(path)) + ') AS path;')[0]['path'] == str(path)
    assert path.stat().st_size > 1000
saved = out / 'saved.html'
sql('SELECT chart_report(' + lit(json.dumps(spec, ensure_ascii=False)) + ',' + lit(str(saved)) + ');')
assert saved.read_text() == html
for spec in [
    'bad',
    '{}',
    '{"title":"x","sections":[]}',
    '{"title":"x","sections":[{"text":7}]}',
    '{"title":"x","sections":[{"data":[null]}]}',
    '{"title":"x","sections":[{"text":"safe","layout":[]}]}',
    '{"title":"x","sections":[{"text":"safe","config":null}]}',
]:
    assert sql('SELECT TRY(chart_report_html(' + lit(spec) + ')) IS NULL AS bad;')[0]['bad']
assert sql('SELECT chart_report_html(NULL) IS NULL AS n;')[0]['n']
assert sql('SET enable_external_access=false; SELECT length(' + expr + ') > 0 AS ok;')[0]['ok']
rows = sql(
    "SELECT TRY(chart_report_html(spec)) IS NULL AS bad FROM (VALUES ('bad'), ('{\"title\":\"ok\",\"sections\":[{\"text\":\"safe\"}]}'), (NULL)) t(spec);"
)
assert [r['bad'] for r in rows] == [True, False, True]

# Invalid/failed exports must preserve an existing output file.
for bad in ['<html><script>broken(', '<!DOCTYPE html><html><head><title>x</title></head><body>no chart</body></html>']:
    path = out / 'must-remain.png'
    path.write_bytes(b'keep')
    r = run('SELECT chart_export(' + lit(bad) + ',' + lit(str(path)) + ');')
    assert r.returncode and path.read_bytes() == b'keep'
assert run('SELECT chart_export(' + expr + ',' + lit(str(out / 'bad.svg')) + ');').returncode
assert run(
    'SET enable_external_access=false; SELECT chart_export(' + expr + ',' + lit(str(out / 'denied.png')) + ');'
).returncode
assert not (out / 'denied.png').exists()
print('Structured report, SQL PNG/PDF exports and failure preservation passed')
