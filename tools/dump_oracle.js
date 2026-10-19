(() => {
  const u = window.__rt.Uc;
  const MODE = "__MODE__";
  const FRAME = __FRAME__;
  const MAX_ARR = 8192, MAX_KEYS = 128;

  function snap(v, depth, seen) {
    const t = typeof v;
    if (v === null || v === undefined) return null;
    if (t === 'number' || t === 'string' || t === 'boolean') return v;
    if (t === 'function') return 'fn:' + (v.name || '?');
    if (depth <= 0) return '$' + t;
    if (seen.has(v)) return '[cycle]';
    seen.add(v);
    try {
      if (Array.isArray(v)) {
        const out = [];
        const n = Math.min(v.length, MAX_ARR);
        for (let i = 0; i < n; i++) out.push(snap(v[i], depth - 1, seen));
        if (v.length > n) out.push('$more:' + (v.length - n));
        return out;
      }
      const out = {};
      const ctor = v.constructor && v.constructor.name;
      if (ctor && ctor !== 'Object') out.$ctor = ctor;
      const keys = Object.getOwnPropertyNames(v);
      for (const k of keys.slice(0, MAX_KEYS)) {
        try { out[k] = snap(v[k], depth - 1, seen); }
        catch (e) { out[k] = '$error:' + (e && e.message ? e.message : '?'); }
      }
      if (keys.length > MAX_KEYS) out.$more = keys.length - MAX_KEYS;
      return out;
    } finally {
      seen.delete(v);
    }
  }

  const load = () => { if (u.Db !== FRAME) { u.Db = FRAME; u.tK(); } };

  if (MODE === 'objects') {
    // Object common data (oc) is not loaded yet; the app's own ccj reader can fill each def.
    const defs = u.Tc.yc;
    const file = u.file;
    const out = [];
    for (let i = 0; i < defs.length; i++) {
      const d = defs[i];
      if (!d) { out.push(null); continue; }
      if (file && d.load && !d.oc) { try { d.load(file); } catch (e) { /* leave as is */ } }
      out.push(snap(d, 10, new Set()));
    }
    return JSON.stringify({ count: defs.length, objects: out });
  }

  load();
  const f = u.frame;

  if (MODE === 'frame') {
    const wd = f.wd || {};
    const list = wd.list || [];
    const inst = [];
    for (let k = 0; k < list.length; k++) {
      const o = list[k];
      if (!o) { inst.push(null); continue; }
      inst.push({
        named: { order: o.Pi, object: o.Df, x: o.gv, y: o.hv, fv: o.fv, BA: o.BA, Xq: o.Xq },
        raw: snap(o, 2, new Set()),
      });
    }
    const imgs = [];
    const ims = (u.ba && u.ba.images) || [];
    for (const it of ims) {
      if (!it) continue;
      const shallow = Object.assign({}, it);
      delete shallow.app; delete shallow.vb; delete shallow.wg; delete shallow.Tl; delete shallow.Hh;
      imgs.push({
        handle: it.handle, atlas: it.wb, crop_x: it.rd, crop_y: it.sd,
        w: it.width, h: it.height, hotspot_x: it.Zg, hotspot_y: it.$g,
        Ga: it.Ga, Ca: it.Ca, Sa: it.Sa, raw: snap(shallow, 2, new Set()),
      });
    }
    const un = f.Un || [];
    const unUsed = [];
    for (let h = 0; h < un.length; h++) if (un[h]) unUsed.push([h, un[h]]);
    return JSON.stringify({
      i: FRAME,
      instanceCount: wd.If,
      instances: inst,
      images: imgs,
      unUsed,
      meta: snap(f, 2, new Set()),
    });
  }

  if (MODE === 'program') {
    const un = f.un || {};
    const groups = (un.Xd || []).map((g, gi) => ({ gi, raw: snap(g, 8, new Set()) }));
    const summary = {}, samples = {};
    for (const k of Object.keys(un)) {
      const v = un[k];
      if (k === 'Uc' || k === 'l') { summary[k] = 'skipped (app/loader object)'; continue; }
      if (Array.isArray(v)) {
        summary[k] = v.length;
        samples[k] = v.slice(0, 4).map(x => snap(x, 4, new Set()));
      } else if (v && typeof v === 'object') {
        summary[k] = Object.keys(v).length;
        samples[k] = snap(v, 3, new Set());
      } else summary[k] = v;
    }
    return JSON.stringify({ i: FRAME, groupCount: groups.length, groups, summary, samples });
  }

  return JSON.stringify({ error: 'unknown mode ' + MODE });
})()
