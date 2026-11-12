#!/usr/bin/env python3
# digicmi copy of digi1_mods tests/digiemu_poly.py (MIT, see LICENSE-digi1_mods). Changes:
#  - port_os is imported from the digi1_mods checkout beside digicmi (--digi1 to override);
#  - SETTINGS > POLY is found without assuming Digi Matrix's MOD MATRIX entry sits under it
#    (in a build without Digi Matrix, POLY is the last entry).
"""Digi Poly inside the real firmware (digiemu, headless): the parts tests/emu_poly.py cannot reach.

    python3 tests/digiemu_poly.py --digiemu <checkout, after --add of the build> --fw <firmware folder name>
        --stock <official .syx> --elekloader <checkout> --mods <core> <digipoly> [<others>] --png <dir>

tests/emu_poly.py checks the code itself (voice choice, the settings row, the chord's messages, the knob
and level mirroring) in seconds. This one boots the firmware and checks what only it can show:
  1. a track set to POLY gets the MIDI tracks' TRIG page, with the track's level beside it;
  2. pressing that track's key plays the whole chord;
  3. its trigs play the chord while the sequencer runs, and the POLY track's LEVEL and knobs reach every
     voice of the chord, including a voice stolen for the first time after the knob was turned;
  4. SETTINGS > POLY writes the pattern's own kit, and another pattern keeps its own voice allocation;
  5. the LEVEL knob on the POLY track's TRIG page moves the track level;
  6. a live note recorded onto a POLY track goes into the step's NOT1..NOT4, not a bare trig. The
     emulator has no MIDI input, so the note comes from the unit's own key and the sequencer's record
     step (0x4020c29c, which the firmware only sets while it is recording) is forced for that one note -
     which is exactly the condition the record path runs under.
Prints a report and PASS/FAIL.
"""
import argparse, json, os, struct, sys, time, types, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--digiemu", required=True)
ap.add_argument("--fw", required=True)
ap.add_argument("--stock", required=True)
ap.add_argument("--elekloader", required=True)
ap.add_argument("--mods", nargs="+", required=True)
ap.add_argument("--png", required=True)
ap.add_argument("--digi1", default=os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__)))), "digi1_mods"), help="digi1_mods checkout (for tools/port_os.py)")
a = ap.parse_args()

sys.path.insert(0, a.elekloader)
from elekloader import syx as _syx, devices as _dev, elemod as _em, link as _link
_st = _syx.Syx.load(a.stock)
_d, _r = _dev.identify(_st.sha256)
sys.path.insert(0, os.path.join(a.digi1, "tools"))
from port_os import fw as _fw
OS = _r.version                                    # the firmware addresses here are 1.53's; fw() moves them
def fw(x):
    return _fw(x, OS)
MAP = _link.link([_em.load_any(p) for p in a.mods], _st.section(_d.main_section)).map

FW = os.path.join(a.digiemu, "portable", "firmware", a.fw)
sys.path.insert(0, a.digiemu)
syxname = [f for f in os.listdir(FW) if f.endswith(".syx")][0]
os.environ.update({
    "DT2_SYX": os.path.join(FW, syxname), "DT2_SECTIONS": FW + "/sections", "DT2_SNAPSHOTS": FW + "/snapshots",
    "DT2_PLUSDRIVE": FW + "/plusdrive.img", "DT2_MAIN_IMG": FW + "/sections/section_3_MAIN_OS.bin",
    "DT2_DEVICES": FW + "/devices" if os.path.isdir(FW + "/devices") else os.path.join(a.digiemu, "devices"),
    "DIGIKIT_PIT3_PROBE": "1"})
os.chdir(FW)
_tk = types.ModuleType("tkinter")
_tk.Frame = type("Frame", (), {}); _tk.Tk = type("Tk", (), {})
sys.modules["tkinter"] = _tk
for _n in ("ttk", "messagebox", "filedialog", "font"):
    sys.modules["tkinter." + _n] = types.ModuleType("tkinter." + _n)
from unicorn import UC_HOOK_CODE
from unicorn.m68k_const import (UC_M68K_REG_A2, UC_M68K_REG_A3, UC_M68K_REG_A4, UC_M68K_REG_A6, UC_M68K_REG_A7,
                                 UC_M68K_REG_D0, UC_M68K_REG_D1, UC_M68K_REG_D2, UC_M68K_REG_D4)
import emu.gui as G
if os.environ.get("DIGICMI_LRTEST"):              # the SSI's output frames, left/right, while a note is measured
    import emu.ssi as _ssi
    def _lr_sink(data):
        on = state.get("lr_on") if "state" in globals() else None
        if on:
            acc = state["lr"][on]
            n_ = len(data) // 8
            for i in range(n_):
                l_, r_ = struct.unpack_from(">ii", data, 8 * i)
                acc[0] += abs((l_ << 8) >> 8); acc[1] += abs((r_ << 8) >> 8); acc[2] += 1
                sm_ = state.setdefault("lrs", {}).setdefault(on, [])
                if len(sm_) < 24000:
                    sm_.append(((l_ << 8) >> 8) + ((r_ << 8) >> 8))
    def _sink_get(self): return getattr(self, "_lr_sink_inner", None)
    def _sink_set(self, f):
        self._lr_sink_inner = None if f is None else (lambda d, f=f: (_lr_sink(d), f(d)))
    _ssi.Ssi0Dma.sink = property(_sink_get, _sink_set)

SNAP = [os.path.join(dp, f) for dp, _, fs in os.walk(FW + "/snapshots") for f in fs if f == "gui.snap"][0]
os.makedirs(a.png, exist_ok=True)
UI_KIT, ENG_KIT = fw(0x4199dc44), 0x800019ac
LOADED, PARAMS, LEVELS = 0x800014f0 + 0x131 * 4, 0x800014f2, 0x80002760
PAT = fw(0x409bac18)
VOICE_START = fw(0x40077a76)                       # the render starts voice d2 for message a2
AMP, OWNER, MUTES = fw(0x4199df54), fw(0x4399db54), fw(0x4199e47c)   # fw_amp_env; voice owners; mutes
CMI = any(os.path.basename(m).startswith("digicmi") for m in a.mods)
POLY_ID = 40 if CMI else 6
SMP_TAB = 0x4031a3a0 if OS == "1.54" else 0x4031a3a0 - 0x1000   # sample slots, 16 bytes each (digislicer)
TEST_PCM, TEST_LEN = 0x44000000, 9600               # the loop probe's sample: free sample RAM, 0.2 s
PROBE_PLAY = int(os.environ.get("DIGICMI_PLAY", "2"))   # 0 REV, 1 REV.L, 2 FWD.L, 3 FWD
PROBE_LOOP = int(os.environ.get("DIGICMI_LOOP", "60"))
PROBE_EXTRAS = [int(os.environ.get(k, "0")) for k in ("DIGICMI_LLEN", "DIGICMI_LMOD", "DIGICMI_DIR", "DIGICMI_LFIN",
                                                        "DIGICMI_8BIT", "DIGICMI_ENG")]
LOOPTEST = os.environ.get("DIGICMI_LOOPTEST")      # hold, sus, alt or lfin: the loop controls, checked
if LOOPTEST:
    os.environ["DIGICMI_LOOPPROBE"] = "1"
    PROBE_PLAY = {"alt": 4, "rev": 1}.get(LOOPTEST, 2)   # 4: CMI's P (ping-pong); 1: REV.L
    PROBE_LOOP = 60
    PROBE_EXTRAS = {"hold": [0, 0, 0, 0], "sus": [0, 100, 0, 0], "alt": [0, 0, 0, 0], "lfin": [0, 0, 0, 64],
                    "live": [0, 0, 0, 0], "rev": [0, 0, 0, 0]}[LOOPTEST] + [0, 0]   # 0.13: no LLEN, loops run to END
LIVE_STEP, LIVE_LOOP = 1416, 90                    # live: LOOP 60 -> 90 in the middle of the held chord
PLAYREAD0 = 0x4007525e                             # voice 0: just after its ONESHOT PLAY read (digicmi's hook)
VOICE_VEL = 0x80001f18                             # fw_voice_vel: each voice's velocity, 8.8 (the gain's input)
RENDER_DONE = 0x40077fba                           # the render has written every track's block (same in 1.53/1.54)
LOCK_HEAD = 0x421f7ee4 if OS == "1.54" else 0x421f6ee4      # the lock lists' free list (1.54 measured)
VELS = [1, 32, 33, 64, 65, 96, 97, 127, 100, 20]   # digicmi: velocities that straddle 4 layers' edges


def xb(n):                                       # the persistent map (src/kitstore.h)
    return 0x20 + n // 6 * 0xa2 + (0, 1, 2, 3, 0x14, 0x15)[n % 6]


def png(path, fb):
    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
    big = []
    for y in range(64):
        line = b"".join(bytes([0 if fb[y * 128 + x] else 255] * 3) * 4 for x in range(128))
        big += [b"\0" + line] * 4
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 512, 256, 8, 2, 0, 0, 0))
                           + chunk(b"IDAT", zlib.compress(b"".join(big))) + chunk(b"IEND", b""))


PLAN, t = [], 600
def at(dt, *act):
    global t
    t += dt
    PLAN.append((t, act))
def tap(code, hold=20, gap=40):
    at(1, "press", code); at(hold, "release", code); at(gap, "wait")
def turn(enc, n):                               # n notches, one event each
    for _ in range(abs(n)):
        at(4, "encoder", enc, 4 if n > 0 else -4)
    at(40, "wait")

# 1. track 1 -> POLY, its TRIG page
at(1, "press", 1); at(20, "press", 20); at(20, "release", 20); at(5, "release", 1); at(60, "wait")
if os.environ.get("DIGICMI_SRCSHOT"):
    at(150, "snap", "q8_machine_menu")
for _ in range(4):
    tap(15, 20, 60)
tap(12, 20, 80); tap(13, 20, 60)
at(0, "machines")
if os.environ.get("DIGICMI_STOCKSHOT"):         # track 2 (ONESHOT): its SRC page, then SRC again (the stock view)
    at(1, "press", 2); at(20, "press", 25); at(20, "release", 25); at(5, "release", 2); at(150, "wait")
    tap(20, 20, 150); at(250, "snap", "q9_oneshot_src")
    tap(20, 20, 150); at(250, "snap", "q9_oneshot_src2")
    for enc in (5, 6, 7):                       # E, F, G turned there
        turn(enc, 6); at(150, "snap", "q9_oneshot_src2_enc%d" % enc)
    tap(20, 20, 150); at(250, "snap", "q9_oneshot_src3")
    at(1, "press", 2); at(20, "press", 24); at(20, "release", 24); at(5, "release", 2); at(150, "wait")
