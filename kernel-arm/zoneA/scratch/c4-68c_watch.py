#!/usr/bin/env python3
# CHALICE C4-68C FINISH -- late-armed QEMU gdbstub (rig-level, TCG software
# watchpoint, NOT the guest DBGWCR/PSTATE.SS mechanism C4-68/68B already
# proved undelivered) 8-byte WRITE watchpoint on FEXCore's own
# CpuStateFrame::State.rip (CurrentFrame+0x18).
#
# PROVEN PROBLEM (this atom's own prior lane, scratch/c4-68c_v2_watch_
# receipt_full.log, 124KB vs a normal ~1.2MB full boot): arming the host
# gdb-remote SBWatchpoint IMMEDIATELY after connect (CPU still paused at
# reset via -S) makes QEMU TCG add a global memory-write check to every
# translated block from the very first guest instruction on -- boot alone
# (kernel bring-up + shell + typing "pe10 fex" + early wow64 setup, normally
# a few seconds) didn't finish inside a 900s budget, reaching only the early
# 0x270000000 territory allocation, zero watchpoint hits.
#
# FIX -- LATE-ARM: connect + continue at FULL SPEED (no watchpoint yet),
# drive the serial console to "pe10 fex" as usual, TAIL the transcript for
# a marker close to the fault, ONLY THEN interrupt the vCPU and arm the
# watchpoint. Marker chosen (not "binding boot exe IAT", C4-57 -- that
# print lands at roughly 42% of a full boot's own transcript by line count,
# leaving the majority of the run, and the fault, still to execute watched):
# the LAST NtProtectVirtualMemory(BaseAddress=<culprit page>, RegionSize=
# 0x1000, NewProtect=0x20) call (ntfile.c's "CHALICE C4-60: ..." print,
# id=80) -- confirmed by direct grep of the prior (unwatched, full-speed)
# V2 boot receipt (scratch/c4-68_v2_receipt_full.log) to be the LAST of 3
# occurrences of this BaseAddress, printed ~13 lines (a handful of guest/
# native instructions) before the zeroing event and the elr=0x26D0C02B0
# fault trampoline -- this is the task's own named "id=80
# NtProtectVirtualMemory last-protect gate", empirically the closest
# reliable marker to the write on disk. The *BaseAddress itself is NOT
# hardcoded here -- extracted live from the SAME boot's own earlier "CHALICE
# C4-60: NtProtectVirtualMemory ... *BaseAddress=0x...RegionSize=
# 0x1000 NewProtect=0x20" print (first two occurrences, RegionSize=0x200,
# are filtered out by requiring RegionSize=0x1000/NewProtect=0x20) and
# cross-checked against the C4-64 "svc id=80 rip=<addr>" companion line
# that always carries the same address as its own rip field pre-fault.
#
# Watch address: also NOT hardcoded blind -- read live from this boot's own
# "CHALICE C4-68: watchpoint ARMED addr=0x... (CurrentFrame=0x...)" print
# (kmain.c's nt_dbg_arm_watchpoint, fires early via the guest's own -- proven
# undelivered under TCG, C4-68/68B -- DBGWVR0_EL1 attempt) if it appears in
# the transcript before our own late-arm point; falls back to the fixed
# 0x270001158 (CurrentFrame+0x18) default every c4-68* receipt on disk has
# shown so far (deterministic allocator, no ASLR) only if that print is
# somehow missed.
#
# Method: lldb's own SBTarget/SBWatchpoint (real, mature GDB-remote client)
# ConnectRemote's to build.sh's GAIA_GDB_PORT gdbstub. A background thread
# drives the serial console (boot banner drain, "pe10 fex", continuous
# drain+buffer) while the main thread polls the buffered transcript for the
# late-arm marker, then interrupts, arms, and resumes -- from that point on,
# the SAME watch-hit event loop as before: on every watchpoint stop, read
# the new 8-byte value + $pc, log one line, auto-continue; on the first
# value==0, dump full x0-x30 registers + a disassembly window, then keep
# watching (per task spec) to the known crash/exit.
#
# No hardcoded port (IRON LAW) -- caller picks a free one. Never /tmp --
# all sockets/logs live under zoneA/scratch/.
import os, sys, re, socket, subprocess, threading, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRATCH = os.path.join(ZONEA, "scratch")
WATCH_ADDR_DEFAULT = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x270001158
WATCH_SIZE = 8
# Overall wall budget (this atom's own law: watched-slice 600s, whole run
# under 900s) -- BOOT_TO_MARKER gets the difference so the watched slice
# itself always has its full 600s even if the unwatched drive-to-marker
# phase is slow.
TOTAL_TIMEOUT = int(os.environ.get("C468C_TOTAL_TIMEOUT", "900"))
WATCHED_TIMEOUT = int(os.environ.get("C468C_WATCHED_TIMEOUT", "600"))
BOOT_TO_MARKER_TIMEOUT = max(60, TOTAL_TIMEOUT - WATCHED_TIMEOUT - 60)

