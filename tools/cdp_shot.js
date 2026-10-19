#!/usr/bin/env node
/* CDP screenshot: node cdp_shot.js out.png [waitMs] */
const port = process.env.CDP_PORT || 9222;
const outPath = process.argv[2] || '/tmp/opencode/shot.png';
const waitMs = parseInt(process.argv[3] || '0', 10);

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
      close() { ws.close(); }
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
(async () => {
  const t = await getTarget();
  const c = await connect(t.webSocketDebuggerUrl);
  try {
    await c.send('Page.enable', {});
    if (waitMs) await new Promise(r => setTimeout(r, waitMs));
    const r = await c.send('Page.captureScreenshot', { format: 'png' });
    require('fs').writeFileSync(outPath, Buffer.from(r.data, 'base64'));
    console.log('saved', outPath);
  } finally { c.close(); }
})().catch(e => { console.error('ERR', e.message); process.exit(1); });