if os.environ.get("DIGICMI_SRCSHOT"):           # the machine's SRC page (key 20), then its second page
    tap(20, 20, 150); at(250, "snap", "q0_src_page")
    tap(20, 20, 150); at(250, "snap", "q0_src_page2")
    PAGES_END = t + 2                           # DIGICMI_PAGES_ONLY: stop here, no playback or sound checks
    if os.environ.get("DIGICMI_INSNPROF"):      # instructions inside the audio render, by owner: idle, then 4 notes held
        at(0, "chord", (0x44, 0x47, 0x4b))
        at(200, "prof", "idle")
        at(400, "wait")
        at(1, "press", 24); at(80, "prof", "4 notes held (FWD.L)"); at(400, "wait"); at(5, "release", 24)
        PAGES_END = t + 2
    if os.environ.get("DIGICMI_REDRAWTEST"):    # frames drawn: page 2 with a held note, then the TRIG page, note still held
        tap(20, 20, 150)
        at(1, "press", 24); at(60, "frames", "page 2, note held"); at(400, "frames", None)
        tap(19, 20, 60); at(0, "frames", "TRIG page, note held"); at(400, "frames", None); at(5, "release", 24)
        at(0, "frames", "TRIG page, no note"); at(300, "frames", None)
        PAGES_END = t + 2
    if os.environ.get("DIGICMI_TIMESHOT"):      # page 2: G turned, the label place over time
        tap(20, 20, 150); at(100, "snap", "q5_page2")
        for _ in range(3):
            at(4, "encoder", 7, 4)
        for d_, nm in ((10, "a"), (60, "b"), (150, "c"), (300, "d"), (600, "e")):
            at(d_, "snap", "q5_gturn_" + nm)
        PAGES_END = t + 2
    if os.environ.get("DIGICMI_LRTEST"):        # single notes from track 1's key: left/right output each
        at(0, "chord", (0x40, 0x40, 0x40))
        for k in range(4):
            at(1, "press", 24); at(15, "lr", "note %d" % (k + 1)); at(90, "lr", None); at(5, "release", 24)
            at(250, "wait")
        PAGES_END = t + 2
    if os.environ.get("DIGICMI_ARROWSHOT"):     # page 2 while track 1's key holds a note: the playhead arrows
        tap(20, 20, 150)
        at(1, "press", 24); at(120, "snap", "q0_arrows_a"); at(120, "snap", "q0_arrows_b"); at(5, "release", 24)
        at(1200, "snap", "q0_arrows_after_release"); tap(20, 20, 150)
        PAGES_END = t + 2
    if os.environ.get("DIGICMI_SRCSHOT") == "3":  # a third press, then E and G turned there
        tap(20, 20, 150); at(250, "snap", "q0_src_page3")
        turn(5, 6); turn(7, 6); at(150, "snap", "q0_src_page3_turned")
        tap(20, 20, 150); at(250, "snap", "q0_src_page4")
        for _ in range(4):                      # page 1: B (8BIT) a little up, then down
            at(4, "encoder", 2, 4)
        at(30, "slots8bit", "8BIT turned up"); at(0, "snap", "q0_switch_on")
        for k_ in range(1, 9):                   # the pop-up's life, in UI ticks
            at(150, "snap", "q0_popup_%d" % k_)
        at(400, "snap", "q0_switch_on_later")    # the label back after the value's pop-up time
        for _ in range(4):
            at(4, "encoder", 2, -4)
        at(30, "slots8bit", "8BIT turned down")
        for _ in range(4):                      # C (ENG) up, then D (SAMP)
            at(4, "encoder", 3, 4)
        at(60, "slots8bit", "ENG turned up"); at(0, "snap", "q0_eng_on")
        for _ in range(4):
            at(4, "encoder", 3, -4)
        at(60, "slots8bit", "ENG turned down")
        if os.environ.get("DIGICMI_MACHINETEST"):    # CMI on track 1: track 2 can't change; then it can
            def menu_pick(track_key, moves, label):
                at(1, "press", 2); at(20, "press", track_key); at(20, "release", track_key); at(5, "release", 2)
                at(80, "wait")
                at(1, "press", 1); at(20, "press", 20); at(20, "release", 20); at(5, "release", 1); at(150, "wait")
                at(0, "snap", "q7_menu_open_%s" % label.split(",")[0].replace(" ", "_").replace(">", ""))
                for _ in range(abs(moves)):
                    tap(15 if moves > 0 else 14, 10, 45)
                at(60, "snap", "q7_menu_%s" % label.replace(" ", "_").replace(",", "").replace(">", ""))
                tap(12, 20, 120); tap(13, 20, 80); at(150, "mlog", label)
            menu_pick(25, 1, "track 2 -> WERP, track 1 CMI")
            menu_pick(25, 8, "track 2 -> CMI, track 1 CMI")
            menu_pick(24, -4, "track 1 -> ONESHOT")
            menu_pick(25, 1, "track 2 -> WERP, track 1 ONESHOT")
            menu_pick(25, 8, "track 2 -> CMI, track 1 ONESHOT")   # past the bottom: CMI, the last row
            menu_pick(24, 4, "track 1 -> CMI again")
            PAGES_END = t + 2
        at(1500, "snap", "q0_switch_eng_much_later")              # 8BIT/ENG's labels, long after
        turn(1, 2); at(400, "snap", "q0_tune_later"); at(1500, "snap", "q0_tune_much_later")   # a stock knob
        turn(4, 2); at(200, "snap", "q0_samp_turned"); at(0, "slots8bit", "after SAMP")
        PAGES_END = t + 2
at(0, "chord", (0x44, 0x47, 0x4b))              # the track's NOT2-NOT4: a major 7th chord
tap(19, 20, 150); at(0, "snap", "q1_trig_poly")
# 2. the track's key plays the chord
at(0, "phase", "key"); at(1, "press", 24); at(60, "release", 24); at(120, "wait"); at(0, "phase", None)
# 3. trigs, then PLAY; knobs while it runs
tap(9, 20, 60); tap(24, 20, 40); tap(32, 20, 40); tap(9, 20, 60)
at(0, "phase", "play"); tap(10, 20, 400)
tap(21, 20, 60)                                 # FLTR page of the POLY track
at(0, "follow", "before")
if CMI:
    tap(21, 20, 150)                            # the FLTR page: knob E really is a filter knob here
turn(5, 12)                                     # knob E: filter frequency
turn(9, -12)                                    # the LEVEL knob
at(0, "follow", "after the knobs")
at(300, "follow", "after more chords")          # a voice stolen for the first time since the knobs
tap(11, 20, 80); at(0, "phase", None)
if CMI:                                         # FUNC held: the eight track keys, blue
    at(60, "ledprobe", "idle"); at(0, "memsnap", "a"); at(1, "press", 1); at(60, "ledprobe", "func"); at(0, "memsnap", "b")
    at(1, "release", 1); at(60, "ledprobe", "after"); at(0, "memsnap", "c")
    at(1, "press", 1); at(60, "memsnap", "d"); at(1, "release", 1); at(60, "memsnap", "e")
# 4. SETTINGS > POLY, and another pattern's own allocation
tap(6, 20, 150)
MATRIX = any("digimatrix" in os.path.basename(m) for m in a.mods)
for _ in range(20):          # to the bottom of the list: POLY, or MOD MATRIX under it with Digi Matrix
    tap(15, 10, 45)
if MATRIX:
    tap(14, 20, 120)         # one up, past MOD MATRIX
else:
    at(120, "phase", None)   # keep the original timing
at(60, "snap", "q2_settings")
if CMI:                                         # 0.10: no SETTINGS row; just leave the menu
    at(60, "snap", "q3_settings"); tap(13, 20, 80); tap(13, 20, 80)
else:
    tap(17, 20, 200); tap(12, 20, 200)          # RIGHT to track 2, YES: out of the pool
    at(60, "snap", "q3_settings_track2_out")
    tap(13, 20, 80); tap(13, 20, 80)
    at(0, "locks", "pattern 1")
    at(1, "press", 3); at(20, "press", 25); at(20, "release", 25); at(5, "release", 3); at(400, "locks", "pattern 2")
    at(1, "press", 3); at(20, "press", 24); at(20, "release", 24); at(5, "release", 3); at(400, "locks", "pattern 1 again")
# 5. the TRIG page's LEVEL knob
tap(19, 20, 200)                                # the POLY track's TRIG page
at(0, "level", "before")
turn(9, -14)
at(200, "level", "after 14 down")
at(0, "snap", "q4_trig_level")
# 6. recording a live note onto the POLY track
tap(20, 20, 150)                                # off the TRIG page
at(0, "clearnotes", None)
at(0, "force", 5)                               # the sequencer's record step, as while recording
at(1, "press", 24); at(80, "release", 24); at(300, "wait")
at(0, "force", 0)
at(0, "notes", "after a recorded note")
STEPS = t + 2
PAGES_ONLY = bool(os.environ.get("DIGICMI_PAGES_ONLY"))
if PAGES_ONLY:
    STEPS = PAGES_END

MEM_REGIONS = [(0x40200000, 0x800000), (0x41900000, 0xa00000), (0x43900000, 0x100000), (0x80000000, 0x10000)]
KIT0 = 0x4191f018 if OS == "1.54" else 0x4191e018   # the first pattern's kit; +0x91e a pattern
state = {"n": 0, "phase": None}
starts = {}
log, fails = [], []

