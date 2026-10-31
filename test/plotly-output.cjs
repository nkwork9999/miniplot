const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
const parse5 = require('parse5');

// Execute the actual SQL-generated script with a recording Plotly boundary.
// This checks data delivery and escaping; real rendering is tested separately.
const charts = JSON.parse(fs.readFileSync(0, 'utf8'));
for (const chart of charts) {
  const errors = [];
  const doc = parse5.parse(chart.html, {onParseError: e => errors.push(e.code)});
  assert.deepEqual(errors, [], chart.name + ' invalid HTML');
  const nodes = [];
  function walk(n) { nodes.push(n); for(const c of n.childNodes || []) walk(c); }
  walk(doc);
  assert.equal(nodes.filter(n => n.tagName === 'title').length, 1, chart.name);
  assert.equal(nodes.filter(n => n.attrs?.some(a => a.name === 'id' && a.value === 'chart')).length, 1, chart.name);
  assert.equal(nodes.filter(n => n.tagName === 'script').length, 2, chart.name + ' injected script');
  assert.equal(nodes.some(n => n.attrs?.some(a => /^on/i.test(a.name))), false, chart.name);
  const scripts = [...chart.html.matchAll(/<script>([\s\S]*?)<\/script>/g)];
  assert.equal(scripts.length, 1, chart.name);
  let received;
  const context = vm.createContext({
    Plotly: {newPlot: (...args) => { received = args; }},
    window: {addEventListener() {}},
  });
  vm.runInContext(scripts[0][1], context, {timeout: 1000});
  assert.equal(received[0], 'chart');
  const actual = JSON.parse(JSON.stringify(received.slice(1)));
  assert.deepEqual(actual, chart.expected, chart.name);
  assert.equal(vm.runInContext('({}).polluted', context), undefined);
  assert.equal(context.injected, undefined);
}
console.log(`${charts.length} SQL-generated Plotly documents passed JavaScript checks`);
