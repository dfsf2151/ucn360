#!/usr/bin/env node
/* CDP key dispatch: node cdp_key.js <key> [code] [keyCode]
   e.g. node cdp_key.js Enter Enter 13 ; node cdp_key.js " " Space 32 */
const port = process.env.CDP_PORT || 9222;
const key = process.argv[2] || 'Enter';
const code = process.argv[3] || key;
const keyCode = parseInt(process.argv[4] || '0', 10) || (key === 'Enter' ? 13 : key === ' ' ? 32 : 0);

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
    const base = { key, code, windowsVirtualKeyCode: keyCode, nativeVirtualKeyCode: keyCode, text: key === 'Enter' ? '\r' : key };
    await c.send('Input.dispatchKeyEvent', { type: 'keyDown', ...base });
    await c.send('Input.dispatchKeyEvent', { type: 'keyUp', ...base });
    console.log('sent', key);
  } finally { c.close(); }
})().catch(e => { console.error('ERR', e.message); process.exit(1); });