_spin = G.spin
def spin(m, pc, *args, **kw):
    uc = m.uc
    if state["n"] == 0:
        if CMI and not os.environ.get("DIGICMI_POLY8_OFF"):   # 8 TRACK POLY on, as a saved project would have it
            uc.mem_write(KIT0 + xb(45), bytes([uc.mem_read(KIT0 + xb(45), 1)[0] | 1]))
        def vstart(u, ad, s_, d):
            ph = state["phase"]
            if not ph:
                return
            msg = u.reg_read(UC_M68K_REG_A2)
            v = u.reg_read(UC_M68K_REG_D2)
            note, mid, snd = (struct.unpack(">i", u.mem_read(msg + 24, 4))[0],
                              struct.unpack(">i", u.mem_read(msg + 12, 4))[0],
                              struct.unpack(">I", u.mem_read(msg + 40, 4))[0])
            kit = struct.unpack(">I", u.mem_read(ENG_KIT, 4))[0]
            st = (snd - kit - 0x20) // 0xa2 if snd else v
            starts.setdefault(ph, []).append((state["n"], v, note, mid, st))
        uc.hook_add(UC_HOOK_CODE, vstart, begin=VOICE_START, end=VOICE_START)
        if os.environ.get("DIGICMI_REDRAWTEST") and "digicmi_wave_draw" in MAP:
            def frame_(u, ad, s_, d):
                if state.get("fr_on"):
                    state["frames"][state["fr_on"]][0] += 1
            uc.hook_add(UC_HOOK_CODE, frame_, begin=MAP["digicmi_wave_draw"], end=MAP["digicmi_wave_draw"])
        if os.environ.get("DIGICMI_LRTEST"):
            from unicorn import UC_HOOK_MEM_WRITE
            def aw(u, acc_, ad, sz, val, d):
                w = state.setdefault("aw", {})
                key = (u.reg_read(0x2c) if False else 0, sz)
                w[sz] = w.get(sz, 0) + 1
                if val:
                    state["aw_nz"] = state.get("aw_nz", 0) + 1
            from unicorn import UC_HOOK_MEM_READ
            for wa in [int(x, 16) for x in os.environ.get("DIGICMI_RDWATCH", "").split(",") if x]:
                def rw(u, acc_, ad, sz, val, d, wa=wa):
                    if state.get("lr_on"):
                        from unicorn.m68k_const import UC_M68K_REG_PC as _PC
                        state.setdefault("rw", {}).setdefault(wa, set()).add((u.reg_read(_PC), struct.unpack(">h", u.mem_read(ad & ~1, 2))[0]))
                uc.hook_add(UC_HOOK_MEM_READ, rw, begin=wa, end=wa + 1)
        if CMI:
            # digicmi velocity layers. Count the free lock lists at the start (leak check at the end).
            def nfree(u):
                n, p = 0, struct.unpack(">I", u.mem_read(LOCK_HEAD, 4))[0]
                while p and n < 200:
                    n, p = n + 1, struct.unpack(">I", u.mem_read(p + 16, 4))[0]
                return n
            state["free0"] = nfree(uc)
            state["nfree"] = nfree
            sweep = bool(os.environ.get("DIGICMI_VELS"))
            def route_in(u, ad, s_, d):          # each note into the router: optionally another velocity
                sp = u.reg_read(UC_M68K_REG_A7)
                m = struct.unpack(">I", u.mem_read(sp + 4, 4))[0]
                on, v, mid = (struct.unpack(">i", u.mem_read(m + o, 4))[0] for o in (4, 8, 12))
                if os.environ.get("DIGICMI_MSGLOG"):
                    ty, fl = (struct.unpack(">i", u.mem_read(m + o, 4))[0] for o in (0, 36))
                    state.setdefault("msgs", []).append((state["n"], ty, on, v, mid, fl,
                        struct.unpack(">ii", u.mem_read(AMP + 12 * (v & 7), 8))))
                if sweep and v == 0 and on == 1 and mid in (1, 2):
                    k = state.setdefault("vk", 0)
                    u.mem_write(m + 20, bytes([VELS[k % len(VELS)]]))
                    state["vk"] = k + 1
            uc.hook_add(UC_HOOK_CODE, route_in, begin=MAP["digipoly_route_c"], end=MAP["digipoly_route_c"])
            def cmi_start(u, ad, s_, d):         # a CMI voice starts: its note's velocity (integer byte of +20)
                m, v = u.reg_read(UC_M68K_REG_A2), u.reg_read(UC_M68K_REG_D2)
                kit = struct.unpack(">I", u.mem_read(ENG_KIT, 4))[0]
                snd = struct.unpack(">I", u.mem_read(m + 40, 4))[0]
                if kit and kit + 0x20 + 0xa2 <= snd < kit + 0x20 + 8 * 0xa2 and u.mem_read(MAP["digicmi_poly8"], 1)[0]:
                    state.setdefault("foreign", []).append((state["n"], v, (snd - kit - 0x20) // 0xa2))
                if not kit or not snd or u.mem_read(snd + 0x7e, 1)[0] != POLY_ID:
                    return                       # not a CMI sound (a sound's machine byte is +0x7e)
                state.setdefault("cmi_pend", []).append([state["n"], v, u.mem_read(m + 20, 1)[0], 0, 0, False])
            uc.hook_add(UC_HOOK_CODE, cmi_start, begin=VOICE_START, end=VOICE_START)
            def cmi_pan(u, ad, s_, d):           # 16 blocks into each CMI note: its voice's PAN, settled
                for p in state.get("cmi_pend", []):
                    if len(p) == 6:
                        p.append(0)
                    if len(p) == 7:
                        p[6] += 1
                        if p[6] == 16:
                            p.append(struct.unpack(">h", u.mem_read(0x80002794 + 2 * (53 * p[1] + 45 - 17), 2))[0])
            uc.hook_add(UC_HOOK_CODE, cmi_pan, begin=RENDER_DONE, end=RENDER_DONE)
            def cmi_blocks(u, ad, s_, d):        # the players have run: the velocity each CMI voice was given
                for p in state.get("cmi_pend", []):
                    if p[4] == 0:
                        p[3] = struct.unpack(">h", u.mem_read(VOICE_VEL + 2 * p[1], 2))[0]
                        p[4] = 1
            uc.hook_add(UC_HOOK_CODE, cmi_blocks, begin=RENDER_DONE, end=RENDER_DONE)
        if os.environ.get("DIGICMI_LOOPPROBE"):
            def lp(u, ad, s_, d):
                a3 = u.reg_read(UC_M68K_REG_A3)
                rec = (state["n"], u.reg_read(UC_M68K_REG_D0) & 0xffffffff, u.reg_read(UC_M68K_REG_D1) & 0xffffffff,
                       u.reg_read(UC_M68K_REG_D2) & 0xff, struct.unpack(">i", u.mem_read(0x8000edc8, 4))[0],
                       struct.unpack(">I", u.mem_read(0x80001228, 4))[0] & 1,
                       struct.unpack(">ii", u.mem_read(AMP, 8)), struct.unpack(">I", u.mem_read(0x8000edd8, 4))[0])
                rec = rec + (struct.unpack(">i", u.mem_read(0x8000edd4, 4))[0], u.mem_read(0x8000eded, 1)[0],
                             u.mem_read(0x8000edec, 1)[0], u.mem_read(0x8000edee, 1)[0])
                state.setdefault("lp", []).append(rec)
            uc.hook_add(UC_HOOK_CODE, lp, begin=PLAYREAD0, end=PLAYREAD0)
            def brblk(u, ad, s_, d):             # voice 0's block, while the held key's note sounds
                if 1390 <= state["n"] <= 1440:
                    state.setdefault("brblk", []).append(struct.unpack(">32i", u.mem_read(0x80001a18, 128)))
                    state.setdefault("allblk", []).append([(struct.unpack(">32i", u.mem_read(0x80001a18 + 128 * v, 128)),
                        struct.unpack(">i", u.mem_read(0x8000EDC4 + 94 * v + 0x10, 4))[0],
                        struct.unpack(">i", u.mem_read(0x80001f28 + 4 * v, 4))[0] >> 16,
                        u.mem_read(0x8000EDC4 + 94 * v + 0x28, 1)[0]) for v in range(4)])
                    state.setdefault("brgain", []).append((struct.unpack(">i", u.mem_read(0x8000edd4, 4))[0],
                        struct.unpack(">ii", u.mem_read(AMP, 8)), struct.unpack(">i", u.mem_read(0x8000edc8, 4))[0]))
            uc.hook_add(UC_HOOK_CODE, brblk, begin=0x40077fc0, end=0x40077fc0)   # after machine-pages' render event
            def cmi_voice(u, v):                 # the voice has a CMI sound loaded (cmi_loop.c's is_cmi)
                snd = struct.unpack(">I", u.mem_read(0x800014f0 + 0x131 * 4 + 4 * v, 4))[0]
                return snd >= 0x40000000 and u.mem_read(snd + 0x7e, 1)[0] == POLY_ID
            def vrec(u, v):
                base = 0x8000EDC4 + 94 * v
                state.setdefault("vr", []).append((state["n"], v, u.reg_read(UC_M68K_REG_D0) & 0xffffffff,
                    u.reg_read(UC_M68K_REG_D1) & 0xffffffff, u.reg_read(UC_M68K_REG_D2) & 0xff,
                    struct.unpack(">i", u.mem_read(base + 4, 4))[0],
                    (struct.unpack(">I", u.mem_read(0x80001228, 4))[0] >> v) & 1,
                    struct.unpack(">i", u.mem_read(0x80001f28 + 4 * v, 4))[0] >> 16,
                    u.mem_read(base + 0x28, 1)[0], struct.unpack(">I", u.mem_read(base + 0x14, 4))[0],
                    struct.unpack(">i", u.mem_read(AMP + 12 * v, 4))[0], cmi_voice(u, v)))
            if LOOPTEST:
                uc.hook_add(UC_HOOK_CODE, lambda u, ad, s_, d: vrec(u, 0), begin=PLAYREAD0, end=PLAYREAD0)
                uc.hook_add(UC_HOOK_CODE, lambda u, ad, s_, d: vrec(u, u.reg_read(UC_M68K_REG_D4)),
                            begin=0x400759ac, end=0x400759ac)
            def stg(u, ad, s_, d):               # just after the loop-flag stage's PLAY read (digicmi's hook)
                if 1440 <= state["n"] <= 1470:
                    state.setdefault("stg", []).append((state["n"], u.reg_read(UC_M68K_REG_A4),
                        u.reg_read(UC_M68K_REG_D0) & 0xffff, u.reg_read(UC_M68K_REG_A6)))
            uc.hook_add(UC_HOOK_CODE, stg, begin=0x400760c2, end=0x400760c2)
        if CMI:
            def ledp(u, ad, s_, d):              # every LED frame, after digicmi's hook: the track keys' LEDs
                mgr, tab = u.reg_read(UC_M68K_REG_A2), struct.unpack(">I", u.mem_read(0x40610638, 4))[0]
                leds = struct.unpack(">8I", u.mem_read(tab, 32))
                blue = sum(1 for l in leds if struct.unpack(">I", u.mem_read(mgr + 44 + 4 * l, 4))[0] == 2
                           and struct.unpack(">I", u.mem_read(mgr + 224 + 4 * l, 4))[0] == 0x13)
                key = (int(struct.unpack(">I", u.mem_read(0x4217bbb4, 4))[0] != 0), u.mem_read(MAP["digicmi_poly8"], 1)[0])
                st = state.setdefault("ledstat", {}).setdefault(key, [0, 0, 0])
                st[0] += 1; st[1] += blue == 8; st[2] += blue > 0
            uc.hook_add(UC_HOOK_CODE, ledp, begin=0x40008482, end=0x40008482)
        if os.environ.get("DIGICMI_LEDTRACE"):
            def ledset(u, ad, s_, d):            # every key-LED set: (caller, mgr, led, mode, colour)
                sp = u.reg_read(UC_M68K_REG_A7)
                ra, mgr, led, mode, col = struct.unpack(">5I", u.mem_read(sp, 20))
                st = struct.unpack(">8I", u.mem_read(u.reg_read(0x100 if False else UC_M68K_REG_A7) + 20, 32))
                state.setdefault("leds", {}).setdefault((ra, mgr, mode, col), [0, led, [hex(x) for x in st if 0x40000400 <= x < 0x40200000][:6]])[0] += 1
            uc.hook_add(UC_HOOK_CODE, ledset, begin=0x400083a0, end=0x400083a0)
        if os.environ.get("DIGICMI_WORDS") and "digipoly_levels" in MAP:
            def words_(u, ad, s_, d):            # the smoothed block digipoly_levels gets: its first 96 words
                state["wcalls"] = state.get("wcalls", 0) + 1
                if state["n"] in (1300, 1420, 2060) and state["n"] not in state.get("words", {}):
                    sp = u.reg_read(UC_M68K_REG_A7)
                    w = struct.unpack(">I", u.mem_read(sp + 4, 4))[0]
                    state.setdefault("words", {})[state["n"]] = (w, struct.unpack(">96h", u.mem_read(w, 192)))
            uc.hook_add(UC_HOOK_CODE, words_, begin=MAP["digipoly_levels"], end=MAP["digipoly_levels"])
        if os.environ.get("DIGICMI_PROBE"):
            # which message field the render's velocity comes from: keep each starting voice's message,
            # then catch the render writing fw_voice_vel (0x80001f18, s16 a track) for that voice
            from unicorn import UC_HOOK_MEM_WRITE
            def pmsg(u, ad, s_, d):
                state.setdefault("pm", {})[u.reg_read(UC_M68K_REG_D2)] = bytes(
                    u.mem_read(u.reg_read(UC_M68K_REG_A2), 0x4c))
            def pvel(u, acc, ad, size, val, d):
                v = (ad - 0x80001f18) // 2
                pm = state.get("pm", {}).pop(v, None)
                if pm is not None:
                    state.setdefault("pv", []).append((state["n"], v, val & 0xffff, pm, u.reg_read(0x2c)))
            uc.hook_add(UC_HOOK_CODE, pmsg, begin=VOICE_START, end=VOICE_START)
            uc.hook_add(UC_HOOK_MEM_WRITE, pvel, begin=0x80001f18, end=0x80001f27)
        if "digipoly_setparam" in MAP and os.environ.get("DIGICMI_SRCSHOT"):
            def sp_(u, ad, s_, d):               # set_param calls for the extras' slots
                sp = u.reg_read(UC_M68K_REG_A7)
                val, voice, slot = struct.unpack(">3i", u.mem_read(sp + 4, 12))
                if slot >= 46:
                    state.setdefault("setp", []).append((state["n"], val, voice, slot))
            uc.hook_add(UC_HOOK_CODE, sp_, begin=MAP["digipoly_setparam"], end=MAP["digipoly_setparam"])
        if "digicmi_steal" in MAP:
            # digicmi: each steal must take the quietest voice it may take. On entry, work out every
            # voice's eligibility exactly as poly.c does and read its AMP level; at the rts, d0 is the choice.
            S0 = MAP["digicmi_steal"]
            def u32(u, ad):
                return struct.unpack(">I", u.mem_read(ad, 4))[0]
            def s32(u, ad):
                return struct.unpack(">i", u.mem_read(ad, 4))[0]
            def steal_in(u, ad, s_, d):
                sp = u.reg_read(UC_M68K_REG_A7)
                used, fp, rest = struct.unpack(">3I", u.mem_read(sp + 4, 12))
                kit = u32(u, ENG_KIT)
                mutes = struct.unpack(">H", u.mem_read(MUTES, 2))[0] | u32(u, fp - 80)
                pend = set()
                m, n = rest, 0
                while m and n < 64:
                    ty, on, fl = s32(u, m), s32(u, m + 4), s32(u, m + 36)
                    if (ty < 2 or ty > 5) and on == 1 and (fl & 0x81) == 0x81 and not (fl & 0x40000) \
                            and s32(u, m + 12) == 1:
                        pend.add(s32(u, m + 8))
                    m, n = u32(u, m + 72), n + 1
                elig = [not ((used >> v) & 1) and s32(u, OWNER + 4 * v) < 2 for v in range(8)]   # 8 TRACK POLY
                lvl = [abs(s32(u, AMP + 12 * v + 4)) for v in range(8)]   # stored negated: magnitude
                state["steal_in"] = (elig, lvl)
            def steal_out(u, ad, s_, d):
                if state.get("steal_in") and u.mem_read(ad, 2) == b"\x4e\x75":
                    elig, lvl = state.pop("steal_in")
                    state.setdefault("steals", []).append((state["n"], u.reg_read(UC_M68K_REG_D0), elig, lvl))
            uc.hook_add(UC_HOOK_CODE, steal_in, begin=S0, end=S0)
            uc.hook_add(UC_HOOK_CODE, steal_out, begin=S0, end=S0 + 0x200)
        if "digipoly_recnote" in MAP:
            def recn(u, ad, s_, d):
                sp = u.reg_read(0x100 + 15) if False else u.reg_read(UC_M68K_REG_A7)
                a = struct.unpack(">3I", u.mem_read(sp + 4, 12))
                st = struct.unpack(">i", u.mem_read(fw(0x4020c29c), 4))[0]
                state.setdefault("rec", []).append((a[1], a[2], st))
            uc.hook_add(UC_HOOK_CODE, recn, begin=MAP["digipoly_recnote"], end=MAP["digipoly_recnote"])

            def liveon(u, ad, s_, d):
                sp = u.reg_read(UC_M68K_REG_A7)
                a = struct.unpack(">3I", u.mem_read(sp + 4, 12))
                if state.get("force"):
                    u.mem_write(fw(0x4020c29c), struct.pack(">i", state["force"]))
                st = struct.unpack(">i", u.mem_read(fw(0x4020c29c), 4))[0]
                state.setdefault("live", []).append((a[0], a[1], st))
            uc.hook_add(UC_HOOK_CODE, liveon, begin=fw(0x400d53dc), end=fw(0x400d53dc))
    for when, act in PLAN:
        if when != state["n"]:
            continue
        k = act[0]
        kit = struct.unpack(">I", uc.mem_read(UI_KIT, 4))[0]
        if k in ("press", "release"):
            E.inbox.append((k, act[1], 0))
        elif k == "encoder":
            E.inbox.append(("encoder", act[1], act[2]))
        elif k == "snap":
            png(os.path.join(a.png, act[1] + ".png"), E.fb)
            if "digicmi_uiticks" in MAP:
                log.append("snap %s at UI tick %d" % (act[1], struct.unpack(">i", uc.mem_read(MAP["digicmi_uiticks"], 4))[0]))
        elif k == "memsnap":
            if os.environ.get("DIGICMI_MEMDIFF"):
                state.setdefault("snaps", {})[act[1]] = [bytes(uc.mem_read(a0, n0)) for a0, n0 in MEM_REGIONS]
        elif k == "mlog":
            m_ = [uc.mem_read(kit + 0x9e + i * 0xa2, 1)[0] for i in range(8)]
            state.setdefault("mlog", []).append((act[1], m_[:3]))
        elif k == "slots8bit":
            log.append("8BIT/ENG slots (50, 51) %s: %s" % (act[1], [hex(x & 0xffff) for x in
                       struct.unpack(">2h", uc.mem_read(kit + 0x20 + 0x14 + 2 * 50, 4))]))
        elif k == "prof":
            import bisect
            from unicorn import UC_HOOK_CODE as _HC
            if "prof_index" not in state:
                owners = []
                for mp in a.mods:
                    d_ = json.load(open(mp)); mid = d_["id"]
                    for nm in d_.get("symbols", {}):
                        ad_ = MAP.get(nm)
                        if isinstance(ad_, int) and 0x47000000 <= ad_ < 0x48000000:
                            owners.append((ad_, mid, nm))
                owners.sort()
                state["prof_index"] = ([o[0] for o in owners], owners)
            keys_, owners_ = state["prof_index"]
            pr = {"label": act[1], "blocks": 0, "inside": False, "by": {}, "fn": {}}
            state["prof_cur"] = pr
            def in_(u, ad, s_, d):
                pr["inside"] = True
            def out_(u, ad, s_, d):
                if pr["inside"]:
                    pr["blocks"] += 1
                pr["inside"] = False
                if pr["blocks"] >= 100:
                    for h in pr["hooks"]:
                        u.hook_del(h)
                    u.ctl_flush_tb()
                    state.setdefault("prof", []).append(pr)
            def ins_(u, ad, s_, d):
                if not pr["inside"]:
                    return
                if ad < 0x47000000:
                    o, fn_ = "firmware", "firmware"
                else:
                    i_ = bisect.bisect_right(keys_, ad) - 1
                    o, fn_ = (owners_[i_][1], owners_[i_][2]) if i_ >= 0 else ("?", "?")
                pr["by"][o] = pr["by"].get(o, 0) + 1
                if o == "digicmi":
                    pr["fn"][fn_] = pr["fn"].get(fn_, 0) + 1
            pr["hooks"] = [uc.hook_add(UC_HOOK_CODE, in_, begin=0x40077428, end=0x40077428),
                           uc.hook_add(UC_HOOK_CODE, out_, begin=0x400784c8, end=0x400784c8),
                           uc.hook_add(UC_HOOK_CODE, ins_, begin=0, end=0xffffffff)]
            uc.ctl_flush_tb()                    # hooks added mid-run apply only to code translated afterwards
        elif k == "frames":
            state["fr_on"] = act[1]
            if act[1]:
                state.setdefault("frames", {})[act[1]] = [0, state["n"]]
            elif state.get("fr_last"):
                state["frames"][state["fr_last"]][1] = state["n"] - state["frames"][state["fr_last"]][1]
            state["fr_last"] = act[1] or state.get("fr_last")
        elif k == "lr":
            state["lr_on"] = act[1]
            if act[1]:
                state.setdefault("lr", {})[act[1]] = [0, 0, 0]
        elif k == "ledprobe":
            state["ledwant"] = act[1]
        elif k == "poly8":
            bits = [uc.mem_read(KIT0 + p * 0x91e + xb(45), 1)[0] & 1 for p in range(128)]
            state.setdefault("poly8", {})[act[1]] = (sum(bits), uc.mem_read(MAP["digicmi_poly8"], 1)[0])
        elif k == "phase":
            state["phase"] = act[1]
        elif k == "chord":
            uc.mem_write(PAT + 0x385, bytes(act[1]))
        elif k == "machines":
            m_ = [uc.mem_read(kit + 0x9e + i * 0xa2, 1)[0] for i in range(8)]
            log.append("machines: %s" % m_)
            if os.environ.get("DIGICMI_LOOPPROBE"):
                import math
                n = TEST_LEN
                hz_ = float(os.environ.get("DIGICMI_TESTHZ", "440"))          # the test sample: sine, saw or noise
                wave_ = os.environ.get("DIGICMI_TESTWAVE", "sine")
                if wave_ == "saw":
                    vals_ = [12000 * (2 * ((i * hz_ / 48000) % 1.0) - 1) for i in range(n)]
                elif wave_ == "noise":
                    import random; random.seed(1); vals_ = [random.uniform(-12000, 12000) for i in range(n)]
                else:
                    vals_ = [12000 * math.sin(2 * math.pi * hz_ * i / 48000) for i in range(n)]
                pcm = b"".join(struct.pack(">h", int(x)) for x in vals_)
                state["test_pcm"] = [int(x) for x in vals_]
                if os.environ.get("DIGICMI_REGIONS"):
                    for r0, r1, pr in uc.mem_regions():
                        log.append("region 0x%08x-0x%08x %d" % (r0, r1, pr))
                if not any(r0 <= TEST_PCM <= r1 for r0, r1, _ in uc.mem_regions()):
                    uc.mem_map(TEST_PCM & ~0xfffff, 0x100000)   # the empty sample pool: never touched yet
                uc.mem_write(TEST_PCM, pcm)
                slot = struct.unpack(">H", uc.mem_read(kit + 0x20 + 0x14 + 2 * 20, 2))[0] >> 8   # SAMP (slot 20)
                uc.mem_write(SMP_TAB + 16 * slot, struct.pack(">4I", TEST_PCM, 0, n, 0x40000000))
                for t_ in range(1, 8):           # and every other track's sample slot, for the stock views
                    sl_ = struct.unpack(">H", uc.mem_read(kit + 0x20 + t_ * 0xa2 + 0x14 + 2 * 20, 2))[0] >> 8
                    uc.mem_write(SMP_TAB + 16 * sl_, struct.pack(">4I", TEST_PCM, 0, n, 0x40000000))
                ek = struct.unpack(">I", uc.mem_read(ENG_KIT, 4))[0]
                for k_ in (kit, ek):             # PLAY = FWD.L (2), LOOP = 60 (half way), on both kits' sound
                    uc.mem_write(k_ + 0x20 + 0x14 + 2 * 18, struct.pack(">h", PROBE_PLAY << 8))
                    uc.mem_write(k_ + 0x20 + 0x14 + 2 * 23, struct.pack(">h", PROBE_LOOP << 8))
                if os.environ.get("DIGICMI_LEN") is not None:     # LEN (slot 22) on both kits and every voice copy
                    lv_ = int(os.environ["DIGICMI_LEN"]) << 8
                    for k_ in (kit, ek):
                        uc.mem_write(k_ + 0x20 + 0x14 + 2 * 22, struct.pack(">h", lv_))
                    for w in range(8):
                        uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 22), struct.pack(">h", lv_))
                if os.environ.get("DIGICMI_ENG") is not None:     # ENG (slot 51) on both kits and every voice copy
                    ev_ = int(os.environ["DIGICMI_ENG"]) << 8
                    for k_ in (kit, ek):
                        uc.mem_write(k_ + 0x20 + 0x14 + 2 * 51, struct.pack(">h", ev_))
                    for w in range(8):
                        uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 51), struct.pack(">h", ev_))
                if os.environ.get("DIGICMI_PANSET") is not None:   # the track's PAN (slot 45)
                    for k_ in (kit, ek):
                        uc.mem_write(k_ + 0x20 + 0x14 + 2 * int(os.environ.get("DIGICMI_PANSLOT", "45")), struct.pack(">h", int(os.environ["DIGICMI_PANSET"]) << 8))
                if os.environ.get("DIGICMI_BR") is not None:   # BR (slot 19) for the bit-depth probe
                    br = int(os.environ["DIGICMI_BR"])
                    for k_ in (kit, ek):
                        uc.mem_write(k_ + 0x20 + 0x14 + 2 * 19, struct.pack(">h", br << 8))
                    for w in range(8):
                        uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 19), struct.pack(">h", br << 8))
                for k_ in (kit, ek):             # the loop page's extras: slots 46-49
                    for j, x in enumerate(PROBE_EXTRAS):
                        uc.mem_write(k_ + 0x20 + 0x14 + 2 * (46 + j), struct.pack(">h", x << 8))
                for w in range(8):
                    for j, x in enumerate(PROBE_EXTRAS):
                        uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 46 + j), struct.pack(">h", x << 8))
                for w in range(8):               # and every voice's engine copy
                    uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 18), struct.pack(">h", PROBE_PLAY << 8))
                    uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 23), struct.pack(">h", PROBE_LOOP << 8))
                log.append("probe: sample slot %d -> %d samples at 0x%x; PLAY %d LOOP %d; LLEN LMOD DIR LFIN %s"
                           % (slot, n, TEST_PCM, PROBE_PLAY, PROBE_LOOP, PROBE_EXTRAS))
            if os.environ.get("DIGICMI_SMPTAB"):
                tab = uc.mem_read(SMP_TAB, 16 * 256)
                for i in range(256):
                    e = struct.unpack(">4I", tab[16 * i: 16 * i + 16])
                    if any(e):
                        log.append("sample slot %3d: pcm 0x%08x  +4 0x%08x  len 0x%08x (%d)  +12 0x%08x"
                                   % (i, e[0], e[1], e[2], e[2], e[3]))
                log.append("sample table: %d slots in use" % sum(1 for i in range(256) if any(tab[16 * i: 16 * i + 16])))
            sl = struct.unpack(">53h", uc.mem_read(kit + 0x20 + 0x14, 106))
            log.append("track 1 slots 17-24 (A-H): %s; 46-49 (extras 8-11): %s"
                       % ([hex(x & 0xffff) for x in sl[17:25]], [hex(x & 0xffff) for x in sl[46:50]]))
            if m_[0] != POLY_ID:                # POLY: machine 6 (digipoly 2.0); digicmi's CMI: 40
                fails.append("track 1 is not POLY (machine %d)" % POLY_ID)
        elif k == "level":
            state.setdefault("lev", []).append((act[1], list(uc.mem_read(kit + 0x10, 8))[0]))
            log.append("%-16s track 1 level %d" % (act[1], list(uc.mem_read(kit + 0x10, 8))[0]))
        elif k == "force":
            state["force"] = act[1]
        elif k == "clearnotes":
            for i in range(4):
                uc.mem_write(PAT + 0x280 + 0x40 * i, b"\xff" * 64)
        elif k == "notes":
            d = [list(uc.mem_read(PAT + 0x280 + 0x40 * i, 64)) for i in range(4)]
            rows = [(st, [d[i][st] for i in range(4)]) for st in range(64)
                    if d[0][st] != 0xff]
            state["notes"] = rows
            log.append("%-16s steps with a note: %s" % (act[1], rows[:6]))
        elif k == "locks":
            lk = [uc.mem_read(kit + xb(6 * i + 3), 1)[0] >> 7 for i in range(8)]
            log.append("%-16s kit %s  out of the POLY pool: %s" % (act[1], hex(kit), lk))
            state.setdefault("locks", []).append((act[1], kit, lk))
        elif k == "follow":
            loaded = struct.unpack(">8I", uc.mem_read(LOADED, 32))
            prm = struct.unpack(">%dh" % (8 + 53 * 8), uc.mem_read(PARAMS, 2 * (8 + 53 * 8)))
            blk = [prm[8 + 53 * w: 8 + 53 * (w + 1)] for w in range(8)]
            lv = struct.unpack(">8h", uc.mem_read(LEVELS, 16))
            own = kit + 0x20
            stolen = [w for w in range(1, 8) if loaded[w] == own]
            badlv = [w for w in stolen if lv[w] != lv[0]]
            log.append("%-22s chord voices %s; levels %s" % (act[1], stolen, lv))
            log.append("%-22s slot 0x2d: voices %s; kit tracks %s; loaded-track %s" % ("", [b[0x2d] for b in blk],
                [struct.unpack(">h", uc.mem_read(kit + 0x20 + t * 0xa2 + 0x14 + 2 * 0x2d, 2))[0] for t in range(8)],
                [(x - kit - 0x20) / 0xa2 for x in loaded]))
            env = [struct.unpack(">ii", uc.mem_read(AMP + 12 * w, 8)) for w in range(8)]
            log.append("%-22s AMP envelope (phase, level) %s" % ("", env))
            state.setdefault("follow", []).append((act[1], stolen, badlv, lv, blk))
            state.setdefault("followkit", []).append(struct.unpack(">53h", uc.mem_read(kit + 0x20 + 0x14, 106)))
    if "nfree" in state:
        state["free_last"] = state["nfree"](uc)
    if LOOPTEST == "live" and state["n"] == LIVE_STEP:   # a knob turned while the chord sounds
        for w in range(8):
            uc.mem_write(0x800014f2 + 2 * (53 * w + 8 + 23), struct.pack(">h", LIVE_LOOP << 8))
    if os.environ.get("DIGICMI_PITREGS") and state["n"] in (100, 3000):
        for ch, base in enumerate((0xFC080000, 0xFC084000, 0xFC088000, 0xFC08C000)):
            pcsr, pmr, pcntr = struct.unpack(">HHH", uc.mem_read(base, 6))
            pre = 1 << (((pcsr >> 8) & 0xF) + 1)
            per_s = pre * (pmr + 1) / 132e6
            log.append("PIT%d step %d: PCSR 0x%04x PMR %d PCNTR %d prescaler %d -> period %.4f ms, tick %.3f us, %.1f ticks a 667 us block"
                       % (ch, state["n"], pcsr, pmr, pcntr, pre, per_s * 1e3, pre / 132e6 * 1e6, 666.7e-6 / (pre / 132e6)))
    if os.environ.get("DIGICMI_VDUMP") and state["n"] in (2100, 2500):   # the player's voice states, for the PCM pointer
        for v_ in range(8):
            raw_ = uc.mem_read(0x8000EDC4 + 94 * v_, 96)
            hits = [o for o in range(0, 92, 2) if 0x44000000 <= struct.unpack(">I", raw_[o:o + 4])[0] < 0x44100000]
            log.append("V(%d) step %d: on %d, words at the test PCM: %s %s" % (v_, state["n"], raw_[0x28], hits,
                       [hex(struct.unpack(">I", raw_[o:o + 4])[0]) for o in hits]))
    if "digicmi_cpu_load" in MAP:                 # the CPU meter: what it measured, and a fake value for screenshots
        cl_ = struct.unpack(">i", uc.mem_read(MAP["digicmi_cpu_load"], 4))[0]
        state.setdefault("cpu_seen", set()).add(cl_)
        if os.environ.get("DIGICMI_FAKELOAD"):
            uc.mem_write(MAP["digicmi_cpu_load"], struct.pack(">i", int(os.environ["DIGICMI_FAKELOAD"])))
    if state.get("lr_on") and "fpos" in MAP:     # the time synth's voice 0: its place among the waves (16.16)
        fp_ = struct.unpack(">i", uc.mem_read(MAP["fpos"], 4))[0]
        state.setdefault("fpos", {}).setdefault(state["lr_on"], []).append(fp_)
    if state.get("lr_on"):
        tb = struct.unpack(">256i", uc.mem_read(0x80001a18, 1024))
        state["lr"][state["lr_on"]].append(sum(abs(x) >> 8 for x in tb))
    state["n"] += 1
    r = _spin(m, pc, *args, **kw)
    if state["n"] >= STEPS:
        E.stop_flag.set()
    return r
