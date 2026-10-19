(() => {
  const u = window.__rt.Uc;
  const defs = u.Tc.yc;

  function scal(v) {
    const t = typeof v;
    if (v === null || v === undefined) return null;
    if (t === 'number' || t === 'string' || t === 'boolean') return v;
    return undefined;
  }
  function snap(o, depth, seen) {
    if (o === null || o === undefined) return null;
    const t = typeof o;
    if (t === 'number' || t === 'string' || t === 'boolean') return o;
    if (depth <= 0) return typeof o;
    if (Array.isArray(o)) {
      const out = [];
      const n = Math.min(o.length, 4096);
      for (let i = 0; i < n; i++) out.push(snap(o[i], depth - 1));
      return out;
    }
    if (t === 'object') {
      const out = {};
      const keys = Object.keys(o);
      for (const k of keys.slice(0, 64)) {
        const v = o[k];
        const s = scal(v);
        if (s !== undefined) { out[k] = s; continue; }
        if (typeof v === 'object') out[k] = snap(v, depth - 1);
      }
      return out;
    }
    return null; // functions
  }

  function serAnim(La) {
    if (!La || !La.nf) return null;
    const anims = [];
    for (let a = 0; a < La.mf; a++) {
      const w = La.nf[a];
      if (!w) { anims.push(null); continue; }
      const dirs = [];
      for (let d = 0; d < 32; d++) {
        const fd = w.Rd && w.Rd[d];
        if (!fd) continue;
        dirs.push({
          d,
          minSpeed: fd.Jt,
          maxSpeed: fd.It,
          repeat: fd.vy,
          backTo: fd.wy,
          frames: Array.prototype.slice.call(fd.Yj || []),
        });
      }
      anims.push(dirs);
    }
    return { count: La.mf, anims };
  }

  const START = __START__, END = __END__;
  const out = [];
  for (let i = START; i < Math.min(END, defs.length); i++) {
    const d = defs[i];
    if (!d) { out.push(null); continue; }
    const oc = d.oc;
    const rec = {
      i,
      handle: d.mo,
      type: d.Ee,
      flags: d.Rh,
      name: d.Rv,
    };
    if (oc) {
      rec.Vi = oc.Vi;
      rec.xc = oc.xc;
      rec.Ag = oc.Ag;
      rec.Mv = snap(oc.Mv, 1);
      rec.values = oc.dm ? { count: oc.dm.sr, values: Array.prototype.slice.call(oc.dm.values || []), Ya: oc.dm.Ya } : null;
      rec.strings = oc.lo ? { count: oc.lo.rr, list: Array.prototype.slice.call(oc.lo.$a || []) } : null;
      rec.anim = serAnim(oc.Ti);
      rec.Wc = oc.Wc ? snap(oc.Wc, 2) : null;
      rec.counter = oc.jb ? snap(oc.jb, 2) : null;
      rec.movements = oc.$e ? snap(oc.$e, 2) : null;
      rec.sound1 = oc.cm ? snap(oc.cm, 1) : null;
      rec.sound2 = oc.io ? snap(oc.io, 1) : null;
      rec.DB = oc.DB; rec.GB = oc.GB; rec.FB = oc.FB;
    }
    out.push(rec);
  }
  return out;
})()
