#!/usr/bin/env python3
# CHALICE C3c-2n -- minimal unix-socket QEMU serial harness (recreated;
# scratch/ is gitignored/session-local, per HANDOFF convention -- this
# worker's clone had none). Boots gaia-os.img with serial routed to a unix
# socket (never QEMU on stdio, per SCARS discipline), drives the shell over
# that socket, greps the transcript for the required-green markers +
# whatever pe7 prints this run, then kills qemu cleanly.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-pe7-gate.sock"

if os.path.exists(SOCK):
    os.remove(SOCK)

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"

proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

# wait for the socket to appear + become connectable
s = None
for _ in range(200):
    if os.path.exists(SOCK):
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(SOCK)
            break
        except OSError:
            s = None
    time.sleep(0.1)

if not s:
    print("GATE FAIL: never connected to serial socket")
    proc.terminate()
    sys.exit(1)

s.settimeout(0.5)
transcript = b""

def drain(seconds):
    global transcript
    end = time.time() + seconds
    while time.time() < end:
        try:
            chunk = s.recv(65536)
            if not chunk:
                break
            transcript += chunk
        except socket.timeout:
            pass

def send(line):
    s.sendall((line + "\r").encode())

# boot
drain(6)
# drive the shell
for cmd, wait in [("pe", 3), ("pe4", 3), ("pe5", 3), ("pe6", 6), ("pe7", 8), ("fb", 3)]:
    send(cmd)
    drain(wait)

s.close()
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

txt = transcript.decode(errors="replace")
out_path = "/tmp/gaia-pe7-gate.log"
with open(out_path, "w") as f:
    f.write(txt)
print(f"transcript saved: {out_path} ({len(txt)} bytes)")

required = [
    "pe1 ret=42",
    "CHALICE: the cup fills",
    "pe2 ret=7",
    "CHALICE: the cup speaks",
    "CHALICE: continue lands",
    "CHALICE: NT10 floor",
    "pe3 ret=99",
    "CHALICE: the cup links",
    "pe4 ret=60",
    "CHALICE: thunk enters",
    "CHALICE: the chain speaks",
    "pe5 ret=99",
    "CHALICE: stock ntdll mapped",
    "CHALICE: stock thunk enters",
]
missing = [r for r in required if r not in txt]
print("---- required-green ----")
for r in required:
    print(("OK  " if r in txt else "MISS") + " " + r)
print("---- pe7-specific lines ----")
for line in txt.splitlines():
    if "pe7" in line or "kernelbase" in line or "NtInitializeNlsFiles" in line or \
       "init_locale" in line or "FAULT" in line or "NtRaiseException" in line:
        print(line)

if missing:
    print(f"GATE FAIL: missing {len(missing)} required lines")
    sys.exit(1)
print("GATE PASS (required-green)")