G.spin = spin

E = G.Emulator(SNAP, syx=os.environ["DT2_SYX"], realtime=False, audio=True)
t0 = time.time()
E.run()
print("error:", E.error, "steps:", state["n"], "%.0fs" % (time.time() - t0))
print("\n".join(log))

# ---- checks ----
key = starts.get("key", [])
kv = [e[1] for e in key]
print("the track's key started: %s" % (key,))
if len(key) != 4 or len(set(kv)) != 4 or 0 not in kv or any(e[4] != 0 for e in key):
    fails.append("the track's key did not play the chord on four voices with its own sound: %s" % (key,))
elif sorted(e[2] for e in key) != sorted([60, 64, 67, 71]):
    fails.append("the key's chord notes are %s" % (sorted(e[2] for e in key),))

g = {}
for n, v, note, mid, st in starts.get("play", []):
    g.setdefault(n, []).append((v, note, mid, st))
chords = 0
for n in sorted(g):
    ev = g[n]
    poly = [e for e in ev if e[3] == 0]
    if len(poly) < 2:
        continue
    chords += 1
    if chords <= 3:
        print("  step %5d: voices %s notes %s" % (n, [e[0] for e in poly], sorted(e[1] for e in poly)))
    if len(set(e[0] for e in poly)) != len(poly):
        fails.append("a voice was used twice in one chord")
