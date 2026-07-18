#!/usr/bin/env python3
# CHALICE hello7/pe8 FINAL VERIFICATION -- exhaustion-dead proof. One boot,
# pe6/pe7/pe8 run TWICE back-to-back (6 nt_boot_process calls total in one
# QEMU session -- the exact "3 stock boots exhaust territory" scenario
# HANDOFF's wholesale-per-boot-reset fix (07e396a/9dc880e) targets), doubled
# to stress it further. Checks: no STATUS_NO_MEMORY (0xC0000017) anywhere,
# and every completion marker for all three verbs present (>=2) each.
import os, socket, subprocess, sys, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOCK = "/tmp/gaia-backtoback-gate.sock"

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

drain(6)
# pe6/pe7/pe8 TWICE back-to-back, one boot/session.
# CHALICE C4-9: waits bumped (6/8/8 -> 8/20/20) -- the RESERVE/COMMIT arena
# is correctly, by-design (monad ruling #2: "round to 2MB windows") heavier
# per first-touch-in-a-window than the old flat bump allocator (each newly-
# committed 2MB window backs+zeroes 2MB once, vs the old scheme's exact-size
# zero of just the touched bytes) -- live-measured this leg: the SECOND
# pe7/pe8 pass in one session needs ~15-20s wall-clock now (was well under
# 8s), confirmed to complete cleanly (no fault, no STATUS_NO_MEMORY) given
# the headroom -- a harness timing-budget fix, not a functional regression.
for cmd, wait in [("pe6", 8), ("pe7", 20), ("pe8", 20),
                  ("pe6", 8), ("pe7", 20), ("pe8", 20)]:
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
out_path = "/tmp/gaia-backtoback-gate.log"
with open(out_path, "w") as f:
    f.write(txt)
print(f"transcript saved: {out_path} ({len(txt)} bytes)")

def count(s_):
    return txt.count(s_)

checks = [
    ("no STATUS_NO_MEMORY (0xC0000017) anywhere", count("0xC0000017") == 0, count("0xC0000017")),
    ("no STATUS_NO_MEMORY text", count("STATUS_NO_MEMORY") == 0, count("STATUS_NO_MEMORY")),
    ("pe6 ret=99  (x2)", count("pe6 ret=99") >= 2, count("pe6 ret=99")),
    ("pe7 ret=77  (x2)", count("pe7 ret=77") >= 2, count("pe7 ret=77")),
    ("pe8 ret=78  (x2)", count("pe8 ret=78") >= 2, count("pe8 ret=78")),
    ("kernel32 speaks (x2)", count("CHALICE: kernel32 speaks") >= 2, count("CHALICE: kernel32 speaks")),
    ("hello7 surface walk complete (x2)", count("CHALICE: hello7 surface walk complete") >= 2,
     count("CHALICE: hello7 surface walk complete")),
    ("GetModuleHandleA(kernel32.dll) non-null (x2)",
     txt.count("CHALICE: GetModuleHandleA(kernel32.dll) -> 0x0000000000000000") == 0 and
     count("CHALICE: GetModuleHandleA(kernel32.dll) -> 0x") >= 2,
     count("CHALICE: GetModuleHandleA(kernel32.dll) -> 0x")),
]

ok = True
print("---- back-to-back checks ----")
for name, cond, n in checks:
    print(("OK  " if cond else "MISS") + f" {name} (count={n})")
    if not cond:
        ok = False

if not ok:
    print("GATE FAIL")
    sys.exit(1)
print("GATE PASS (back-to-back, exhaustion-dead)")
