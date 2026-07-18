#!/usr/bin/env python3
# CHALICE C4-4 (second slice) -- ISOLATED probe for the opt-in `pe9 call`
# verb (attempts the real BTCpuProcessInit/ThreadInit native call). NEVER
# folded into pe9_gate.py -- ntboot.c's receipt at the call site: this
# kernel's sync_handler has no recovery from a raw CPU exception (dumps
# then `for(;;) wfe`), so a fault here ends the QEMU session outright.
# This script just observes whatever the transcript shows in a bounded
# window, then always terminates the QEMU process itself (never naked,
# never left running) regardless of outcome.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-pe9call-probe.sock"

if os.path.exists(SOCK):
    os.remove(SOCK)

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"

proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

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
    print("PROBE FAIL: never connected to serial socket")
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
    try:
        s.sendall((line + "\r").encode())
    except OSError:
        pass

drain(6)
send("pe9 call")
drain(8)

try:
    s.close()
except OSError:
    pass
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

txt = transcript.decode(errors="replace")
out_path = "/tmp/gaia-pe9call-probe.log"
with open(out_path, "w") as f:
    f.write(txt)
print(f"transcript saved: {out_path} ({len(txt)} bytes)")
print("---- full transcript from 'pe9 call' onward ----")
idx = txt.rfind("pe9 call")
print(txt[idx:] if idx >= 0 else txt[-4000:])
