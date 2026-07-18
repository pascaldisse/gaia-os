#!/usr/bin/env python3
# CHALICE C4-5 -- isolated probe for the opt-in `pe10` verb (nt_boot_process
# with hello8_x86.exe as main image, native stock ntdll entered as usual --
# a MEASURE of how far stock ntdll gets against a non-native-machine main
# image, per HANDOFF measure-don't-guess doctrine). Isolated harness, never
# the canonical pe9_gate.py path -- this can legitimately crash/hang the
# kernel (sync_handler has no recovery for a raw CPU exception loop), so
# this script SIGTERMs QEMU itself regardless of outcome.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-pe10-probe.sock"

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
    s.sendall((line + "\r").encode())

drain(6)
send("pe10")
drain(10)

s.close()
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

txt = transcript.decode(errors="replace")
out_path = "/tmp/gaia-pe10-probe.log"
with open(out_path, "w") as f:
    f.write(txt)
print(f"transcript saved: {out_path} ({len(txt)} bytes)")
print("---- tail (last 60 lines) ----")
for line in txt.splitlines()[-60:]:
    print(line)