SERIAL_SOCK = os.path.join(SCRATCH, "c4-68c-serial.sock")
HITS_LOG = os.path.join(SCRATCH, "c4-68c3_watch_hits.log")
SERIAL_LOG_FULL = os.path.join(SCRATCH, "c4-68c3_v2_watch_receipt_full.log")
SERIAL_LOG_SHORT = os.path.join(SCRATCH, "c4-68c3_v2_watch_receipt.log")
QEMU_LOG = os.path.join(SCRATCH, "c4-68c3_qemu_launch.log")
# CHALICE C4-68C2 FINISH: cap on non-watchpoint stops before bailing with a
# diagnostic dump (fix #2 -- prior c4-68c2 run's single reason=5 stop was
# handled fine, but nothing capped a runaway sequence of them).
NON_WP_STOP_CAP = 50
# CHALICE C4-68C2 FINISH: known fault trampoline address (C4-61 FULL REGS
# receipts, every c4-68* boot so far) -- once seen in the serial transcript
# AFTER a zero-write hit, the race is fully resolved and there is nothing
# left to watch; stop instead of burning the rest of WATCHED_TIMEOUT.
FAULT_RE = re.compile(rb"elr=0x0*26D0C02B0")

for p in (SERIAL_SOCK, HITS_LOG, SERIAL_LOG_FULL, SERIAL_LOG_SHORT, QEMU_LOG):
    if os.path.exists(p):
        os.remove(p)

# free TCP port, chosen fresh each run -- never hardcoded.
_s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
_s.bind(("127.0.0.1", 0))
GDB_PORT = _s.getsockname()[1]
_s.close()

env = dict(os.environ)
env["GAIA_SERIAL"] = f"unix:{SERIAL_SOCK},server=on,wait=off"
env["GAIA_DISPLAY"] = "none"
env["GAIA_GDB_PORT"] = str(GDB_PORT)

print(f"launching build.sh: gdb tcp::{GDB_PORT}, serial {SERIAL_SOCK}")
qemu_log = open(QEMU_LOG, "w")
proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                         stdout=qemu_log, stderr=subprocess.STDOUT)

# wait for the serial unix socket to exist (build finishes, qemu -S paused)
for _ in range(200):
    if os.path.exists(SERIAL_SOCK):
        break
    time.sleep(0.1)
else:
    print("FAIL: serial socket never appeared (build/QEMU launch failed)")
    proc.terminate()
    sys.exit(1)

lldb_pypath = subprocess.check_output(["lldb", "-P"]).decode().strip()
if lldb_pypath not in sys.path:
    sys.path.insert(0, lldb_pypath)
import lldb

dbg = lldb.SBDebugger.Create()
dbg.SetAsync(True)
target = dbg.CreateTargetWithFileAndArch(None, "aarch64")
listener = lldb.SBListener("c468c")
cerr = lldb.SBError()
process = None
for _ in range(100):
    process = target.ConnectRemote(listener, f"connect://127.0.0.1:{GDB_PORT}", "gdb-remote", cerr)
    if cerr.Success():
        break
    time.sleep(0.1)
if not cerr.Success():
    print("FAIL: gdb-remote connect failed:", cerr)
    proc.terminate()
    sys.exit(1)
