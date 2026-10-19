#!/usr/bin/env node
/* Deterministic tick oracle: run the live game under a fake clock and record its state every tick.
 *
 *   node tools/cdp_tick_oracle.js --ticks N [--frame F] [--out states.jsonl] [--png TICK:out.png] [--seed S]
 *                                 [--eval EXPR] [--keep]
 *
 * Opens a fresh tab on the probe page (http://127.0.0.1:8765, headless Chrome with CDP on 9222) with
 * tools/oracle_shim.js installed before any page script, so time only moves when we step it: 1000/60 ms per
 * tick (16/17/17 ms, see LOAD_MS). Loading needs real network turns, so the driver yields to the event loop
 * between steps; the game's own tick counter (run.Lb) decides what a tick is, which keeps loading time out of
 * the record. Runs are bit-identical: two 600-tick recordings of frame 0 compare equal.
 *
 * Once the first frame runs, the driver restarts it or jumps to --frame F, with the LCG seed forced to --seed
 * (default 12345) at every frame init. Recording starts at the target frame's first tick and follows the game
 * through any frame changes it makes on its own.
 * Each record is tools/oracle_capture.js's __capture() for one game tick. --eval prints EXPR's value after the
 * ticks (exploration). --keep leaves the tab open.
 */
const fs = require('fs');
const path = require('path');
const port = process.env.CDP_PORT || 9222;
// Integer ms per step, so floor() never sees a fraction: loading steps take 16, and game tick t takes
// [16, 17, 17][(t - 1) % 3], which puts the frame timer at exactly floor(t * 1000 / 60) on every tick.
const LOAD_MS = 16;
const tickMs = (t) => [16, 17, 17][(t - 1) % 3];

function arg(name, def) {
  const i = process.argv.indexOf(name);
  return i < 0 ? def : (process.argv[i + 1] === undefined ? true : process.argv[i + 1]);
}
const ticks = parseInt(arg('--ticks', '60'), 10);
const jumpFrame = arg('--frame', null);
const outFile = arg('--out', null);
const evalExpr = arg('--eval', null);
const keep = process.argv.includes('--keep');
const seed = parseInt(arg('--seed', '12345'), 10) & 65535;
// --png TICK:out.png saves the canvas as drawn for that tick (grabbed in the same task as the draw).
const pngArg = arg('--png', null);
const pngTick = pngArg ? parseInt(pngArg.split(':')[0], 10) : -1;
const pngFile = pngArg ? pngArg.slice(pngArg.indexOf(':') + 1) : null;
// --input "T:down:x:y,T:up,T:key:code:1|0": applied just before game tick T, as the browser's handlers would
// (window coordinates; mouse button 0). game_host takes the same string.
const inputs = {};
for (const ev of (arg('--input', '') || '').split(',').filter(Boolean)) {
  const [t, ...rest] = ev.split(':');
  (inputs[+t] = inputs[+t] || []).push(rest);
}
const inputJs = (evs) => evs.map(([kind, a, b]) => kind === 'down'
  ? `u.Gf = ${+a}; u.Hf = ${+b}; u.Jc[200] = true; u.sk = true; u.G.i.po(0, 1);`
  : kind === 'up' ? `u.Jc[200] = false;`
  : `u.Jc[${+a}] = ${b === '1'};`).join(' ');

function connect(wsUrl) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    const pending = new Map();
    let nextId = 1;
    ws.onopen = () => resolve({
      send(method, params = {}) {
        return new Promise((res, rej) => {
          const id = nextId++;
          pending.set(id, { res, rej });
          ws.send(JSON.stringify({ id, method, params }));
        });
      },
      close() { ws.close(); },
    });
    ws.onerror = () => reject(new Error('ws error ' + wsUrl));
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) {
        const { res, rej } = pending.get(msg.id);
        pending.delete(msg.id);
        msg.error ? rej(new Error(JSON.stringify(msg.error))) : res(msg.result);
      }
    };
  });
}

