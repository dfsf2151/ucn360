#!/usr/bin/env node
/* Minimal CDP driver: eval <expr> in the probe page, print JSON result.
Usage:
  node cdp_eval.js "expression"        # evaluate, await promises, print result
Environment:
  CDP_PORT (default 9222), CDP_URL (default probe index)
*/
const port = process.env.CDP_PORT || 9222;

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
    ws.onerror = (e) => reject(new Error('ws error'));
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
  const expr = process.argv[2] || '1';
  const t = await getTarget();
  const c = await connect(t.webSocketDebuggerUrl);
  try {
    await c.send('Runtime.enable', {});
    const r = await c.send('Runtime.evaluate', {
      expression: expr,
      returnByValue: true,
      awaitPromise: true,
      timeout: 120000,
    });
    if (r.exceptionDetails) {
      console.error('EXCEPTION:', JSON.stringify(r.exceptionDetails, null, 2));
      process.exitCode = 1;
    } else {
      const v = r.result.value;
      if (typeof v === 'string' && (v.startsWith('{') || v.startsWith('['))) process.stdout.write(v);
      else console.log(JSON.stringify(v, null, 1));
    }
  } finally {
    c.close();
  }
})().catch(e => { console.error('ERR', e); process.exit(1); });