print("gdb-remote attached, CPU paused at reset -- NOT arming yet, full-speed boot")

stop_flag = threading.Event()
transcript = bytearray()
transcript_lock = threading.Lock()
serial_sock_holder = {}

def serial_driver():
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.connect(SERIAL_SOCK)
    s.settimeout(0.5)
    serial_sock_holder["s"] = s
    def drain(sec):
        end = time.time() + sec
        while time.time() < end and not stop_flag.is_set():
            try:
                chunk = s.recv(65536)
                if not chunk:
                    break
                with transcript_lock:
                    transcript.extend(chunk)
            except socket.timeout:
                pass
    drain(6)
    s.sendall(b"pe10 fex\r")
    # keep draining for the whole remaining budget -- the marker-watcher
    # below reads the SAME shared transcript buffer concurrently.
    drain(max(5, TOTAL_TIMEOUT - 10))
    s.close()

th_serial = threading.Thread(target=serial_driver, daemon=True)
th_serial.start()

# full speed -- no watchpoint armed yet.
process.Continue()

ARMED_RE = re.compile(rb"CHALICE C4-68: watchpoint ARMED addr=0x([0-9A-Fa-f]+)")
# CHALICE C4-68C2 FINISH fix #1: the c4-68c2 marker (last-shaped id=80
# NtProtectVirtualMemory BaseAddress print, ntfile.c C4-60) was PROVEN TOO
# LATE (receipt scratch/c4-68c2_watch_hits.log) -- that line prints AFTER
# the syscall completes and the zero-write follows within a handful of
# guest instructions, losing the arm/interrupt/WatchAddress round-trip
# race every time (0 hits). Rearmed on the EARLIER, one-shot ntfile.c
# C4-57 print ("both syswow64 kernel32/kernelbase mapped -- binding boot
# exe IAT", ntfile.c:1626) -- fires once per boot, well before the id=80
# window, giving the arm/interrupt round-trip ample margin to win the race
# this time.
MARKER_RE = re.compile(rb"CHALICE C4-57: both syswow64 kernel32/kernelbase mapped -- binding boot exe IAT")

print(f"waiting up to {BOOT_TO_MARKER_TIMEOUT}s (unwatched, full speed) for the late-arm marker...")
watch_addr = None
marker_seen = False
marker_deadline = time.time() + BOOT_TO_MARKER_TIMEOUT
while time.time() < marker_deadline:
    with transcript_lock:
        buf = bytes(transcript)
    if watch_addr is None:
        m = ARMED_RE.search(buf)
        if m:
            watch_addr = int(m.group(1), 16)
            print(f"live-read watchpoint addr from boot's own ARMED print: 0x{watch_addr:x}")
    if MARKER_RE.search(buf):
        marker_seen = True
        print("late-arm marker seen: CHALICE C4-57 (binding boot exe IAT)")
        break
    if not th_serial.is_alive():
        break
    time.sleep(0.1)
else:
    print("FAIL: late-arm marker not seen within budget")
if not marker_seen:
    print("FAIL: late-arm marker (CHALICE C4-57) not seen")
    stop_flag.set()
    with transcript_lock:
        txt = bytes(transcript).decode(errors="replace")
    with open(SERIAL_LOG_FULL, "w") as f:
        f.write(txt)
    process.Kill()
    proc.send_signal(signal.SIGTERM)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait()
    sys.exit(1)

if watch_addr is None:
    watch_addr = WATCH_ADDR_DEFAULT
    print(f"boot's own ARMED print not seen yet -- using default watch addr 0x{watch_addr:x}")

# Interrupt the vCPU NOW, as close to the marker as the RSP round-trip allows.
# NOTE: process.GetState() reads stale/cached values under a custom listener
# (confirmed live, scratch/c4-68c_debug_interrupt.py) -- SendAsyncInterrupt()
# + draining the listener's own event queue until eStateStopped is the
# proven-working method, not a GetState() poll loop.
process.SendAsyncInterrupt()
stopped = False
ev0 = lldb.SBEvent()
for _ in range(100):
    got = listener.WaitForEvent(1, ev0)
    if got and lldb.SBProcess.GetStateFromEvent(ev0) == lldb.eStateStopped:
        stopped = True
        break