if chords < 2:
    fails.append("only %d chords played while the sequencer ran" % chords)

lev = state.get("lev", [])
print("TRIG page level: %s" % (lev,))
if len(lev) != 2 or lev[1][1] >= lev[0][1]:
    fails.append("the LEVEL knob did not lower the track level on the POLY TRIG page: %s" % (lev,))
print("liveNoteOn calls (track, note, step): %s" % (state.get("live", [])[:8],))
print("digipoly_recnote calls (track, note, step): %s" % (state.get("rec", [])[:6],))
rows = state.get("notes", [])
if not rows:
    fails.append("no note was recorded onto the POLY track")
elif not any(r[1][0] <= 127 and r[1][1] == 0x40 for r in rows):
    fails.append("the recorded step does not hold a note and an empty chord: %s" % (rows[:4],))

fl = state.get("follow", [])
if len(fl) == 3:
    # the words the knob itself moved on the POLY track's own voice (the rest of the block is the
    # engine's running state for that voice, which differs from voice to voice by nature)
    moved = [sl for sl in range(53) if fl[1][4][0][sl] != fl[0][4][0][sl]]
    if CMI:                     # only what the knob moved in the sound: a voice's other words follow its trig's p-locks
        fk = state["followkit"]
        moved = [sl for sl in range(53) if fk[1][sl] != fk[0][sl] and sl != 0]
        print("the FLTR knob moved sound slots %s: %s -> %s" % ([hex(x) for x in moved], [fk[0][x] for x in moved], [fk[1][x] for x in moved]))
    print("knob E moved slots %s on the POLY voice" % [hex(x) for x in moved])
    if not moved:
        fails.append("knob E changed nothing on the POLY track")
    for name, stolen, badlv, lv, blk in fl[1:]:
        if not stolen:
            fails.append("%s: no voice was playing the POLY track's sound" % name)
        # every chord voice must carry the knob's value. Right after the turn the voices that are
        # sounding are still ramping towards it (the engine smooths a playing voice), so the strict
        # comparison waits for the settled snapshot, by which time voices have also been stolen anew.
        if name.startswith("after more"):
            ref = [0] + stolen
            bad = [(w, hex(sl), blk[w][sl]) for w in ref for sl in moved if blk[w][sl] != blk[0][sl]]
            if bad:
                fails.append("%s: the knob did not reach every chord voice %s" % (name, bad[:6]))
        elif not [w for w in stolen if all(blk[w][sl] == blk[stolen[0]][sl] for sl in moved)]:
            fails.append("%s: the knob reached no chord voice" % name)
        if badlv:
            fails.append("%s: voices %s are not at the POLY track's level (%s)" % (name, badlv, lv))
    if fl[1][3][0] == fl[0][3][0]:
        fails.append("the LEVEL knob changed nothing on the POLY track")
