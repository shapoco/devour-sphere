#!/usr/bin/env python3
"""Run a Paradise program (the hello program or the game) in Unicorn before it goes to the device.

The hardware layer's entry points (hw_*, flash_*) are caught by symbol and answered here: the panel
becomes a picture, the buttons and the dial follow a schedule, the tones and the flash writes are
logged. The SysTick runs every RT instructions (about one millisecond of a 96 MHz Cortex-M3), by
calling the program's systick_c() like the interrupt would. After the Paramix Studio simulators
(emu/test/devemu.py, GPL-3.0-or-later), rewritten for this program's symbols.

  python3 sim.py --elf build/ds.elf --pram build/pram.bin [--xip build/xip.bin] [--rom paradise.bin]
                 --ms 5000 [--keys "t:mask,..."] [--wheel "t:steps,..."] [--shots "t,..."] [--out dir] [--prof]

  mask: 1 A, 2 B, 4 C, 8 dial pressed (BTN_* of hw.h); the mask holds from t seconds on.
  wheel: at t seconds the dial turns `steps` quadrature steps (negative the other way), one per ms.
  --prof: where the time goes (PC samples by function), to decide what lives in PRAM.
  Needs: pip install unicorn pillow; arm-none-eabi-nm on the PATH.
"""
import argparse, bisect, os, struct, subprocess, sys
from unicorn import Uc, UcError, UC_ARCH_ARM, UC_MODE_THUMB, UC_MODE_MCLASS, UC_HOOK_CODE, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE
from unicorn.arm_const import *

ap = argparse.ArgumentParser()
ap.add_argument('--elf', required=True); ap.add_argument('--pram', required=True); ap.add_argument('--xip')
ap.add_argument('--rom'); ap.add_argument('--ms', type=int, default=3000); ap.add_argument('--rt', type=int, default=64000)
ap.add_argument('--keys', default=''); ap.add_argument('--wheel', default=''); ap.add_argument('--shots', default='')
ap.add_argument('--out', default='.'); ap.add_argument('--prof', action='store_true'); ap.add_argument('--quiet', action='store_true'); ap.add_argument('--watch', default='', help='symbols (u32) to print at the end, comma separated')
A = ap.parse_args()
os.makedirs(A.out, exist_ok=True)

syms = {}
for l in subprocess.run(['arm-none-eabi-nm', A.elf], capture_output=True, text=True).stdout.splitlines():
    p = l.split()
    if len(p) == 3: syms[p[2]] = int(p[0], 16)
pram = open(A.pram, 'rb').read()
if len(pram) > 0x10000: sys.exit('PRAM image over 64 KB')
# the settings block: PMXC at 0x140, then ours ("DSPH": xip flash offset, length, high score sector)
cfg = struct.unpack('<8I', pram[0x1D8:0x1F8]) if len(pram) >= 0x1F8 else None
xip_off = cfg[2] if cfg and cfg[0] == 0x48505344 else 0
hiscore_off = cfg[4] if cfg and cfg[0] == 0x48505344 else 0

fl = bytearray(b'\xff' * 0x1000000)
if A.rom: fl[:] = open(A.rom, 'rb').read()[:0x1000000].ljust(0x1000000, b'\xff')
if A.xip:
    x = open(A.xip, 'rb').read(); fl[xip_off:xip_off + len(x)] = x

u = Uc(UC_ARCH_ARM, UC_MODE_THUMB | UC_MODE_MCLASS)
u.ctl_set_cpu_model(UC_CPU_ARM_CORTEX_M4)
u.mem_map(0, 0x10000); u.mem_write(0, pram)
u.mem_map(0x18000000, 0x20000)      # AHB SRAM
u.mem_map(0x20000000, 0x10000)      # mailbox (and what DOOM found above it)
u.mem_map(0x10000000, 0x100000)     # the cached flash window at base 0x60011000
u.mem_write(0x10000000, bytes(fl[0x11000:0x111000]))
u.mem_map(0x60000000, 0x1000000); u.mem_write(0x60000000, bytes(fl))
u.mem_map(0x40000000, 0x100000); u.mem_map(0x45000000, 0x1000); u.mem_map(0xE0000000, 0x100000)
u.mem_write(0x4000B014, struct.pack('<I', 0x60))   # UART1: sender empty

fb = [[0] * 128 for _ in range(128)]
rect = {'x0': 0, 'x1': 128, 'y': 0, 'x': 0}
state = {'ms': 0, 'tone': [], 'flashw': 0, 'back': False, 'frames': [], 'ins': 0, 'fault': None, 'gpio2': 0}
KS = [(float(t), int(m)) for t, m in (p.split(':') for p in A.keys.split(',') if ':' in p)]
WH = [(float(t), int(s)) for t, s in (p.split(':') for p in A.wheel.split(',') if ':' in p)]
SH = [float(x) for x in A.shots.split(',') if x]

def keys():
    m = 0
    for t, v in KS:
        if state['ms'] / 1000.0 >= t: m = v
    return m
