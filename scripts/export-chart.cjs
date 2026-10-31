#!/usr/bin/env node
// Optional native exporter. No shell interpolation or network access is used.
const fs = require('node:fs/promises');
const path = require('node:path');
const {chromium} = require('playwright');
const parse5 = require('parse5');

async function render(input, output) {
  const extension = path.extname(output).toLowerCase();
  if (!['.png', '.pdf'].includes(extension)) throw new Error('Output must be PNG or PDF');
  const html = await fs.readFile(input, 'utf8');
  const errors = [];
  const nodes = [];
  const doc = parse5.parse(html, {onParseError: e => errors.push(e.code)});
  function walk(n) {nodes.push(n); for (const c of n.childNodes || []) walk(c);}
  walk(doc);
  if (errors.length || !nodes.some(n => n.tagName === 'meta' && n.attrs?.some(a => a.name === 'name' && a.value === 'generator') && n.attrs?.some(a => a.name === 'content' && a.value === 'miniplot')))
    throw new Error('Expected valid Miniplot HTML; parse errors: ' + errors.join(', '));
  const plotly = await fs.readFile(require.resolve('plotly.js-dist-min'), 'utf8');
  const browser = await chromium.launch({headless:true});
  const temporary = output + '.tmp-' + process.pid;
  const timeout = setTimeout(() => {browser.close().finally(() => process.exit(1));}, 60000);
  try {
    const page = await browser.newPage({viewport:{width:1200,height:800},deviceScaleFactor:1});
    const pageErrors = [];
    page.on('pageerror', e => pageErrors.push(e.message));
    await page.route('**/*', route => {
      if (route.request().url() === 'https://cdn.plot.ly/plotly-2.27.0.min.js') {
        return route.fulfill({contentType:'text/javascript',body:plotly + `\nwindow.__miniplotPlots=[];const originalPlot=Plotly.newPlot;Plotly.newPlot=function(...args){const p=originalPlot.apply(this,args);window.__miniplotPlots.push(p);return p;};`});
      }
      return route.abort();
    });
    await page.setContent(html, {waitUntil:'load',timeout:30000});
    await page.evaluate(async () => {await Promise.all(window.__miniplotPlots || []);await document.fonts.ready;});
    if (pageErrors.length) throw new Error(pageErrors.join('\n'));
    const charts = await page.locator('.js-plotly-plot').count();
    const expected = await page.locator('#chart, .chart').count();
    if (charts !== expected) throw new Error('Chart rendering did not complete');
    await page.emulateMedia({media:'screen'});
    if (extension === '.png') await page.screenshot({path:temporary,type:'png',fullPage:true,animations:'disabled'});
    else await page.pdf({path:temporary,format:'A4',printBackground:true,margin:{top:'12mm',bottom:'12mm',left:'12mm',right:'12mm'}});
    await fs.rename(temporary, output);
  } finally {
    clearTimeout(timeout);
    await fs.rm(temporary, {force:true});
    await browser.close();
  }
}
module.exports = {render};
if (require.main === module) {
  if (process.argv.length !== 4) {console.error('Usage: node scripts/export-chart.cjs INPUT.html OUTPUT.png|pdf');process.exit(2);}
  render(process.argv[2],process.argv[3]).catch(e => {console.error(e.message);process.exitCode=1;});
}