else:
    fails.append("the knob checks did not run")

lk = state.get("locks", [])
if CMI:
    p8 = state.get("poly8", {})
    print("8 TRACK POLY via the menu (kits set of 128, mode): %s" % p8)
    # 0.10: no SETTINGS row any more (CMI on track 1 is the mode)
    print("foreign voice starts (tracks 2-8's own sounds) with the mode on: %s" % state.get("foreign", [])[:6])
    if state.get("foreign"):
        fails.append("tracks 2-8 still started %d voices of their own sounds" % len(state["foreign"]))
    ls_ = state.get("ledstat", {})
    print("LED frames (FUNC held, mode): [frames, all 8 blue, any blue]: %s" % ls_)
    held = ls_.get((1, 1), [0, 0, 0])
    if not held[0] or held[1] < held[0] - 1:
        fails.append("FUNC held in the mode: the eight keys were not blue in every frame: %s" % held)
    for k, v in ls_.items():
        if k != (1, 1) and v[2] > 1:
            fails.append("keys were blue in %d frames with (FUNC, mode) = %s" % (v[2], k))
elif len(lk) == 3:
    if lk[0][2][1] == 0:
        fails.append("SETTINGS did not take track 2 out of this pattern's pool")
    if lk[1][1] == lk[0][1]:
        fails.append("the second pattern uses the same kit, so this says nothing about per-pattern locks")
    elif lk[1][2][1] != 0:
        fails.append("the second pattern inherited the first pattern's voice allocation")
    if lk[2][2] != lk[0][2]:
        fails.append("the first pattern lost its voice allocation (%s -> %s)" % (lk[0][2], lk[2][2]))
else:
    fails.append("the lock checks did not run")

if CMI:
    # velocity -> volume is the stock render's: gain = (vel / 127)^2 x (VOL / 127) (0x40074c60, both
    # players). What digicmi must do is hand every CMI voice, a stolen one included, its own note's velocity.
    cp = [p for p in state.get("cmi_pend", []) if p[4]]
    bad = [(p[0], p[1], p[2], p[3]) for p in cp if p[3] != p[2] << 8]
    vels = sorted(set(p[2] for p in cp))
    print("CMI notes: %d; velocities %s; voices: %s" % (len(cp), vels, sorted(set(p[1] for p in cp))))
    pans = [(p[0], p[1], p[7]) for p in cp if len(p) > 7]
    print("CMI notes' PAN at their start (step, voice, PAN 0-127): %s" % [(n, v, round(x / 256, 1)) for n, v, x in pans[:16]])
    for n, v, vel, got in bad[:6]:
        fails.append("CMI note at step %d on voice %d: velocity %d, but the voice was given %.2f" % (n, v, vel, got / 256))
    if not cp:
        fails.append("no CMI note started a voice")
    if os.environ.get("DIGICMI_VELS") and len(vels) < 4:
        fails.append("the velocity sweep reached only %d velocities" % len(vels))
    print("lock lists free: %d at the start, %d at the end" % (state.get("free0", -1), state.get("free_last", -1)))
    if state.get("free_last", 0) < state.get("free0", 0) - 8:
        fails.append("lock lists leak: %d free at the start, %d at the end" % (state["free0"], state["free_last"]))

if os.environ.get("DIGICMI_MSGLOG"):
    for n, ty, on, v, mid, fl, env in state.get("msgs", [])[:80]:
        print("  msg step %5d type %d on %d voice %d src %2d flags 0x%08x env %s" % (n, ty, on, v, mid, fl & 0xffffffff, env))

