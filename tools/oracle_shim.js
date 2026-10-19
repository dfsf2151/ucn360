/* Deterministic clock for the probe page, installed before Runtime.js runs (tools/cdp_tick_oracle.js).
 *
 * The runtime reads time only through Date (frame timer Rf, delta Go, the LCG seed Dw = Oc & 65535) and paces
 * itself with requestAnimationFrame / setTimeout. Here time stands still until the driver calls
 * window.__clock.step(ms): the fake clock moves by ms, and the timers and animation frames that are due run
 * once. Math.random is a fixed-seed generator so the few visual-only uses repeat too.
 */
(function () {
  // Each run starts from an empty save: the Ini object keeps its files in localStorage, which the Chrome profile
  // would otherwise carry from one recording to the next.
  try { window.localStorage.clear(); } catch (e) { /* storage blocked: nothing to clear */ }
  const RealDate = Date;
  const realSetTimeout = window.setTimeout.bind(window);
  const BASE = 1700000000000; // fixed epoch: Oc & 65535 = 0x2800 seeds the runtime's LCG the same every run
  let now = BASE;             // ms; the driver steps whole milliseconds, Date reports floor(now)

  class FakeDate extends RealDate {
    constructor(...args) {
      if (args.length === 0) super(Math.floor(now));
      else super(...args);
    }
    static now() { return Math.floor(now); }
  }
  window.Date = FakeDate;
  if (window.performance) window.performance.now = () => now - BASE;

  let seed = 0x9e3779b9;
  Math.random = function () {
    seed = (seed + 0x6d2b79f5) >>> 0;
    let t = seed;
    t = Math.imul(t ^ (t >>> 15), t | 1);
    t ^= t + Math.imul(t ^ (t >>> 7), t | 61);
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296;
  };

  let timers = [];     // {id, due, fn, args}
  let frames = [];     // rAF callbacks
  let nextId = 1;
  window.setTimeout = function (fn, ms, ...args) {
    const id = nextId++;
    timers.push({ id, due: now + Math.max(0, +ms || 0), fn, args });
    return id;
  };
  window.clearTimeout = function (id) { timers = timers.filter(t => t.id !== id); };
  window.requestAnimationFrame = function (fn) { const id = nextId++; frames.push({ id, fn }); return id; };
  window.cancelAnimationFrame = function (id) { frames = frames.filter(f => f.id !== id); };

  // Audio on the fake clock. A sample counts as playing until its WebAudio source fires `onended`, which real audio
  // time drives. Sources here never really start: `onended` comes from a fake timer after the buffer's duration,
  // so "sample is (not) playing" conditions repeat run to run. The engine plays samples for the same decoded
  // durations (assets_raw/dump/oracle/sounds.json, tools/dump_sounds.sh).
  if (window.AudioBufferSourceNode) {
    const P = window.AudioBufferSourceNode.prototype;
    P.start = function (when, offset) {
      if (this.__timer) window.clearTimeout(this.__timer);
      const rate = this.playbackRate ? this.playbackRate.value || 1 : 1;
      const ms = this.buffer ? Math.max(0, this.buffer.duration * 1000 - (offset || 0) * 1000) / rate : 0;
      const self = this;
      this.__timer = window.setTimeout(function () { self.__timer = 0; if (self.onended) self.onended(); }, ms);
    };
    P.noteOn = P.start;
    P.stop = function () { if (this.__timer) { window.clearTimeout(this.__timer); this.__timer = 0; } };
    P.noteOff = P.stop;
  }

  window.__clock = {
    now: () => now - BASE,
    pending: () => ({ timers: timers.length, frames: frames.length }),
    // Advance by ms, run the timers that came due (in due order), then one batch of animation frames.
    step(ms) {
      now += ms;
      for (;;) {
        timers.sort((a, b) => a.due - b.due || a.id - b.id);
        if (!timers.length || timers[0].due > now) break;
        const t = timers.shift();
        try { typeof t.fn === 'function' ? t.fn(...t.args) : (0, eval)(t.fn); }
        catch (e) { (window.__probeLog = window.__probeLog || []).push('timer: ' + (e && e.stack || e)); }
      }
      const batch = frames; frames = [];
      for (const f of batch) {
        try { f.fn(now - BASE); }
        catch (e) { (window.__probeLog = window.__probeLog || []).push('raf: ' + (e && e.stack || e)); }
      }
    },
    // A real macrotask turn, so XHR / image loads can land between steps.
    yieldReal: () => new Promise(r => realSetTimeout(r, 0)),
  };
})();
