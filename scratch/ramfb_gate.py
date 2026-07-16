#!/usr/bin/env python3
# GAIA-OS ramfb headless gate: boot QEMU (-display none), prove the shell
# still answers on serial, screendump the framebuffer, assert the pattern.
import os, socket, subprocess, sys, time

REPO   = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZONEA  = os.path.join(REPO, "kernel-arm", "zoneA")
SCR    = os.path.join(REPO, "scratch")
SER    = os.path.join(SCR, "serial.sock")
MON    = os.path.join(SCR, "mon.sock")
PPM    = os.path.join(SCR, "fb.ppm")
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

    # prove the shell still answers in the SAME boot
    ser.send(b"help\n")
    resp = drain(ser, 3.0)
    sys.stdout.write("---- SERIAL (help) ----\n")
    sys.stdout.write(resp.decode("latin1"))
    if b"love" not in resp or b"fb " not in resp:
        fail("shell 'help' did not answer with expected verbs")

    # redraw via the fb verb (proves surface stays writable post-boot)
    ser.send(b"fb\n")
    fbresp = drain(ser, 2.0)
    if b"fb: redrawn" not in fbresp:
        fail("'fb' verb did not confirm redraw")

    # screendump the framebuffer via the QEMU monitor
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
# parse header: P6 <w> <h> <maxval>\n<binary>
tok, idx, hdr = [], 0, data
i = 2
while len(tok) < 3:
    while i < len(hdr) and hdr[i] in b" \t\r\n": i += 1
    j = i
    while j < len(hdr) and hdr[j] not in b" \t\r\n": j += 1
    tok.append(hdr[i:j]); i = j
i += 1  # single whitespace after maxval
pw, ph, mx = int(tok[0]), int(tok[1]), int(tok[2])
px = data[i:]
print(f"PPM header: {pw}x{ph} maxval={mx}  pixelbytes={len(px)}")
if (pw, ph) != (W, H): fail(f"PPM dims {pw}x{ph} != {W}x{H}")
if len(px) < pw*ph*3: fail("truncated pixel data")

def at(x, y):
    o = (y*pw + x)*3
    return px[o], px[o+1], px[o+2]

nonblack = sum(1 for k in range(0, pw*ph*3, 3)
               if px[k] or px[k+1] or px[k+2])
print(f"non-black pixels: {nonblack}")
if nonblack <= 100000: fail(f"only {nonblack} non-black pixels (need >100000)")

checks = [
    ("red bar   (320,120)", at(320,120), lambda r,g,b: r>200 and g<60 and b<60),
    ("green bar (320,220)", at(320,220), lambda r,g,b: g>200 and r<60 and b<60),
    ("blue bar  (320,320)", at(320,320), lambda r,g,b: b>200 and r<60 and g<60),
    ("corner TL (0,0)",     at(0,0),     lambda r,g,b: r>200 and g>200 and b>200),
    ("corner TR (639,0)",   at(639,0),   lambda r,g,b: r>200 and g>200 and b>200),
    ("corner BL (0,479)",   at(0,479),   lambda r,g,b: r>200 and g>200 and b>200),
    ("corner BR (639,479)", at(639,479), lambda r,g,b: r>200 and g>200 and b>200),
]
ok = True
for name, (r,g,b), pred in checks:
    good = pred(r,g,b)
    ok = ok and good
    print(f"  {'OK ' if good else 'BAD'} {name} = rgb({r},{g},{b})")
if not ok: fail("pixel-coordinate assertions failed")
print("GATE PASS: ramfb pattern correct, shell answered in same boot.")