(async () => {
  const shim = fs.readFileSync(path.join(__dirname, 'oracle_shim.js'), 'utf8');
  const capture = fs.readFileSync(path.join(__dirname, 'oracle_capture.js'), 'utf8');
  const target = await (await fetch(`http://127.0.0.1:${port}/json/new?about:blank`, { method: 'PUT' })).json();
  const c = await connect(target.webSocketDebuggerUrl);
  const ev = async (expression) => {
    const r = await c.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, timeout: 120000 });
    if (r.exceptionDetails) throw new Error('page: ' + JSON.stringify(r.exceptionDetails).slice(0, 2000));
    return r.result.value;
  };
  try {
    await c.send('Page.enable');
    await c.send('Runtime.enable');
    await c.send('Emulation.setDeviceMetricsOverride', { width: 1920, height: 1080, deviceScaleFactor: 1, mobile: false });
    await c.send('Page.addScriptToEvaluateOnNewDocument', { source: shim });
    await c.send('Page.navigate', { url: 'http://127.0.0.1:8765/index.html' });

    // Boot: step until the run's tick counter moves (the first frame is loaded and running).
    const run = '(window.__rt && window.__rt.Uc && window.__rt.Uc.G)';
    let boot = 0;
    for (;; boot++) {
      const lb = await ev(`(() => { if (!window.__clock) return -2; __clock.step(${LOAD_MS}); const r = ${run}; return r ? r.Lb : -1; })()`);
      if (lb >= 1) break;
      if (boot > 20000) throw new Error('boot never reached tick 1: ' + JSON.stringify(await ev('window.__probeLog')));
      await ev('__clock.yieldReal()');
    }
    await ev(capture);
    process.stderr.write(`booted after ${boot} steps, clock ${await ev('__clock.now()')} ms\n`);

    // Restart the frame (or jump) with the LCG seed forced, so the record starts at a frame's first tick from a
    // known state whatever the loading took. Mt is the run's per-frame init, where the runtime seeds Dw.
    // Getting to the frame: a jump goes through the runtime's own end-of-frame path (what the "jump to frame"
    // action does: zb = 3, Dm = 0x8000 | index), which loads the frame's assets. Right after a load the browser
    // loop has a loading-phase setTimeout and the rAF chain both pending, so under the fake clock the frame's first
    // step runs two ticks at one time (a scheduling artifact). So once the frame ticks, it is restarted with tK,
    // like a restart of the current frame: assets are cached, the start is a single tick. The save store is
    // cleared first so the recorded run starts from the same empty Ini data as the engine.
    const current = await ev('window.__rt.Uc.Db');
    const want = jumpFrame === null ? current : parseInt(jumpFrame, 10);
    await ev(`(() => {
      const u = window.__rt.Uc, proto = Object.getPrototypeOf(u.G), mt = proto.Mt;
      proto.Mt = function () { const v = mt.apply(this, arguments); this.Dw = ${seed}; return v; };
    })()`);
    if (want !== current) {
      await ev(`(() => { const u = window.__rt.Uc; u.G.zb = 3; u.G.Dm = 32768 | ${want}; })()`);
      let loadedSteps = 0;
      for (let n = 0; ; n++) {
        const st = await ev(`(() => { __clock.step(${LOAD_MS}); const u = window.__rt.Uc; return [u.Db, u.G ? u.G.Lb : -1, u.ug]; })()`);
        // ticking; or, for a frame that leaves on its first tick (13), loaded for a while
        if (st[0] === want && st[1] >= 1) break;
        if (st[0] === want && !st[2] && ++loadedSteps > 600) break;
        if (n > 20000) throw new Error('jump never reached frame ' + want);
        await ev('__clock.yieldReal()');
      }
    }
    // tK reuses the frame object, whose layer containers would keep the previous run's sprites on screen (drawn,
    // though no longer in the run): empty them so the restarted frame draws only its own objects.
    await ev(`(() => {
      try { localStorage.clear(); } catch (e) {}
      const u = window.__rt.Uc;
      for (const l of u.frame.pb) { l.gc.children.length = 0; l.Ib.children.length = 0; l.Pa.children.length = 0; }
      u.tK();
    })()`);

    const out = outFile ? fs.openSync(outFile, 'w') : null;
    let recorded = 0, lastKey = null, steps = 0, lastT = 0, running = false, started = false;
    while (recorded < ticks) {
      const ms = running ? tickMs(lastT + 1) : LOAD_MS;
      const grab = started && lastT + 1 === pngTick;
      const pre = started && inputs[lastT + 1] ? inputJs(inputs[lastT + 1]) : '';
      const res = await ev(`(() => {
        { const u = window.__rt.Uc; ${pre} }
        __clock.step(${ms});
        const rec = __capture();
        if (${grab}) {
          const c = document.getElementById('MMFCanvas'), t = document.createElement('canvas');
          t.width = c.width; t.height = c.height;
          t.getContext('2d').drawImage(c, 0, 0);
          rec.png = t.toDataURL('image/png');
        }
        return rec;
      })()`);
      const rec = res;
      if (rec && rec.png) {
        fs.writeFileSync(pngFile, Buffer.from(rec.png.split(',')[1], 'base64'));
        process.stderr.write(`tick ${rec.t}: canvas -> ${pngFile}\n`);
        delete rec.png;
      }
      // The game ticked when its counter moved; tick 1 of a frame can run while the app still flags loading.
      running = started || !!(rec && rec.f === want && rec.t === 1);
      if (running && rec) lastT = rec.t;
      steps++;
      if (process.env.ORACLE_TRACE && steps <= +process.env.ORACLE_TRACE)
        process.stderr.write(`step ${steps} ms ${ms} t ${rec && rec.t} f ${rec && rec.f} rf ${rec && rec.rf} ug ${await ev('window.__rt.Uc.ug')} pending ${JSON.stringify(await ev('__clock.pending()'))}\n`);
      if (rec && rec.f === want && rec.t === 1) started = true;
      if (started && rec && rec.key !== lastKey && rec.t >= 1) {
        lastKey = rec.key;
        recorded++;
        if (out) fs.writeSync(out, JSON.stringify(rec) + '\n');
      } else {
        await ev('__clock.yieldReal()');
      }
      if (steps > ticks * 50 + 20000) throw new Error('stuck: ' + JSON.stringify(rec).slice(0, 500));
    }
    if (out) fs.closeSync(out);
    process.stderr.write(`recorded ${recorded} ticks in ${steps} steps\n`);
    if (evalExpr) {
      const v = await ev(evalExpr);
      process.stdout.write((typeof v === 'string' ? v : JSON.stringify(v, null, 1)) + '\n');
    }
  } finally {
    c.close();
    if (!keep) await fetch(`http://127.0.0.1:${port}/json/close/${target.id}`);
  }
})().catch(e => { console.error('ERR', e.message || e); process.exit(1); });
