#!/usr/bin/env node
/* CDP reference capture: freeze a game frame, wait for it to settle, save the canvas.
 *
 *   node tools/cdp_frame_ref.js FRAME out.png [out.json]
 *
 * Findings that shaped this tool (2026-10-18):
 *  - the page sizes its canvas to the viewport (1920x993 in this headless window),
 *    so we force an emulated viewport at the frame's own logical size first;
 *  - Page.captureScreenshot returns a black canvas here, so the pixels are taken
 *    with drawImage inside a rAF callback (same-frame read of the WebGL buffer);
 *  - switching frames shows the game's loading bar, so the tool polls a coarse
 *    pixel signature until it stops changing (or 30 s pass).
 */
const fs = require('fs');
const port = process.env.CDP_PORT || 9222;
const frameIndex = parseInt(process.argv[2] || '0', 10);
const outPng = process.argv[3] || `/tmp/opencode/frame_${frameIndex}.png`;
const outJson = process.argv[4] || null;

async function getTarget() {
  const res = await fetch(`http://127.0.0.1:${port}/json/list`);
  const list = await res.json();
  const pages = list.filter(t => t.type === 'page' && t.url.includes('8765'));
  if (!pages.length) throw new Error('no page target');
  return pages[0];
}

function connect(wsUrl) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(wsUrl);
    const pending = new Map();
    let nextId = 1;
    ws.onopen = () => resolve({
      send(method, params) {
        return new Promise((res2, rej2) => {
          const id = nextId++;
          pending.set(id, { res2, rej2 });
          ws.send(JSON.stringify({ id, method, params }));
        });
      },
      close() { ws.close(); },
    });
    ws.onerror = () => reject(new Error('ws error'));
    ws.onmessage = (ev) => {
      const msg = JSON.parse(ev.data);
      if (msg.id && pending.has(msg.id)) {
        const { res2, rej2 } = pending.get(msg.id);
        pending.delete(msg.id);
        if (msg.error) rej2(new Error(JSON.stringify(msg.error)));
        else res2(msg.result);
      }
    };
  });
}

const sleep = (ms) => new Promise(r => setTimeout(r, ms));

async function evalOn(c, expression) {
  const r = await c.send('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true, timeout: 120000 });
  if (r.exceptionDetails) throw new Error('page exception: ' + JSON.stringify(r.exceptionDetails.exception || r.exceptionDetails));
  return r.result.value;
}

const FREEZE = (frame) => `(() => {
  const u = window.__rt.Uc;
  if (u.Db !== ${frame}) { u.Db = ${frame}; u.tK(); }
  return JSON.stringify({ frame: u.frame.Vz, w: u.frame.pd, h: u.frame.uc });
})()`;

/* One rAF drawImage grab; also returns a coarse signature (every 37th pixel summed)
 * and the game's current frame index/name (the boot sequence can advance frames). */
const GRAB = `(async () => {
  const c = document.querySelector('canvas');
  const off = document.createElement('canvas');
  off.width = c.width; off.height = c.height;
  const ctx = off.getContext('2d');
  const url = await new Promise(res => requestAnimationFrame(() => {
    ctx.drawImage(c, 0, 0);
    res(off.toDataURL('image/png'));
  }));
  const d = ctx.getImageData(0, 0, off.width, off.height).data;
  let sig = 0, nonblack = 0;
  for (let i = 0; i < d.length; i += 4 * 37) { sig = (sig + d[i] + d[i+1] * 3 + d[i+2] * 7) % 1000000007; if (d[i] + d[i+1] + d[i+2] > 24) nonblack++; }
  const u = window.__rt.Uc;
  return JSON.stringify({ canvas: [c.width, c.height], sig, nonblack, url, db: u.Db, name: u.frame.Vz });
})()`;

(async () => {
  const t = await getTarget();
  const c = await connect(t.webSocketDebuggerUrl);
  try {
    await c.send('Page.enable', {});
    const info = JSON.parse(await evalOn(c, FREEZE(frameIndex)));
    console.log(`freeze -> frame ${JSON.stringify(info.frame)} logical ${info.w}x${info.h}`);
    await c.send('Emulation.setDeviceMetricsOverride', {
      width: info.w, height: info.h, deviceScaleFactor: 1, mobile: false,
    });

    // Poll until the pixels stop changing (loading bars / boot animation), re-freezing
    // whenever the game's own sequence navigates away from the requested frame.
    let prev = null, stable = 0, last = null, refreezes = 0;
    for (let i = 0; i < 45; i++) {
      await sleep(700);
      const s = JSON.parse(await evalOn(c, GRAB));
      last = s;
      if (s.db !== frameIndex || s.name !== info.frame) {
        if (refreezes < 3) { refreezes++; await evalOn(c, FREEZE(frameIndex)); console.log(`re-freeze (game moved to ${s.name})`); }
        stable = 0; prev = null;
        continue;
      }
      if (prev && s.canvas[0] === info.w && s.canvas[1] === info.h && Math.abs(s.sig - prev.sig) < 200) stable++;
      else stable = 0;
      prev = s;
      if (stable >= 2) break;
    }
    if (!last) throw new Error('no grab');
    if (stable < 2) console.log('warning: pixels never settled — capturing the last state');
    if (last.canvas[0] !== info.w || last.canvas[1] !== info.h)
      console.log(`note: canvas is ${last.canvas.join('x')} (frame is ${info.w}x${info.h})`);
    const url = last.url.split(',', 2)[1];
    fs.writeFileSync(outPng, Buffer.from(url, 'base64'));
    const record = {
      frame: frameIndex, frame_name: info.frame, frame_size: [info.w, info.h],
      canvas: last.canvas, nonblack: last.nonblack, captured: new Date().toISOString(),
    };
    if (outJson) fs.writeFileSync(outJson, JSON.stringify(record, null, 1));
    console.log(`saved ${outPng} (${last.canvas.join('x')}, nonblack ${last.nonblack})`);
  } finally {
    c.close();
  }
})().catch(e => { console.error('ERR', e.message); process.exit(1); });