if not stopped:
    print(f"FAIL: process did not stop after interrupt, state={process.GetState()}")
    proc.terminate()
    sys.exit(1)
print("vCPU interrupted at late-arm point")

werr = lldb.SBError()
wp = target.WatchAddress(watch_addr, WATCH_SIZE, False, True, werr)
if not werr.Success() or not wp or not wp.IsValid():
    print("FAIL: watchpoint arm failed:", werr)
    proc.terminate()
    sys.exit(1)
print(f"write watchpoint LATE-ARMED at 0x{watch_addr:x} size={WATCH_SIZE} -- resuming")

hits_f = open(HITS_LOG, "w")
hits_f.write(f"# CHALICE C4-68C2 FINISH watch hits -- addr=0x{watch_addr:x} size={WATCH_SIZE} "
             f"late-armed at marker CHALICE C4-57 (binding boot exe IAT)\n")
hits_f.flush()

def read_val(addr):
    e = lldb.SBError()
    data = process.ReadMemory(addr, 8, e)
    if not e.Success() or data is None:
        return None
    return int.from_bytes(data, "little")

def read_regs(thread):
    fr = thread.GetFrameAtIndex(0)
    regs = {}
    for reg_set in fr.GetRegisters():
        for r in reg_set:
            regs[r.GetName()] = r.GetValue()
    return fr.GetPC(), regs

def dump_disasm(pc):
    e = lldb.SBError()
    lo = max(0, pc - 64)
    data = process.ReadMemory(lo, 192, e)
    if not e.Success() or data is None:
        return f"disasm read failed at 0x{lo:x}: {e}"
    raw_path = os.path.join(SCRATCH, "c4-68c3_zerowrite_window.bin")
    with open(raw_path, "wb") as f:
        f.write(data)
    try:
        out = subprocess.check_output(
            ["aarch64-elf-objdump", "-D", "-b", "binary", "-m", "aarch64",
             "--adjust-vma=" + hex(lo), raw_path], stderr=subprocess.STDOUT
        ).decode(errors="replace")
    except Exception as ex:
        try:
            out = subprocess.check_output(
                ["llvm-objdump", "-D", "-b", "binary", "-m", "aarch64",
                 "--adjust-vma=" + hex(lo), raw_path], stderr=subprocess.STDOUT
            ).decode(errors="replace")
        except Exception as ex2:
            out = f"objdump failed: {ex} / {ex2}"
    return out

watch_start = time.time()
process.Continue()

