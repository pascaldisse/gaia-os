#!/usr/bin/env python3
# CHALICE C2a Atom A gate: boot QEMU headless, drive shell verbs over serial
# unix socket, assert Gate B lines. Modeled on scratch/ramfb_gate.py harness.
import os, socket, subprocess, sys, time

REPO  = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ZONEA = os.path.join(REPO, "kernel-arm", "zoneA")
SCR   = os.path.join(REPO, "scratch")
SER   = os.path.join(SCR, "chalice_serial.sock")

try: os.remove(SER)
except FileNotFoundError: pass

env = dict(os.environ, GAIA_SERIAL=f"unix:{SER},server,nowait", GAIA_DISPLAY="none")
qemu = subprocess.Popen(["bash", "build.sh"], cwd=ZONEA, env=env,
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
    print("GATE FAIL:", msg)
    qemu.terminate()
    sys.exit(1)

full = b""
try:
    ser = connect(SER)
    full += drain(ser, 5.0)
    if b"T:/>" not in full: fail("shell prompt never appeared")

    for cmd in (b"zc\n", b"say\n", b"aot\n", b"pe\n", b"pe4\n", b"pe5\n"):
        ser.send(cmd)
        full += drain(ser, 15.0)
finally:
    qemu.terminate()
    try: qemu.wait(timeout=5)
    except Exception: qemu.kill()

text = full.decode("latin1")
sys.stdout.write("---- FULL SERIAL LOG ----\n")
sys.stdout.write(text)

need = [
    "GaiaMod=100",
    "love=1",
    "pe1 ret=42",
    "CHALICE: the cup fills",
    "pe2 ret=7",
    "CHALICE: the cup speaks",
    "CHALICE: continue lands",
    "CHALICE: NT10",
    "pe3 ret=99",
    "CHALICE: the cup links",
    "pe4 ret=60",
    "CHALICE: thunk enters",
    "CHALICE: the chain speaks",
    "pe5 ret=99",
]
ok = True
for n in need:
    hit = n in text
    ok = ok and hit
    print(f"  {'OK ' if hit else 'MISSING'} {n!r}")
if not ok: fail("one or more Gate B lines missing")
print("GATE PASS: chalice slice-2 Atom A console gate green.")
