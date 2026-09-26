'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const vm = require('node:vm');
let now = 10000;
const elements = new Map();
const context2d = new Proxy({}, { get: () => () => {} });
function element() {
  return { textContent: '', innerHTML: '', style: {}, dataset: {}, clientWidth: 800, clientHeight: 200,
    classList: { add() {}, remove() {}, toggle() {}, contains() { return false; } },
    setAttribute() {}, addEventListener() {}, getAttribute() { return ''; }, contains() { return false; },
    querySelector() { return element(); }, getContext() { return context2d; },
    getBoundingClientRect() { return { width: 800, height: 200 }; } };
}
class Clock extends Date { constructor(...args) { super(...(args.length ? args : [now])); } static now() { return now; } }
const sandbox = { console, Date: Clock, Float32Array, URL, URLSearchParams,
  document: { hidden: false, activeElement: null, getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); },
    querySelectorAll() { return []; }, addEventListener() {} },
  window: { matchMedia() { return { matches: false }; }, addEventListener() {}, location: { search: '' }, devicePixelRatio: 1 },
  setTimeout() { return 1; }, clearTimeout() {}, setInterval() {}, requestAnimationFrame() {},
};
vm.createContext(sandbox);
vm.runInContext(fs.readFileSync('web/app.js', 'utf8'), sandbox);
const run = code => vm.runInContext(code, sandbox);
const message = object => run(`handleDeviceMessage({data:${JSON.stringify(JSON.stringify({v: 1, ...object}))}})`);
run("state.wsUrl='ws://127.0.0.1/ws'; state.connectedAt=10000;");
message({type: 'hello', device_id: 'unit', firmware: '0.2.0', session_id: 'one', source: 'serial'});
message({type: 'samples', seq: 0, samples: [-32768, 0, 32767], sample_rate_hz: 248});
assert.equal(run('state.mode'), 'live');
const count = run('state.receivedMessages');
now += 5000;
message({type: 'metrics', seq: 1});
assert.equal(run('state.receivedMessages'), count);
assert.equal(run('state.lastMessageAt'), 10000);
message({type: 'status', seq: 2, connected: true, received_rate_hz: 248});
assert.equal(run('state.mode'), 'stale');
message({type: 'samples', seq: 3, samples: [true]});
assert.equal(run('state.sampleCount'), 3);
message({type: 'samples', seq: 3, samples: [1.5]});
assert.equal(run('state.sampleCount'), 3);
message({type: 'metrics', seq: 4, attention: 60, poor_signal: 0});
now += 4000;
run('updateClockAndDuration()');
assert.equal(run('state.attention'), null);
message({type: 'hello', device_id: 'recording', session_id: 'two', source: 'replay'});
message({type: 'samples', seq: 0, samples: [10], sample_rate_hz: 250});
assert.equal(run('state.mode'), 'replay');
assert.equal(run('state.sampleCount'), 1);
message({type: 'samples', seq: 0, samples: [20]});
assert.equal(run('state.sampleCount'), 1);
assert.equal(run('state.exportRows[0].source'), 'demo');
assert.equal(run("state.exportRows.find(row => row.deviceId === 'unit').sampleRate"), 248);
assert.equal(run("state.exportRows.find(row => row.deviceId === 'unit').source"), 'device');
assert.equal(run('csvEscape("=1+1")'), "'=1+1");
assert.equal(run('csvEscape(-5)'), '-5');
run('state.exportRows=[]; state.exportWrite=0; state.exportDropped=0; for(let i=0;i<100003;i++) appendExportRows([i],{});');
assert.equal(run('state.exportRows.length'), 100000);
assert.equal(run('orderedExportRows()[0].raw'), 3);
assert.equal(run('orderedExportRows()[99999].raw'), 100002);
assert.equal(run('state.exportDropped'), 3);
console.log('PASS: web freshness, validation, replay, duplicate handling and export provenance');
