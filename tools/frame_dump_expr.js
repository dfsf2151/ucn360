(() => {
  const u = window.__rt.Uc;
  const i = __FRAME__;
  u.Db = i; u.tK();
  const f = u.frame;
  const inst = [];
  const wd = f.wd;
  if (wd && wd.list) for (let k = 0; k < wd.If; k++) {
    const o = wd.list[k]; if (!o) continue;
    inst.push([o.Pi, o.Df, o.gv, o.hv, o.fv, o.BA, o.Xq]);
  }
  const imgs = [];
  const ba = u.ba;
  if (ba && ba.images) for (const it of ba.images) {
    if (!it) continue;
    imgs.push([it.handle, it.wb || 0, it.rd || 0, it.sd || 0, it.width, it.height, (it.vb && it.vb.src) ? String(it.vb.src).split('/').pop() : null]);
  }
  const un = f.un || {};
  let unz = 0; if (f.Un) for (let z = 0; z < f.Un.length; z++) if (f.Un[z]) unz++;
  const sz = v => Array.isArray(v) ? v.length : (v && typeof v === 'object' ? Object.keys(v).length : (typeof v === 'number' || typeof v === 'string' ? v : null));
  const bg = f.Vq ? { pd: f.Vq.pd, uc: f.Vq.uc, img: (typeof f.Vq.img === 'number') ? f.Vq.img : null, mode: f.Vq.mode, va: f.Vq.va, In: f.Vq.In, GA: f.Vq.GA } : null;
  const out = {
    i,
    name: (typeof f.Vz === 'string' && f.Vz.length < 200) ? f.Vz : null,
    nameType: typeof f.Vz,
    w: f.pd, h: f.uc, ir: f.ir, GA: f.GA,
    baKeys: ba ? Object.keys(ba) : null,
    imgN: imgs.length,
    images: imgs,
    UnNonzero: unz,
    instCount: wd ? wd.If : 0,
    instances: inst,
    background: bg,
    evStats: { Xd: un.Xd ? un.Xd.length : 0, ec: un.ec ? un.ec.length : 0, Gv: sz(un.Gv), Kh: sz(un.Kh), n: sz(un.$n), Gg: sz(un.Gg), Uc: sz(un.Uc) },
  };
  return JSON.stringify(out);
})()
