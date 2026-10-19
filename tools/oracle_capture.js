/* Per-tick state record for tools/cdp_tick_oracle.js; game_host writes the same shape (tools/compare_state.py).
 *
 * One record per game tick:
 *   { key, running, t: run.Lb, f: frame index, rf: frame timer ms, seed: LCG state after the tick,
 *     o: [[slot, oi, x, y, image, anim, anim_frame, dir, sprite_flags, values, counter, text], ...] }
 * values is {index: value} for the non-zero alterable values; counter / text are null unless the object is one.
 * Field names are the runtime's minified ones (see docs/runtime-map.md).
 */
window.__capture = function () {
  const u = window.__rt.Uc, r = u.G;
  if (!r || !r.H) return { key: null, running: false };
  const o = [];
  for (let i = 0; i < r.H.length; i++) {
    const h = r.H[i];
    if (!h) continue;
    let values = null;
    if (h.T && h.T.Va) {
      for (let k = 0; k < h.T.Va.length; k++) {
        if (h.T.Va[k]) (values = values || {})[k] = h.T.Va[k];
      }
    }
    o.push([
      h.Sb, h.Ue, h.v, h.u,
      h.b ? h.b.Ha : -1,
      h.X ? h.X.Xh : -1, h.X ? h.X.Pf : -1,
      h.b ? h.b.Ua : -1,
      h.D ? h.D.M : -1,
      values,
      h.Ea === 7 ? h.pa : null,
      h.Ea === 3 ? (h.ei === undefined ? null : h.ei) : null,
    ]);
  }
  return { key: r.Lb + ':' + u.Db, running: true, t: r.Lb, f: u.Db, rf: r.Rf, seed: r.Dw, o };
};