def ret(uc, v=None):
    if v is not None: uc.reg_write(UC_ARM_REG_R0, v)
    uc.reg_write(UC_ARM_REG_PC, uc.reg_read(UC_ARM_REG_LR) | 1)
def put_px(v):
    if rect['y'] < 128 and rect['x'] < 128: fb[rect['y']][rect['x']] = v
    rect['x'] += 1
    if rect['x'] >= rect['x1']: rect['x'] = rect['x0']; rect['y'] += 1
def h_begin(uc):
    x0, y0, x1, y1 = (uc.reg_read(r) for r in (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3))
    rect.update(x0=x0, x1=x1, y=y0, x=x0); ret(uc)
def h_row(uc):
    p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    for v in struct.unpack('<%dH' % n, uc.mem_read(p, 2 * n)): put_px(v)
    ret(uc)
def h_raw(uc):
    p, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = bytes(uc.mem_read(p, n))
    for i in range(0, n - 1, 2): put_px((d[i] << 8) | d[i + 1])
    ret(uc)
def h_push(uc):
    p, stride = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    x0, y0 = uc.reg_read(UC_ARM_REG_R2), uc.reg_read(UC_ARM_REG_R3)
    x1, y1 = struct.unpack('<II', uc.mem_read(uc.reg_read(UC_ARM_REG_SP), 8))
    for y in range(y0, min(y1, 128)):
        d = struct.unpack('<%dH' % (x1 - x0), uc.mem_read(p + 2 * (y * stride + x0), 2 * (x1 - x0)))
        for i, v in enumerate(d):
            if x0 + i < 128: fb[y][x0 + i] = v
    ret(uc)
def h_tone(uc):
    hz = uc.reg_read(UC_ARM_REG_R0)
    if not state['tone'] or state['tone'][-1][1] != hz: state['tone'].append((state['ms'], hz))
    ret(uc)
def h_btn(uc): ret(uc, keys())
def h_back(uc): state['back'] = True; uc.emu_stop()
def h_nop(uc): ret(uc)
def flash_write(a, d):
    a &= 0xFFFFFF
    u.mem_write(0x60000000 + a, d); fl[a:a + len(d)] = d; state['flashw'] += 1
    if 0x11000 <= a < 0x111000: u.mem_write(0x10000000 + a - 0x11000, d)
def h_prog4k(uc):
    ad, src = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    flash_write(ad, bytes(uc.mem_read(src, 0x1000))); ret(uc)
def h_update4k(uc):
    ad, src = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1)
    d = bytes(uc.mem_read(src, 0x1000)); a = ad & 0xFFFFFF
    if bytes(fl[a:a + 0x1000]) == d: ret(uc, 0); return
    flash_write(ad, d); ret(uc, 1)
def h_erase4k(uc):
    flash_write(uc.reg_read(UC_ARM_REG_R0) & 0xFFF000, b'\xff' * 0x1000); ret(uc)
def h_prog(uc):
    ad, src, n = uc.reg_read(UC_ARM_REG_R0), uc.reg_read(UC_ARM_REG_R1), uc.reg_read(UC_ARM_REG_R2)
    old = bytes(fl[ad & 0xFFFFFF:(ad & 0xFFFFFF) + n]); new = bytes(uc.mem_read(src, n))
    flash_write(ad, bytes(a & b for a, b in zip(old, new))); ret(uc)
def h_frame(uc):
    state['frames'].append((state['ms'], state['ins'])); ret(uc)
def h_fault(uc):
    sp = uc.reg_read(UC_ARM_REG_SP)
    fr = struct.unpack('<8I', uc.mem_read(sp, 32))
    state['fault'] = 'fault at ms %d: pc %08x lr %08x psr %08x cfsr %08x' % (state['ms'], fr[6], fr[5], fr[7], struct.unpack('<I', uc.mem_read(0xE000ED28, 4))[0])
    uc.emu_stop()
HOOKS = {'hw_init': h_nop, 'hw_wdt_feed': h_nop, 'hw_buttons': h_btn, 'hw_px_begin': h_begin, 'hw_px_row': h_row,
         'hw_px_raw': h_raw, 'hw_px_dma_start': h_raw, 'hw_px_dma_wait': h_nop, 'hw_px_end': h_nop, 'hw_push': h_push, 'hw_tone': h_tone, 'hw_backlight': h_nop,
         'hw_bl_pwm_off': h_nop, 'hw_back_to_paradise': h_back, 'flash_prog4k': h_prog4k, 'flash_update4k': h_update4k,
         'flash_erase4k': h_erase4k, 'flash_prog': h_prog, 'ds_frame_mark': h_frame, 'fault_c': h_fault, 'ds_fault_c': h_fault}
for name, f in HOOKS.items():
    if name in syms:
        a = syms[name] & ~1
        u.hook_add(UC_HOOK_CODE, (lambda f: lambda uc, ad, sz, ud: f(uc))(f), begin=a, end=a)
def bad(uc, access, addr, size, value, ud):
    print('unmapped access', access, hex(addr), 'pc', hex(uc.reg_read(UC_ARM_REG_PC)), 'ms', state['ms']); return False
