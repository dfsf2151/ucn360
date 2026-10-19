#!/usr/bin/env python3
"""Print a frame's event program readably: one line per condition/action with object names and parameters.

    python3 tools/program_pretty.py FRAME [--grep NAME]

Reads build/bundle/program.json and bundle.json. The behaviour text per code is the runtime map's
(docs/runtime-map.md). --grep keeps only the groups that mention an object whose name contains NAME.
"""
import json
import sys

M = {
 ('C',-7,-4): 'player joystick pressed: (Vr & Mc)[player] has all bits of s[0]; triggered form tests pc',
 ('C',-6,-8): 'mouse button held: s[0].key 1/2/4 = left/right/middle, h.Jc[button]',
 ('C',-6,-7): 'user clicks on object s[1] with button/click type s[0] (triggered: pc, clicked oi Jw, instance Hw)',
 ('C',-6,-5): 'user clicks with button/click type s[0] (triggered on pc)',
 ('C',-6,-4): 'mouse pointer is over object s[0] (eA; negatable)',
 ('C',-6,-2): 'key s[0].key is held (h.Jc[key]; negatable)',
 ('C',-6,-1): 'upon pressing key s[0].key (held and newly pressed: KG)',
 ('C',-4,-8): 'every s[0] ms: s[1] keeps the countdown (Ds = 0 means unset); subtracts Go each tick, fires and reloads at <= 0',
 ('C',-4,-7): 'timer reaches s[0] ms: fires once, latched in s[1].value by tick Lb',
 ('C',-4,-2): 'timer Rf <= s[0] ms',
 ('C',-4,-1): 'timer Rf > s[0] ms',
 ('C',-3,-1): 'start of frame: Lb <= 2',
 ('C',-2,-3): 'no sample is playing (kc.oI; negatable)',
 ('C',-2,-1): 'sample s[0].ts is not playing (kc.nI; negatable)',
 ('C',-1,-7): 'only one action when event loops (sets group flag G.Sm; My skips when Lb is Hi or Hi+1)',
 ('C',-1,-6): 'run this event once (group flag G.kp latched)',
 ('C',-1,-3): 'compare two general values (s[0] op s[1], op = s[1].Ei)',
 ('C',-1,-1): 'always',
 ('C',2,-42): 'compare alterable value #s[0].value with constant s[1] (filters selection)',
 ('C',2,-34): 'pick one object at random (random(count))',
 ('C',2,-32): 'compare number of objects (Dg = live count) with s[0]',
 ('C',2,-29): 'sprite flag r.dg (1) clear, negatable (filters selection)',
 ('C',2,-28): 'sprite flag r.dg (1) set (filters selection)',
 ('C',2,-27): 'compare alterable value #expr(s[0]) (code 53) or #s[0].value with expression s[1]',
 ('C',2,-23): 'object overlaps a backdrop (collision test with pa.Nm / pa.ee; negatable)',
 ('C',2,-21): 'movement event: b.Kc == O.tF (path movement end/node; triggered)',
 ('C',2,-17): 'compare X position with s[0]',
 ('C',2,-16): 'compare Y position with s[0]',
 ('C',2,-7): 'object is stopped: b.aa (speed) == 0 (negatable)',
 ('C',2,-4): 'object overlaps object s[0] (wA, bounding box / mask)',
 ('C',2,-3): 'animation s[0] is playing: X.Xh == s[0] and X.Ld != 0',
 ('C',2,-2): 'animation s[0] is over: X.Xh == s[0] and X.Ld == 0 (also triggered)',
 ('C',2,-1): 'compare animation frame X.Pf with s[0]',
 ('C',3,-29): 'as (2,-29) for a text object',
 ('C',7,-81): 'compare counter value pa with s[0] (filters selection)',
 ('C',7,-4): 'as (2,-4) for a counter',
 ('A',-5,0): 'create object (s[0] create param: oi, position, direction, layer)',
 ('A',-3,0): 'next frame (zb = h.rp)',
 ('A',-3,2): 'jump to frame s[0] (zb = h.qp, Dm = frame)',
 ('A',-3,4): 'end/restart application (zb = h.pp when h.yA)',
 ('A',-3,7): 'center display at position s[0] (eK)',
 ('A',-2,1): 'stop all samples',
 ('A',-2,11): 'play sample s[0] on channel s[1] (loop flag in s[0].RC)',
 ('A',-2,12): 'play sample s[0] on channel s[1], s[2] loops',
 ('A',-2,17): 'set channel s[0] volume s[1] (0..100)',
 ('A',-2,18): 'no-op in this runtime',
 ('A',2,1): 'set position (s[0] position param; may set direction)',
 ('A',2,2): 'set X = floor(s[0])',
 ('A',2,6): 'set movement speed (B.la.Je)',
 ('A',2,13): 'select movement s[0] (B.cK)',
 ('A',2,15): 'X.zo = true (stop animation)',
 ('A',2,16): 'X.zo = false (start animation)',
 ('A',2,17): 'change animation sequence to s[0] (X.Zp)',
 ('A',2,19): 'force animation frame (X.oG)',
 ('A',2,23): 'set direction s[0] (& 31)',
 ('A',2,24): 'destroy (marks F.Me; text objects with h.Qs fade instead)',
 ('A',2,26): 'reappear: clear sprite flag r.eg (32), stop flashing (h.ZI)',
 ('A',2,27): 'make invisible: set sprite flag r.eg (32) (h.$I)',
 ('A',2,28): 'flash every s[0] ms',
 ('A',2,31): 'set alterable value #s[0] = s[1]',
 ('A',2,32): 'add s[1] to alterable value #s[0]',
 ('A',2,33): 'subtract s[1] from alterable value #s[0]',
 ('A',2,40): 'force animation direction / restore (X.nG)',
 ('A',2,58): 'move to front of layer (be(0))',
 ('A',2,61): 'move to layer s[0] (1-based)',
 ('A',2,63): 'set ink effect by name s[0] (Add/Invert/Sub/Mono/Blend/XOR/OR/AND)',
 ('A',2,65): 'set alpha coefficient 255 - s[0] (D.Nb top byte, flag r.Ns)',
 ('A',2,85): 'set scale s[0] (Rw), s[1] != 0 = resample',
 ('A',2,88): 'set angle s[0] mod 360, s[1] != 0 = resample',
 ('A',3,1): 'as (2,1) for a text object', ('A',3,24): 'as (2,24) for a text object',
 ('A',3,26): 'as (2,26) for a text object', ('A',3,27): 'as (2,27) for a text object',
 ('A',3,88): 'set text string s[0] (fD)',
 ('A',7,1): 'as (2,1) for a counter', ('A',7,24): 'as (2,24) for a counter', ('A',7,26): 'as (2,26) for a counter',
 ('A',7,27): 'as (2,27) for a counter', ('A',7,28): 'as (2,28) for a counter', ('A',7,65): 'as (2,65) for a counter',
 ('A',7,80): 'set counter to s[0] (gu, mn)',
 ('A',7,81): 'add s[0] to counter (OG)',
 ('A',7,82): 'subtract s[0] from counter (RG)',
 ('A',7,85): 'set counter colour 1 (PG)', ('A',7,86): 'set counter colour 2 (QG)',
 ('A',32,80): 'Ini extension action 80', ('A',32,86): 'Ini extension action 86',
 ('A',32,87): 'Ini extension action 87', ('A',32,93): 'Ini extension action 93',
 ('E',-1,-2): '")" (no-op token)', ('E',-1,-1): '"(" sub-expression', ('E',-1,0): 'integer constant',
 ('E',-1,1): 'random(expr)', ('E',-1,3): 'string constant', ('E',-1,4): 'str$(expr)',
 ('E',-1,23): 'double constant (marks the expression as float)', ('E',0,0): 'end of expression',
 ('E',0,2): '+', ('E',0,4): '- (unary when it starts an operand)', ('E',0,6): '*',
 ('E',0,8): '/ (integer division unless the expression is float; x/0 = 0)',
 ('E',2,16): 'alterable value #Lh of object Ub', ('E',2,81): 'X scale b.lb of object Ub',
 ('E',7,80): 'counter value pa of object Ub', ('E',32,82): 'Ini extension expression 82',
}