if state.get("snaps") and len(state["snaps"]) == 5:
    sn = state["snaps"]
    for ri, (a0, n0) in enumerate(MEM_REGIONS):
        A, Bb, C, Dd, E = (sn[k][ri] for k in "abcde")
        for i in range(n0):
            if A[i] == C[i] == E[i] and Bb[i] == Dd[i] != A[i]:
                print("memdiff 0x%08x: released %02x held %02x" % (a0 + i, A[i], Bb[i]))

if os.environ.get("DIGICMI_LEDTRACE"):
    print("led sets (caller, mgr, mode, colour): count, first led, stack")
    for k, v in sorted(state.get("leds", {}).items(), key=lambda kv: -kv[1][0])[:30]:
        print("  led", hex(k[0]), hex(k[1]), k[2], hex(k[3]), v)

if LOOPTEST:
    def sc(val, ln):                              # cmi_loop.c's scale(), independently
        return (ln // 0x7800) * val + ((ln % 0x7800) * val) // 0x7800
    L = TEST_LEN
    llen, lmod, dirx, lfin = PROBE_EXTRAS[:4]     # (LLEN gone in 0.13, LFIN in 0.20: the loop runs LOOP -> END)
    lenv_ = min(int(os.environ.get("DIGICMI_LEN", "120")) << 8, 0x7800)
    se = sc(lenv_, L)                             # END (STRT is 0)
    ls = sc(min(max(PROBE_LOOP * 256 - 1, 0), lenv_), L)
    le = se
    pcm_ = state.get("test_pcm")
    def zc_(pos, lo, hi):                         # 0.23: cmi_loop.c's zc_near, independently
        lo, hi = max(lo, 1), min(hi, L - 1)
        for d in range(257):
            for i in ((pos + d,) if d == 0 else (pos + d, pos - d)):
                if lo <= i <= hi and pcm_[i - 1] < 0 <= pcm_[i]:
                    return i
        return pos
    if pcm_ and le - ls >= 320:
        le = zc_(le, ls + 320, L - 1)
        ls = zc_(ls, 1, le - 320)
    print("loop test %s: knobs LLEN LMOD DIR LFIN %s; expected loop [%d, %d], end %d" % (LOOPTEST, PROBE_EXTRAS, ls, le, se))
    s32 = lambda x: x - 2**32 if x >= 2**31 else x
    notes = []                                    # per voice, the blocks of each note on the test sample
    byv = {}
    for r in state.get("vr", []):
        byv.setdefault(r[1], []).append(r)
    for v, rs in byv.items():
        cur = None
        for r in rs:
            if r[6]:
                cur = [] if r[9] == L and r[11] else None    # a note of the CMI sound, on the test sample
                if cur is not None:
                    notes.append((v, cur))
            if cur is not None and not r[11]:
                cur = None                       # the voice went back to its own track's sound (Digi Poly)
            if cur is not None and r[9] == L:
                cur.append(r)
    lt_fail = []
    checked = speed = turns_total = released_ok = 0
    for v, rs in notes:
        if len(rs) < 40:
            continue
        note = rs[0][7]
        rate = 32 * 2 ** ((note - 60) / 12)          # samples a block at this note
        body = rs[1:]
        if LOOPTEST == "rev":                     # a reverse loop wraps upwards
            wraps = [i for i in range(1, len(body)) if body[i][5] > body[i - 1][5] + 200]
        else:
            wraps = [i for i in range(1, len(body)) if body[i][5] < body[i - 1][5] - 200]
        playing = [r for r in body if r[8]]           # the voice still on
        if LOOPTEST in ("hold", "lfin", "rev"):
            want_w = (le, ls, 1) if LOOPTEST == "rev" else (ls, le, 2)
            for r in body:
                if r[8] and (s32(r[2]), s32(r[3]), r[4]) != want_w:
                    lt_fail.append("voice %d note %d at step %d: window %d..%d PLAY %d, wanted %d..%d FWD.L"
                                   % (v, note, r[0], s32(r[2]), s32(r[3]), r[4], ls, le)); break
            for i in wraps:
                if LOOPTEST == "rev":
                    if not (le - rate - 64 <= body[i][5] <= le and body[i - 1][5] <= ls + rate + 64):
                        lt_fail.append("voice %d note %d: reverse wrap from %d to %d, not from %d to %d"
                                       % (v, note, body[i - 1][5], body[i][5], ls, le)); break
                    continue
                if not (ls <= body[i][5] <= ls + rate + 64 and body[i - 1][5] >= le - rate - 64):
                    lt_fail.append("voice %d note %d: wrapped from %d to %d, not from %d to %d"
                                   % (v, note, body[i - 1][5], body[i][5], le, ls)); break
            if len(wraps) >= 3:
                per = (wraps[-1] - wraps[0]) / (len(wraps) - 1)
                want = (le - ls) / rate
                speed += 1
                print("  voice %d note %d: a lap every %.1f blocks, %.1f wanted (x%.3f the root's speed)"
                      % (v, note, per, want, 75.0 / per if le - ls == 2400 else 0))
                if abs(per - want) > 0.05 * want + 1:
                    lt_fail.append("voice %d note %d: laps every %.1f blocks, wanted %.1f" % (v, note, per, want))
        if LOOPTEST == "alt":
            dirs = [r[4] for r in body]
            turns = sum(1 for i in range(1, len(dirs)) if dirs[i] != dirs[i - 1])
            turns_total += turns
            if wraps:
                lt_fail.append("voice %d note %d: jumped (%d -> %d) instead of turning"
                               % (v, note, body[wraps[0] - 1][5], body[wraps[0]][5]))
            inside = [r for r in body if r[5] >= ls]
            if any(r[5] > le + 64 or r[5] < ls - rate - 64 for r in inside[5:]):
                lt_fail.append("voice %d note %d: left the loop" % (v, note))
            tpos = [i for i in range(1, len(dirs)) if dirs[i] != dirs[i - 1]]
            if len(tpos) >= 4:
                per = (tpos[-1] - tpos[0]) / (len(tpos) - 1)
                want = (le - ls) / rate
                speed += 1
                print("  voice %d note %d: a turn every %.1f blocks, %.1f wanted" % (v, note, per, want))
                if abs(per - want) > 0.08 * want + 2:
                    lt_fail.append("voice %d note %d: turns every %.1f blocks, wanted %.1f" % (v, note, per, want))
        if LOOPTEST == "sus":
            rel = next((i for i, r in enumerate(body) if r[10] == 0), None) if False else None
            after = [i for i, r in enumerate(body) if r[4] == 3]
            if after:
                i0 = after[0]
                held = body[:i0]
                if any(r[4] != 2 or s32(r[3]) != le for r in held[:len(held)]):
                    lt_fail.append("voice %d note %d: not looping [%d, %d] while held" % (v, note, ls, le))
                tail = body[i0:]                 # 0.24: on past END to the sample's own end, L
                if any(r[4] != 3 or s32(r[3]) != L for r in tail if r[8]):
                    lt_fail.append("voice %d note %d: after release, not FWD to the sample's end %d" % (v, note, L))
                off = next((j for j, r in enumerate(tail) if not r[8]), None)
                if off is not None:
                    last = tail[off - 1][5] if off else tail[0][5]
                    if last < L - rate - 200:
                        lt_fail.append("voice %d note %d: stopped at %d, before the sample's end %d" % (v, note, last, L))
                    elif max(r[5] for r in tail[:off]) > se:
                        released_ok += 1
            elif L - se < 64:                    # 0.24: no tail after END: it keeps looping through the release
                late = [r for r in body if r[8] and r[0] > body[0][0] + 200]
                if late and all(r[4] == 2 for r in late):
                    released_ok += 1
                elif late:
                    lt_fail.append("voice %d note %d: no tail, but stopped looping after release" % (v, note))
        checked += 1
    if LOOPTEST == "live":                      # LOOP turned while the chord sounds: the loop's start moves
        held = [(v, rs) for v, rs in notes if rs and rs[0][0] <= LIVE_STEP <= rs[-1][0]]
        ls2 = sc(min(max(LIVE_LOOP * 256 - 1, 0), 0x7800), L)
        moved = 0
        for v, rs in held:
            starts_ = [s32(r[2]) for r in rs if r[8] and not r[6]]
            if ls in starts_ and ls2 in starts_ and starts_.index(ls2) > starts_.index(ls):
                moved += 1
        print("live: %d notes sounding at step %d; the loop start moved %d -> %d on %d of them"
              % (len(held), LIVE_STEP, ls, ls2, moved))
        if not held or moved < len(held):
            lt_fail.append("live: the loop start moved on %d of %d sounding notes" % (moved, len(held)))
    print("loop test %s: %d notes on the test sample checked, %d speed measurements, %d turns, %d releases played out"
          % (LOOPTEST, checked, speed, turns_total, released_ok))
    if checked < 2:
        lt_fail.append("only %d notes reached the test sample" % checked)
    if LOOPTEST in ("hold", "lfin", "alt", "rev") and speed < 2:
        lt_fail.append("too few laps to measure loop speed against pitch (%d)" % speed)
    if LOOPTEST == "sus" and not released_ok:
        lt_fail.append("no released note played its tail to the sample's end, or looped on with no tail")
    fails.extend(lt_fail)

if os.environ.get("DIGICMI_LOOPPROBE"):
    lp = state.get("lp", [])
    print("loop probe: %d blocks of voice 0" % len(lp))
    sg = state.get("stg", [])
    print("stage reads: %d; first: %s" % (len(sg), [(n, a4, hex(w), hex(a6)) for n, a4, w, a6 in sg[:12]]))
    print("stage reads at 1447+: %s" % [(n, a4, hex(w)) for n, a4, w, a6 in sg if n >= 1447][:16])
    bb = state.get("brblk", [])
    if bb:
        import math
        allv = [x for b in bb for x in b]
        pk = max(abs(x) for x in allv) or 1
        print("BR %s: %d blocks, peak %d, distinct values %d" % (os.environ.get("DIGICMI_BR"), len(bb), pk, len(set(allv))))
        if PROBE_EXTRAS[4] >= 64:                # RETRO: per voice, held runs that follow its pitch, and the 8-bit grid
            for v in range(4):
                rep = tot = off = 0
                note = None
                for blocks in state.get("allblk", [])[4:]:
                    blk_, g, nt, on = blocks[v]
                    if not on:
                        continue
                    note = nt
                    rep += sum(1 for i in range(1, 32) if blk_[i] == blk_[i - 1])
                    tot += 31
                    st_ = ((g & 0xffffffff) >> 8) * 207 >> 7
                    off += sum(1 for x in blk_ if st_ and int(x / st_) * st_ != x)
                if tot and note is not None:
                    r = 2 ** ((note - 60) / 12)
                    want = max(0.0, 1 - r / 2)
                    print("RETRO voice %d note %d: held %.3f of samples (24 kHz clock at this pitch: %.3f); %d off the 8-bit grid"
                          % (v, note, rep / tot, want, off))
                    # the 8-bit cut alone makes some neighbours equal too: never below the clock's holds, a little above
                    if not (want - 0.02 <= rep / tot <= want + 0.10) or off:
                        fails.append("RETRO voice %d note %d: held %.3f, wanted %.3f; %d off grid" % (v, note, rep / tot, want, off))
        if False and PROBE_EXTRAS[4] >= 64:
            gains = [g for g, env, pos in state["brgain"]]
            off = 0
            for blk_, g in zip(bb, gains):
                st_ = ((g & 0xffffffff) >> 8) * 207 >> 7
                off += sum(1 for x in blk_ if st_ and int(x / st_) * st_ != x)
            lv = sorted(set(int(x / (((g & 0xffffffff) >> 8) * 207 >> 7)) for blk_, g in zip(bb, gains) for x in blk_))
            print("8BIT: %d values off the 8-bit grid; levels used %d (%d..%d of -128..127)" % (off, len(lv), lv[0], lv[-1]))
            if off or len(lv) > 256:
                fails.append("8BIT: %d values off the grid, %d levels" % (off, len(lv)))
        if PROBE_EXTRAS[5] >= 64:
            print("SPEC: voice 0's peak while the key is held: %d" % pk)
            if pk > 1:
                fails.append("ENG = SPEC: voice 0 still sounds (peak %d)" % pk)
        for i in range(0, len(bb), 40):
            g, env, pos = state["brgain"][i]
            pkb = max(abs(x) for x in bb[i])
            print("   block %3d: block peak %10d  gain V+0x10 %10d  env phase %d level %11d  peak/gain %.4f" % (i, pkb, g, env[0], env[1], pkb / g if g else 0))
    if os.environ.get("DIGICMI_LPDUMP"):
        import json as _json
        _json.dump([[r[0], r[1], r[2], r[3], r[4], r[5], list(r[6]), r[7], r[8], r[9], r[10], r[11]] for r in lp], open(os.environ["DIGICMI_LPDUMP"], "w"))
    starts = [i for i, r in enumerate(lp) if r[5]]
    ph = None
    print(" voice 0 amp phase changes (step, phase, level, PLAY start):")
    for r in lp:
        if r[6][0] != ph or r[5]:
            print("   step %5d phase %d level %11d%s" % (r[0], r[6][0], r[6][1], "  note start" if r[5] else ""))
            ph = r[6][0]
    for i0 in starts[:3]:
        print(" note start at block %d:" % i0)
        prev = None
        for r in lp[i0: i0 + 400]:
            pos = r[4]
            jump = prev is not None and abs(pos - prev) > 200
            if r is lp[i0] or jump or (lp.index(r) - i0) % 50 == 0:
                print("  step %5d d0 %7d d1 %7d PLAY %d pos %7d start %d env %s len %d%s"
                      % (r[0], r[1] if r[1] < 2**31 else r[1] - 2**32, r[2] if r[2] < 2**31 else r[2] - 2**32,
                         r[3], pos, r[5], r[6], r[7], "  <- jump" if jump else ""))
            prev = pos

if os.environ.get("DIGICMI_WORDS"):
    print("digipoly_levels in MAP: %s; calls %d" % ("digipoly_levels" in MAP, state.get("wcalls", 0)))
    for n, (w, ws) in sorted(state.get("words", {}).items()):
        print("words at step %d (0x%x):" % (n, w))
        for i in range(0, 96, 8):
            print("   [%2d] %s" % (i, " ".join("%6d" % x for x in ws[i:i + 8])))

if os.environ.get("DIGICMI_PROBE"):
    pv = state.get("pv", [])
    print("probe: %d voice starts with a velocity write" % len(pv))
    for n, v, vel, pm, pc in pv[:12]:
        words = struct.unpack(">19i", pm)
        print("  step %5d voice %d vel 0x%04x pc 0x%x  msg %s" % (n, v, vel, pc, " ".join("%x" % w for w in words)))

if "digicmi_steal" in MAP:
    st = state.get("steals", [])
    telling = 0                                  # steals where the eligible voices were not all equally loud
    for n, ch, elig, lvl in st:
        cand = [v for v in range(8) if elig[v]]
        ch = ch if ch < 0x80000000 else ch - (1 << 32)
        if len(set(lvl[v] for v in cand)) > 1:
            telling += 1
        if not cand:
            if ch != -1:
                fails.append("steal at step %d: chose voice %d with none eligible" % (n, ch))
        elif ch not in cand:
            fails.append("steal at step %d: chose voice %d, not eligible (eligible %s)" % (n, ch, cand))
        elif lvl[ch] > min(lvl[v] for v in cand):
            fails.append("steal at step %d: chose voice %d at level %d, but %s were quieter (levels %s)"
                         % (n, ch, lvl[ch], [v for v in cand if lvl[v] < lvl[ch]], lvl))
    print("steals: %d, %d of them with eligible voices at different levels" % (len(st), telling))
    if os.environ.get("DIGICMI_STEALS"):
        for n, ch, elig, lvl in st:
            print("  step %5d: chose %d; eligible %s; levels %s" % (n, ch, [v for v in range(8) if elig[v]], lvl))
    for n, ch, elig, lvl in [s for s in st if len(set(s[3][v] for v in range(8) if s[2][v])) > 1][:4]:
        print("  step %5d: chose %d; eligible %s; levels %s" % (n, ch, [v for v in range(8) if elig[v]], lvl))
    if not st:
        fails.append("no steal ran, so the quietest-voice rule was not exercised")

if state.get("cpu_seen"):
    print("cpu meter values seen:", sorted(state["cpu_seen"])[:12])
if state.get("prof_cur") and state["prof_cur"] not in state.get("prof", []):
    pc_ = state["prof_cur"]; print("profile unfinished:", pc_["label"], "blocks", pc_["blocks"], "inside", pc_["inside"], "by", pc_["by"])
for pr in state.get("prof", []):
    tot = sum(pr["by"].values())
    print("render profile, %s: %d blocks, %.0f instructions a block" % (pr["label"], pr["blocks"], tot / max(pr["blocks"], 1)))
    for o_, n_ in sorted(pr["by"].items(), key=lambda x: -x[1]):
        print("   %-14s %5.1f%%  %7.0f a block" % (o_, 100.0 * n_ / tot, n_ / max(pr["blocks"], 1)))
    for f_, n_ in sorted(pr["fn"].items(), key=lambda x: -x[1])[:6]:
        print("      digicmi %-22s %6.0f a block" % (f_, n_ / max(pr["blocks"], 1)))
if state.get("frames"):
    for k, (n_, steps_) in state["frames"].items():
        print("frames %s: %d in %d steps" % (k, n_, steps_))
if state.get("fpos"):
    for k, xs in state["fpos"].items():
        xs = [x / 65536 for x in xs]
        jumps = [(round(xs[i - 1], 2), round(xs[i], 2)) for i in range(1, len(xs)) if abs(xs[i] - xs[i - 1]) > 3]
        turns = [round(xs[i], 2) for i in range(1, len(xs) - 1) if (xs[i] - xs[i - 1]) * (xs[i + 1] - xs[i]) < 0]
        print("waves %s: range %.2f-%.2f, jumps %s, turns %s" % (k, min(xs), max(xs), jumps[:6], turns[:8]))
if state.get("lr"):
    for wa_, pcs_ in state.get("rw", {}).items():
        print("reads of 0x%x: %s" % (wa_, " ".join("0x%x/%d" % x for x in sorted(pcs_))))
    for k, (l_, r_, n_, *tb_) in state["lr"].items():
        print("  blocks %s: %d" % (k, sum(tb_)))
        side = "LEFT" if l_ > 2 * r_ else "RIGHT" if r_ > 2 * l_ else "centre"
        sm_ = state.get("lrs", {}).get(k, [])[4000:12000]
        if len(sm_) > 2000 and any(sm_):
            import statistics
            mu = statistics.mean(sm_); x_ = [a - mu for a in sm_]
            best = max(range(60, 800), key=lambda L: sum(x_[i] * x_[i + L] for i in range(0, 4000, 2)) / L ** 0.1)
            print("  pitch %s: period %d samples = %.1f Hz" % (k, best, 48000 / best))
        print("output %s: left %.0f right %.0f (frames %d) -> %s" % (k, l_ / max(n_, 1), r_ / max(n_, 1), n_, side))
if PAGES_ONLY and state.get("mlog"):
    want = {"track 2 -> WERP, track 1 CMI": [40, 0], "track 2 -> CMI, track 1 CMI": [40, 0],
            "track 1 -> ONESHOT": [0, 0], "track 2 -> WERP, track 1 ONESHOT": [0, 1],
            "track 2 -> CMI, track 1 ONESHOT": None, "track 1 -> CMI again": None}
    # the menu sets the machine as its cursor moves; a refused CMI stops the cursor short (at SLICE)
    bad = 0
    for label, m3 in state["mlog"]:
        w_ = want.get(label, m3[:2])
        ok = (m3[:2] == w_) if w_ is not None else (m3[1] != 40 and (m3[0] == 40) == ("CMI again" in label))
        bad += not ok
        print("machines %-34s tracks 1-3 %s  %s" % (label, m3, "ok" if ok else "WRONG, wanted %s" % want.get(label)))
    print("machine rules: %s" % ("PASS" if not bad else "FAIL (%d)" % bad))
if PAGES_ONLY:
    print("set_param calls for slots 46+ (step, value, voice, slot): %s" % [(n, hex(v & 0xffff), w, sl) for n, v, w, sl in state.get("setp", [])][:16])
    print("pages only: screenshots in %s; no playback or sound checks" % a.png)
    sys.exit(0)
print("PASS" if not fails else "FAIL:\n  " + "\n  ".join(fails))
sys.exit(1 if fails else 0)
