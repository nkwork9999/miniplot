// Measures SQL + real headless Chromium PNG rendering for large WebGL plots.
const {execFileSync}=require('node:child_process');
const {performance}=require('node:perf_hooks');
const fs=require('node:fs/promises'),path=require('node:path');
const {render}=require('../scripts/export-chart.cjs');
(async()=>{
 const [cli,extension]=process.argv.slice(2),rows=[];
 const dir=path.resolve('build/validation/large');await fs.mkdir(dir,{recursive:true});
 const lit=s=>"'"+s.replaceAll("'","''")+"'";
 for(const n of [100000,1000000]){
  const input=path.join(dir,n+'.html'),output=path.join(dir,n+'.png');
  const start=performance.now();
  execFileSync(cli,['-unsigned','-c',`LOAD ${lit(path.resolve(extension))}; SELECT scatter_gl_chart(list(i::DOUBLE),list((i%100)::DOUBLE),'${n} points',${lit(input)}) FROM range(${n}) t(i);`]);
  const sql=(performance.now()-start)/1000,t=performance.now();
  try {
    await render(input,output);
    rows.push({points:n,sql_seconds:sql,chromium_png_seconds:(performance.now()-t)/1000,png_bytes:(await fs.stat(output)).size,status:'success'});
  } catch(e) {
    rows.push({points:n,sql_seconds:sql,chromium_png_seconds:(performance.now()-t)/1000,status:'failed',error:e.message});
  }
 }
 await fs.writeFile('build/validation/render-benchmark.json',JSON.stringify({scope:'One run per size; includes browser launch, local Plotly load and full-page PNG export; hardware/software dependent',measurements:rows},null,2)+'\n');
 console.log(rows);
})().catch(e=>{console.error(e);process.exitCode=1});
