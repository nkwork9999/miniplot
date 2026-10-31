#!/usr/bin/env python3
"""Keep all one million WebGL points; validate output at the JavaScript boundary."""

import argparse, subprocess, tempfile
from pathlib import Path

p = argparse.ArgumentParser()
p.add_argument('--duckdb', required=True)
p.add_argument('--extension', required=True)
a = p.parse_args()


def lit(s):
    return "'" + s.replace("'", "''") + "'"


with tempfile.TemporaryDirectory() as d:
    f = Path(d) / 'million.html'
    q = (
        'LOAD '
        + lit(str(Path(a.extension).resolve()))
        + "; SELECT scatter_gl_chart(list(i::DOUBLE/10),list((i%100)::DOUBLE),'Million',"
        + lit(str(f))
        + ') FROM range(1000000) t(i);'
    )
    subprocess.run([a.duckdb, '-unsigned', '-c', q], check=True, capture_output=True, timeout=60)
    script = r'''
 const fs=require('node:fs'),vm=require('node:vm'),assert=require('node:assert/strict');
 const html=fs.readFileSync(process.argv[1],'utf8');let got;
 const script=html.match(/<script>([\s\S]*?)<\/script>/)[1];
 vm.runInNewContext(script,{Plotly:{newPlot:(_id,data)=>got=data}}, {timeout:10000});
 assert.equal(got[0].type,'scattergl');assert.equal(got[0].x.length,1000000);assert.equal(got[0].y.length,1000000);
 for(const i of [0,1,500001,999999]) {assert.equal(got[0].x[i],i/10);assert.equal(got[0].y[i],i%100);}
 console.log('One million points retained, endpoints and precision verified');
 '''
    subprocess.run(['node', '-e', script, str(f)], check=True, timeout=30)