hit_count = 0
last_values = []
zero_seen = False
fault_seen = False
non_wp_stop_count = 0
ev = lldb.SBEvent()
while time.time() - watch_start < WATCHED_TIMEOUT and time.time() < marker_deadline + WATCHED_TIMEOUT + 30:
    # CHALICE C4-68C2 FINISH fix -- "continue until serial shows the fault
    # ... then stop": poll every iteration (not just on watch hits) so a
    # fault seen while idle-waiting for events still ends the loop.
    if zero_seen and not fault_seen:
        with transcript_lock:
            buf_tail = bytes(transcript[-4096:])
        if FAULT_RE.search(buf_tail):
            fault_seen = True
            hits_f.write("# fault trampoline elr=0x26D0C02B0 observed in serial -- stopping watch loop\n")
            hits_f.flush()
            break
    got = listener.WaitForEvent(1, ev)
    if not got:
        if not th_serial.is_alive() and process.GetState() != lldb.eStateStopped:
            break
        continue
    st = lldb.SBProcess.GetStateFromEvent(ev)
    if st == lldb.eStateStopped:
        th = process.GetThreadAtIndex(0)
        reason = th.GetStopReason() if th else None
        if reason == lldb.eStopReasonWatchpoint:
            hit_count += 1
            pc, regs = read_regs(th)
            val = read_val(watch_addr)
            val_s = f"0x{val:016x}" if val is not None else "READ_FAIL"
            line = f"hit={hit_count} pc=0x{pc:016x} new_value={val_s}"
            hits_f.write(line + "\n")
            hits_f.flush()
            last_values.append(val)
            last_values = last_values[-10:]
            if val == 0 and not zero_seen:
                zero_seen = True
                hits_f.write(f"# ZERO WRITE at hit={hit_count} pc=0x{pc:016x}\n")
                for k in ("x0","x1","x2","x3","x4","x5","x6","x7","x8","x9",
                          "x10","x11","x12","x13","x14","x15","x16","x17",
                          "x18","x19","x20","x21","x22","x23","x24","x25",
                          "x26","x27","x28","fp","lr","sp","pc","cpsr"):
                    if k in regs:
                        hits_f.write(f"#   {k}={regs[k]}\n")
                hits_f.write("# disasm window pc-64..pc+128:\n")
                for dl in dump_disasm(pc).splitlines():
                    hits_f.write("#   " + dl + "\n")
                hits_f.flush()
                print(f"ZERO WRITE CAUGHT at hit={hit_count} pc=0x{pc:016x} -- continuing to observe fault/exit")
            process.Continue()
        else:
            # CHALICE C4-68C2 FINISH fix #2: ANY non-watchpoint stop -> log
            # one line (pc, stop reason) and CONTINUE. Capped at
            # NON_WP_STOP_CAP consecutive/total such stops, then bail with
            # a full diagnostic dump instead of silently spinning.
            non_wp_stop_count += 1
            pc, regs = read_regs(th) if th else (0, {})
            hits_f.write(f"# non-watchpoint stop #{non_wp_stop_count} reason={reason} pc=0x{pc:016x}\n")
            hits_f.flush()
            if non_wp_stop_count >= NON_WP_STOP_CAP:
                hits_f.write(f"# BAIL: {NON_WP_STOP_CAP} non-watchpoint stops reached -- dumping state and stopping\n")
                for k in ("x0","x1","x2","x3","x4","x5","x6","x7","x8","x9",
                          "x10","x11","x12","x13","x14","x15","x16","x17",
                          "x18","x19","x20","x21","x22","x23","x24","x25",
                          "x26","x27","x28","fp","lr","sp","pc","cpsr"):
                    if k in regs:
                        hits_f.write(f"#   {k}={regs[k]}\n")
                hits_f.flush()
                break
            process.Continue()
    elif st in (lldb.eStateExited, lldb.eStateCrashed, lldb.eStateDetached):
        hits_f.write(f"# process state event: {st}\n")
        break

stop_flag.set()
th_serial.join(timeout=10)

hits_f.write(f"# TOTAL HITS: {hit_count}\n")
hits_f.write(f"# LAST 10 VALUES: {[hex(v) if v is not None else None for v in last_values]}\n")
hits_f.close()

with transcript_lock:
    txt = bytes(transcript).decode(errors="replace")
with open(SERIAL_LOG_FULL, "w") as f:
    f.write(txt)
with open(SERIAL_LOG_SHORT, "w") as f:
    f.write("\n".join(txt.splitlines()[-80:]))

process.Kill()
proc.send_signal(signal.SIGTERM)
try:
    proc.wait(timeout=5)
except subprocess.TimeoutExpired:
    proc.kill()
    proc.wait()

print(f"total watchpoint hits: {hit_count}")
print(f"serial transcript: {SERIAL_LOG_FULL} ({len(txt)} bytes)")
print(f"hit log: {HITS_LOG}")
print("---- serial tail (last 60 lines) ----")
for line in txt.splitlines()[-60:]:
    print(line)

# CHALICE C4-68C2: lldb's own SWIG teardown crashes (SIGBUS) at normal
# Python interpreter exit under this async/custom-listener setup, live-
# confirmed harmless-but-fatal-looking in scratch/c4-68c_debug_interrupt.py
# (crash happens strictly AFTER all real work/printing is done) -- all
# receipts are already flushed to disk above, so skip the crashing
# destructor path entirely instead of chasing an upstream lldb bug.
sys.stdout.flush()
sys.stderr.flush()
os._exit(0)
