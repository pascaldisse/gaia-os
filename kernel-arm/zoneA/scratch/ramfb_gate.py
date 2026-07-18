#!/usr/bin/env python3
# CHALICE C3c-2n -- minimal ramfb regression check (recreated, scratch/ is
# session-local per HANDOFF convention). Boots gaia-os.img with the serial
# console on a unix socket (drives 'fb' to redraw the boot test pattern)
# and the QEMU monitor on a second unix socket (screendump -> PPM), then
# checks the 4 corners are white and the 3 color bars are present -- same
# pattern FbDrawPattern (fw_cfg.c) draws.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SERIAL_SOCK = "/tmp/gaia-ramfb-serial.sock"
MON_SOCK = "/tmp/gaia-ramfb-mon.sock"
PPM = "/tmp/gaia-ramfb.ppm"

for p in (SERIAL_SOCK, MON_SOCK, PPM):
    if os.path.exists(p):
        os.remove(p)

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SERIAL_SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"

proc = subprocess.Popen(
    ["./build.sh", "-monitor", f"unix:{MON_SOCK},server=on,wait=off"],
    cwd=ZONEA, env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

def connect(path, tries=200):
    for _ in range(tries):
        if os.path.exists(path):
            try:
                s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                s.connect(path)
                return s
            except OSError:
                pass
        time.sleep(0.1)
    return None

s = connect(SERIAL_SOCK)
if not s:
    print("GATE FAIL: no serial socket")
    proc.terminate(); sys.exit(1)
s.settimeout(0.5)

def drain(sock, seconds):
    end = time.time() + seconds
    buf = b""
    while time.time() < end:
        try:
            c = sock.recv(65536)
            if not c: break
            buf += c
        except socket.timeout:
            pass
    return buf

drain(s, 6)
s.sendall(b"fb\r")
drain(s, 2)

m = connect(MON_SOCK)
if not m:
    print("GATE FAIL: no monitor socket")
    proc.terminate(); sys.exit(1)
m.settimeout(1.0)
drain(m, 1)   # banner
m.sendall(f"screendump {PPM}\n".encode())
drain(m, 2)

s.close(); m.close()
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill(); proc.wait()

if not os.path.exists(PPM):
    print("GATE FAIL: no screendump produced")
    sys.exit(1)

with open(PPM, "rb") as f:
    data = f.read()

# PPM P6 header: "P6\n<w> <h>\n255\n" then raw RGB bytes
assert data[:2] == b"P6"
i = 2
def next_tok(buf, i):
    while buf[i:i+1].isspace(): i += 1
    j = i
    while not buf[j:j+1].isspace(): j += 1
    return buf[i:j], j
w_tok, i = next_tok(data, i); w = int(w_tok)
h_tok, i = next_tok(data, i); h = int(h_tok)
maxv_tok, i = next_tok(data, i); i += 1  # skip single whitespace after maxval
pixels = data[i:]

def px(x, y):
    off = (y * w + x) * 3
    return pixels[off], pixels[off+1], pixels[off+2]

def near(c, target, tol=10):
    return all(abs(a-b) <= tol for a, b in zip(c, target))

checks = [
    ("top-left corner white",  px(0, 0),           (255,255,255)),
    ("top-right corner white", px(w-1, 0),         (255,255,255)),
    ("bot-left corner white",  px(0, h-1),         (255,255,255)),
    ("bot-right corner white", px(w-1, h-1),       (255,255,255)),
    ("red bar",   px(400, 120), (255,0,0)),
    ("green bar", px(400, 220), (0,255,0)),
    ("blue bar",  px(400, 320), (0,0,255)),
]

print(f"screendump: {w}x{h}, {len(pixels)} pixel bytes")
fail = 0
for label, got, want in checks:
    ok = near(got, want)
    print(("OK  " if ok else "MISS") + f" {label}: got={got} want~={want}")
    if not ok: fail += 1

if fail:
    print(f"GATE FAIL: {fail} pattern checks failed")
    sys.exit(1)
print("GATE PASS (ramfb pattern correct)")
