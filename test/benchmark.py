#!/usr/bin/env python3
"""Measure end-to-end SQL HTML serialization; not browser render time."""

import argparse, json, platform, statistics, subprocess, time
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--duckdb', required=True)
p.add_argument('--extension', required=True)
p.add_argument('--output', required=True)
p.add_argument('--repeats', type=int, default=3)
a = p.parse_args()
ext = str(Path(a.extension).resolve()).replace("'", "''")
rows = []
for n in [100_000, 1_000_000]:
    for name, expr in [
        ('scatter', "scatter_chart_html(list(i::DOUBLE),list((i%100)::DOUBLE),'Large')"),
        ('histogram', "histogram_chart_html(list(i::DOUBLE),'Large')"),
    ]:
        times = []
        for _ in range(a.repeats):
            t = time.perf_counter()
            r = subprocess.run(
                [
                    a.duckdb,
                    '-unsigned',
                    '-json',
                    '-c',
                    f"LOAD '{ext}'; SELECT length({expr}) AS bytes FROM range({n}) t(i);",
                ],
                text=True,
                capture_output=True,
                check=True,
            )
            times.append(time.perf_counter() - t)
        rows.append(
            dict(
                chart=name,
                points=n,
                seconds=times,
                median_seconds=statistics.median(times),
                html_bytes=json.loads(r.stdout)[0]['bytes'],
            )
        )
result = dict(
    platform=platform.platform(),
    processor=platform.machine(),
    duckdb=subprocess.check_output([a.duckdb, '--version'], text=True).strip(),
    scope='CLI startup + SQL aggregation + HTML generation; no browser rendering',
    measurements=rows,
)
Path(a.output).write_text(json.dumps(result, indent=2) + '\n')
print(json.dumps(result, indent=2))
