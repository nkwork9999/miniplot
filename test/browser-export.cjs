const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const {PNG} = require('pngjs');
const {PDFDocument} = require('pdf-lib');
const {render} = require('../scripts/export-chart.cjs');
(async () => {
  const dir = path.resolve(process.argv[2] || 'build/validation/charts');
  for(const name of ['pie','donut','horizontal_bar','violin','bubble','heatmap','contour','step','radar','scatter_gl']) {
    const output = path.join(dir, name+'.png');
    await render(path.join(dir,name+'.html'), output);
    const png = PNG.sync.read(await fs.readFile(output));
    assert.equal(png.width,1200);assert(png.height>=600);
    const colors = new Set();
    for(let i=0;i<png.data.length;i+=4) colors.add(png.data.readUInt32BE(i));
    assert(colors.size>30,name+' rendered blank');
  }
  const png = PNG.sync.read(await fs.readFile('build/validation/export/report.png'));
  assert.equal(png.width,1200);assert(png.height>1000,'report sections clipped');
  const pdf = await PDFDocument.load(await fs.readFile('build/validation/export/report.pdf'));
  assert(pdf.getPageCount()>=1 && pdf.getPageCount()<=4);
  console.log('10 real Chromium chart renders + SQL report PNG/PDF verified');
})().catch(e=>{console.error(e);process.exitCode=1});