b = json.load(open('build/bundle/bundle.json'))
names = {o['handle']: o['name'] for o in b['objects']}
p = json.load(open('build/bundle/program.json'))
def split(c):
    c &= 0xFFFFFFFF; s = lambda v: v - 0x10000 if v >= 0x8000 else v
    return s(c & 0xFFFF), s(c >> 16)
def tok(t):
    c = t['code']
    if c == 65535: return str(t['value'])
    if c == 1572863: return repr(t['value'])
    if c == 262143: return json.dumps(t['Eb'])
    m = {0: '', 131072: '+', 262144: '-', 393216: '*', 524288: '/', -1: '(', -65537: ')', 131071: 'random(', 327679: 'str$('}
    if c in m: return m[c]
    ty, n = split(c)
    return f"{names.get(t.get('Qh'), t.get('Qh'))}.E{n}[{t.get('Lh','')}]"
def prm(x):
    if 'Fa' in x: return ('op%d ' % x['Ei'] if x['code'] in (23, 27, 15) else '') + ' '.join(tok(t) for t in x['Fa']).strip()
    d = {k: v for k, v in x.items() if k not in ('$ctor', 'code')}
    if x['code'] == 1: return 'obj:' + str(names.get(x['Qh']))
    if x['code'] in (9, 16): return 'pos:' + json.dumps({k: d[k] for k in ('qo', 'Dr', 'Er', 'Cr', 'Hk', 'hw') if k in d}) + (' create:' + str(names.get(d.get('eq'), d.get('eq'))) if 'eq' in d else '')
    return f"p{x['code']}:" + json.dumps(d)
F = int(sys.argv[1])
GREP = sys.argv[sys.argv.index('--grep') + 1].lower() if '--grep' in sys.argv else None
for g in p['frames'][F]['groups']:
    r = g['raw']
    if GREP and not any(GREP in str(names.get(it['ke'], '')).lower() or any(GREP in str(names.get(x.get('Qh'), '')).lower() for x in it.get('s') or []) or any(GREP in str(names.get(t.get('Qh'), '')).lower() for x in it.get('s') or [] for t in x.get('Fa') or []) for it in r['ib']):
        continue
    print(f"G{g['gi']} Ka={r['Ka']} conds={r['Yb']} acts={r['xe']}")
    for k, it in enumerate(r['ib']):
        kind = 'C' if k < r['Yb'] else 'A'
        t, n = split(it['ya'])
        nm = M.get((kind, t, n), '?').split(':')[0].split('(')[0][:40]
        obj = names.get(it['ke'], it['ke']) if t >= 2 else ''
        neg = 'NOT ' if it['Qe'] & 1 else ''
        print(f"   {kind} {neg}({t},{n}) {obj!s:<14} {nm:<40} " + ' | '.join(prm(x) for x in it.get('s') or []))