u.hook_add(UC_HOOK_MEM_UNMAPPED, bad)

# the symbol table sorted, for the profile
fs = sorted(((v, k) for k, v in syms.items() if v < 0x20000000 and not k.startswith('$')), key=lambda t: t[0])
fa = [a for a, _ in fs]
PROF = {}
def fname(a):
    i = bisect.bisect_right(fa, a & ~1) - 1
    return fs[i][1] if i >= 0 else '?'

def shot(name):
    from PIL import Image
    im = Image.new('RGB', (128, 128))
    for y in range(128):
        for x in range(128):
            v = fb[y][x]; im.putpixel((x, y), (((v >> 11) & 31) << 3, ((v >> 5) & 63) << 2, (v & 31) << 3))
    im.save(os.path.join(A.out, name))

# the dial: a quadrature sequence, one step per millisecond when a turn is scheduled
GRAY = [0, 1, 3, 2]
wheel_left = 0; wheel_dir = 1; gidx = 0
def wheel_tick():
    global wheel_left, gidx
    for t, s in WH:
        if int(t * 1000) == state['ms']: wheel_left = abs(s); globals()['wheel_dir'] = 1 if s > 0 else -1
    if wheel_left:
        wheel_left -= 1; gidx = (gidx + wheel_dir) & 3
        u.mem_write(0x4001A000, struct.pack('<I', GRAY[gidx]))

sp, rst = struct.unpack('<II', pram[:8])
u.reg_write(UC_ARM_REG_SP, sp)
pc = rst
systick = syms.get('systick_c')
RET = 0x1F000000   # where a called handler returns to (never fetched: emu_start stops there)
NCH = 8
err = None
while state['ms'] < A.ms and not state['back'] and not state['fault']:
    u.mem_write(0x45000304, struct.pack('<I', 1000 + state['ms'] // 1000))
    ch = max(1, A.rt // NCH)
    for k in range(NCH):
        u.mem_write(0xE000E018, struct.pack('<I', 95999 - (k * 96000) // NCH))
        try: u.emu_start(pc | 1, 0xFFFFFFF0, count=ch)
        except UcError as ex:
            err = 'error %s pc %x ms %d' % (ex, u.reg_read(UC_ARM_REG_PC), state['ms']); break
        state['ins'] += ch
        pc = u.reg_read(UC_ARM_REG_PC)
        if state['back'] or state['fault']: break
        if A.prof: PROF[pc] = PROF.get(pc, 0) + 1
    if err or state['back'] or state['fault']: break
    state['ms'] += 1
    wheel_tick()
    if systick is not None:
        # the interrupt: the handler runs on the same stack, PRIMASK aside
        SAVED = (UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R12, UC_ARM_REG_LR, UC_ARM_REG_XPSR)
        regs = [u.reg_read(r) for r in SAVED]   # what the exception frame would hold, the flags included
        u.reg_write(UC_ARM_REG_LR, RET | 1)
        try: u.emu_start(systick | 1, RET, count=200000)
        except UcError as ex:
            err = 'error in systick %s pc %x ms %d' % (ex, u.reg_read(UC_ARM_REG_PC), state['ms']); break
        if u.reg_read(UC_ARM_REG_PC) != RET: err = 'systick_c did not return (pc %x)' % u.reg_read(UC_ARM_REG_PC); break
        for r, v in zip(SAVED, regs): u.reg_write(r, v)
    for i, t in enumerate(SH):
        if int(t * 1000) == state['ms']: shot('shot_%02d.png' % i)
if err: print(err)
if state['fault']: print(state['fault'], fname(int(state['fault'].split('pc ')[1][:8], 16)))
shot('shot_end.png')
fr = state['frames']
print('ms', state['ms'], 'instructions', state['ins'], 'back to the Paradise' if state['back'] else '', 'pc %x' % pc, fname(pc))
print('tones', len(state['tone']), state['tone'][:12], 'flash writes', state['flashw'])
if len(fr) > 2:
    n = len(fr) - 1; dms = fr[-1][0] - fr[0][0]; dins = fr[-1][1] - fr[0][1]
    print('frames %d in %d ms: %.1f fps at %d instructions/ms, %d instructions a frame' % (n, dms, 1000.0 * n / max(1, dms), A.rt, dins // n))
if PROF:
    agg = {}
    for a, n in PROF.items(): agg[fname(a)] = agg.get(fname(a), 0) + n
    tot = sum(agg.values())
    print('profile (%d samples; PRAM = address below 0x10000):' % tot)
    for k, v in sorted(agg.items(), key=lambda kv: -kv[1])[:30]: print('  %5.1f%%  %08x  %s' % (100.0 * v / tot, syms.get(k, 0), k))
for w in [x for x in A.watch.split(',') if x]:
    print('watch', w, hex(syms[w]), struct.unpack('<i', u.mem_read(syms[w], 4))[0] if w in syms else '?')
if A.rom or state['flashw']: open(os.path.join(A.out, 'flash_after.bin'), 'wb').write(fl)
