#!/usr/bin/env python3
# GAIA-OS plot gate: boot QEMU headless, draw the boot pattern (fb), then run
# the Shrine/zoneC HolyC GaiaPlot module (plot) which draws a magenta rect +
# two yellow diagonals DIRECTLY on the ramfb surface via imported FbBase/FbWidth.
# Assert: magenta rect interior, yellow on both diagonals (computed coords),
# boot pattern intact OUTSIDE the rect (corners white, gradient present),
# and the module banner on serial. This is the first HolyC-drawn-pixels proof.
import os, socket, subprocess, sys, time

REPO   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZONEA  = os.path.join(REPO, "kernel-arm", "zoneA")
SCR    = os.path.join(REPO, "scratch")
SER    = os.path.join(SCR, "serial.sock")
MON    = os.path.join(SCR, "mon.sock")
PPM    = os.path.join(SCR, "plot.ppm")
W, H   = 640, 480

for p in (SER, MON, PPM):
    try: os.remove(p)
    except FileNotFoundError: pass

env = dict(os.environ, GAIA_SERIAL=f"unix:{SER},server,nowait", GAIA_DISPLAY="none")
qemu = subprocess.Popen(
    ["bash", "build.sh", "-monitor", f"unix:{MON},server,nowait"],
    cwd=ZONEA, env=env,
    stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT)

def connect(path, timeout=15):
    end = time.time() + timeout
    while time.time() < end:
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(path); s.setblocking(False); return s
        except OSError:
            time.sleep(0.1)
    raise SystemExit(f"could not connect {path}")

def drain(s, secs):
    buf = b""; end = time.time() + secs
    while time.time() < end:
        try: buf += s.recv(4096)
        except BlockingIOError: time.sleep(0.05)
    return buf

def fail(msg):
    print("GATE FAIL:", msg); qemu.terminate(); sys.exit(1)

try:
    ser = connect(SER); mon = connect(MON)
    boot = drain(ser, 5.0)
    sys.stdout.write("---- SERIAL (boot) ----\n")
    sys.stdout.write(boot.decode("latin1"))
    if b"ramfb: 640x480 @" not in boot:
        fail("no 'ramfb: 640x480 @' banner on serial")
    if b"T:/>" not in boot:
        fail("shell prompt never appeared")

    # draw the boot pattern first (guarantees the region OUTSIDE the rect)
    ser.send(b"fb\n"); fbresp = drain(ser, 2.0)
    if b"fb: redrawn" not in fbresp:
        fail("'fb' verb did not confirm redraw")

    # run the HolyC GaiaPlot module
    ser.send(b"plot\n"); presp = drain(ser, 3.0)
    sys.stdout.write("---- SERIAL (plot) ----\n")
    sys.stdout.write(presp.decode("latin1"))
    if b"GaiaPlot" not in presp:
        fail("no GaiaPlot module banner on serial")
    if b"plot: ok" not in presp:
        fail("'plot' verb did not confirm ok")

    drain(mon, 1.0)
    mon.send(f"screendump {PPM}\n".encode())
    time.sleep(1.5)
    print("---- MONITOR (screendump) ----")
    print(drain(mon, 1.0).decode("latin1").strip())
finally:
    try: mon.send(b"quit\n")
    except Exception: pass
    time.sleep(0.5)
    qemu.terminate()
    try: qemu.wait(timeout=5)
    except Exception: qemu.kill()

# ---- assert the PPM ----
if not os.path.exists(PPM): fail("no PPM produced")
with open(PPM, "rb") as f: data = f.read()
if not data.startswith(b"P6"): fail("PPM not P6")
tok, i = [], 2
while len(tok) < 3:
    while i < len(data) and data[i] in b" \t\r\n": i += 1
    j = i
    while j < len(data) and data[j] not in b" \t\r\n": j += 1
    tok.append(data[i:j]); i = j
i += 1
pw, ph, mx = int(tok[0]), int(tok[1]), int(tok[2])
px = data[i:]
print(f"PPM header: {pw}x{ph} maxval={mx}  pixelbytes={len(px)}")
if (pw, ph) != (W, H): fail(f"PPM dims {pw}x{ph} != {W}x{H}")
if len(px) < pw*ph*3: fail("truncated pixel data")

def at(x, y):
    o = (y*pw + x)*3
    return px[o], px[o+1], px[o+2]

magenta = lambda r,g,b: r>200 and g<60 and b>200
yellow  = lambda r,g,b: r>200 and g>200 and b<60
white   = lambda r,g,b: r>200 and g>200 and b>200

# TL->BR diagonal: col == row (100..379).  TR->BL diagonal: col == 639-row.
checks = [
    # magenta rect interior, off both diagonals
    ("magenta interior (300,200)", at(300,200), magenta),
    ("magenta interior (450,350)", at(450,350), magenta),
    # yellow TL->BR diagonal (col==row)
    ("yellow diag TL (150,150)",   at(150,150), yellow),
    ("yellow diag TL (300,300)",   at(300,300), yellow),
    # yellow TR->BL diagonal (col==639-row)
    ("yellow diag TR (489,150)",   at(489,150), yellow),
    ("yellow diag TR (339,300)",   at(339,300), yellow),
    # boot pattern intact OUTSIDE the rect
    ("corner TL white (0,0)",      at(0,0),     white),
    ("corner TR white (639,0)",    at(639,0),   white),
    ("corner BL white (0,479)",    at(0,479),   white),
    ("corner BR white (639,479)",  at(639,479), white),
    # gradient still present just outside the rect (not overwritten)
    ("boot gradient (600,50)",     at(600,50),  lambda r,g,b: b<60 and r>120 and not magenta(r,g,b)),
]
ok = True
for name, (r,g,b), pred in checks:
    good = pred(r,g,b); ok = ok and good
    print(f"  {'OK ' if good else 'BAD'} {name} = rgb({r},{g},{b})")

# count magenta + yellow pixels to prove a real fill (not a few sample dots)
mag = sum(1 for k in range(0,pw*ph*3,3) if magenta(px[k],px[k+1],px[k+2]))
yel = sum(1 for k in range(0,pw*ph*3,3) if yellow(px[k],px[k+1],px[k+2]))
print(f"magenta pixels: {mag}   yellow pixels: {yel}")
if mag < 100000: fail(f"only {mag} magenta pixels (expected a filled rect ~123k)")
if yel < 400:    fail(f"only {yel} yellow pixels (expected ~2*280 diagonal)")
if not ok: fail("pixel-coordinate assertions failed")
print("GATE PASS: HolyC GaiaPlot drew magenta rect + yellow diagonals on ramfb; boot pattern intact outside.")
