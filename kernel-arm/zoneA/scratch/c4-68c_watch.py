#!/usr/bin/env python3
# CHALICE C4-68C6 -- FLOOR-STALL late-arm (final watchpoint atom, this run).
# Same rig-level QEMU gdbstub / lldb SBWatchpoint mechanism as every prior
# C4-68C* sub-run (8-byte WRITE watchpoint on FEXCore's own
# CpuStateFrame::State.rip, CurrentFrame+0x18, default 0x270001158) -- NOT
# the guest DBGWCR/PSTATE.SS mechanism (C4-68/68B, proven undelivered under
# QEMU TCG). This file has been edited IN PLACE across every C4-68C* sub-run
# (c2/c3/c4/c5/c6) -- see git log for the per-run diffs, not separate files.
#
# WHY C6 EXISTS (c5 harvest, commit b52afb8): c4/c5's own COUNT-based
# late-arm (count C4-64 "svc id=" probe lines live, interrupt+arm at
# T-margin) measured a 9-count OVERSHOOT past the reference T=2212 on its
# single detached final run (armed_at_count=2221) -- the async gdb
# interrupt/arm round-trip has enough jitter that count arming at
# single-syscall granularity is a coin flip, not a fix (same class of
# failure c4-68c2's marker-based race already proved, just smaller). 0
# hits, fault reached (class b: watchpoint armed too late, missed the
# write).
#
# THIS ATOM -- deterministic FLOOR STALL, no more racing anything:
#   kernel-arm/zoneA (commit before this driver, floor-stall commit)
#   ntfile.c's NtProtectVirtualMemory now calls kmain.c's c468_maybe_stall()
#   at the EXACT id=80 "last-protect" gate this whole C4-68 thread has
#   chased (BaseAddress==boot exe .text page, NewProtect==0x20 PAGE_EXECUTE
#   /protect-down -- empirically the ONE call matching both conditions in a
#   full boot-to-fault transcript, c4-68c5_v2_watch_receipt_full.log:12323)
#   -- when built with -DC468_STALL (build.sh, GAIA_C468_STALL=1 env), the
#   guest prints "CHALICE C4-68C6 STALL 45s" then busy-waits ~45 REAL
#   wall-clock seconds (CNTPCT_EL0-based) before printing "CHALICE C4-68C6
#   RELEASE" and proceeding. Arm trigger for THIS driver = the STALL marker
#   line itself: the instant it appears on serial, interrupt the vCPU and
#   arm the watchpoint -- the guest is provably still inside (or about to
#   enter) the busy-wait for the next ~45s, so the arm/interrupt round-trip
#   (measured sub-second every prior run) has enormous margin either
#   direction. No count, no fixed-string-position marker, no race.
#
# Hit handling unchanged in spirit from c4-68c2..c5: every watchpoint stop
# logs pc+new_value, auto-continues; the first value==0 hit additionally
# dumps full x0-x30/fp/lr/sp/pc/cpsr registers + a 256-BYTE disassembly
# window around pc (widened from c5's 192B per this atom's own task spec),
# and the loop keeps watching until the fault trampoline
# (elr=0x26D0C02B0) appears on the live serial transcript (early-stop, no
# need to burn the rest of WATCHED_TIMEOUT). The last 10 hit values before
# any zero are also recorded.
#
# LIVE SERIAL TEE (new this run, fixes c5's blindness): the full serial
# transcript is now flushed to the receipt file LINE-BY-LINE as it arrives
# (line-buffered), not just written once at the very end -- `tail -f` the
# receipt file mid-run to watch boot/stall/arm/release/hits live.
#
# No hardcoded watch address blind either -- read live from this boot's own
# "CHALICE C4-68: watchpoint ARMED addr=0x..." print if it appears before
# our own late-arm point; falls back to the fixed 0x270001158 default
# (every c4-68* receipt on disk so far has shown this address, deterministic
# allocator, no ASLR) only if that print is somehow missed.
#
# No hardcoded port (IRON LAW) -- fresh free TCP port each attempt. Never
# /tmp -- all sockets/logs live under zoneA/scratch/. Vendored FEX/ tree
# read-only, not touched by this script (it only drives QEMU + reads serial
# + reads/writes guest memory via the gdb-remote protocol).
#
# Receipts prefix c4-68c6_*. watched_timeout=1800, total_timeout=3600
# (env-overridable, see bottom). Launch DETACHED (nohup/setsid) so it
# survives lane death, per this atom's own task law.
import os, sys, re, socket, subprocess, threading, time, signal

ZONEA = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SCRATCH = os.path.join(ZONEA, "scratch")
WATCH_ADDR_DEFAULT = int(sys.argv[1], 0) if len(sys.argv) > 1 else 0x270001158
WATCH_SIZE = 8

FAULT_RE = re.compile(rb"elr=0x0*26D0C02B0")           # C4-30 fault trampoline
ARMED_RE = re.compile(rb"CHALICE C4-68: watchpoint ARMED addr=0x([0-9A-Fa-f]+)")
STALL_RE = re.compile(rb"CHALICE C4-68C6 STALL")       # this atom's arm trigger
RELEASE_RE = re.compile(rb"CHALICE C4-68C6 RELEASE")   # informational only

NON_WP_STOP_CAP = 50
STALL_SECONDS = int(os.environ.get("GAIA_C468_STALL_SECONDS", "45"))


def read_val(process, addr):
    import lldb
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


def dump_disasm(process, pc, label):
    import lldb
    e = lldb.SBError()
    # CHALICE C4-68C6: 256-byte window (widened from c5's 192B) -- pc-64..
    # pc+192, per this atom's own task spec ("full regs + 256B window").
    lo = max(0, pc - 64)
    WINDOW = 256
    data = process.ReadMemory(lo, WINDOW, e)
    if not e.Success() or data is None:
        return f"disasm read failed at 0x{lo:x}: {e}"
    raw_path = os.path.join(SCRATCH, f"{label}_zerowrite_window.bin")
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


def run_attempt(attempt_no, total_timeout=3600, watched_timeout=1800):
    marker_wait_timeout = max(60, total_timeout - watched_timeout - 60)
    label = "c4-68c6"
    serial_sock = os.path.join(SCRATCH, "c4-68c6-serial.sock")
    hits_log = os.path.join(SCRATCH, f"{label}_watch_hits.log")
    serial_log_full = os.path.join(SCRATCH, f"{label}_v2_watch_receipt_full.log")
    serial_log_short = os.path.join(SCRATCH, f"{label}_v2_watch_receipt.log")
    qemu_log = os.path.join(SCRATCH, f"{label}_qemu_launch.log")
    for p in (serial_sock, hits_log, serial_log_full, serial_log_short, qemu_log):
        if os.path.exists(p):
            os.remove(p)

    print(f"[attempt {attempt_no}] STALL_SECONDS={STALL_SECONDS} "
          f"total_timeout={total_timeout} watched_timeout={watched_timeout} "
          f"marker_wait_timeout={marker_wait_timeout}")

    _s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    _s.bind(("127.0.0.1", 0))
    GDB_PORT = _s.getsockname()[1]
    _s.close()

    env = dict(os.environ)
    env["GAIA_SERIAL"] = f"unix:{serial_sock},server=on,wait=off"
    env["GAIA_DISPLAY"] = "none"
    env["GAIA_GDB_PORT"] = str(GDB_PORT)
    # CHALICE C4-68C6: the whole point of this run -- build with the floor
    # stall compiled IN (build.sh only injects -DC468_STALL when this is
    # set, canonical/gates unaffected otherwise).
    env["GAIA_C468_STALL"] = "1"
    env["GAIA_C468_STALL_SECONDS"] = str(STALL_SECONDS)

    print(f"[attempt {attempt_no}] launching build.sh: GAIA_C468_STALL=1 "
          f"GAIA_C468_STALL_SECONDS={STALL_SECONDS}, gdb tcp::{GDB_PORT}, "
          f"serial {serial_sock}")
    qemu_log_f = open(qemu_log, "w")
    proc = subprocess.Popen(["./build.sh"], cwd=ZONEA, env=env,
                             stdout=qemu_log_f, stderr=subprocess.STDOUT)

    result = dict(hit_count=0, fault_seen=False, ok=False, last_values=[],
                  hits_log=hits_log, serial_log_full=serial_log_full,
                  serial_log_short=serial_log_short, qemu_log=qemu_log,
                  armed=False, watch_addr=None, stall_seen=False,
                  release_seen=False)

    def cleanup_proc():
        try:
            proc.terminate()
            proc.wait(timeout=5)
        except Exception:
            try:
                proc.kill()
                proc.wait()
            except Exception:
                pass

    for _ in range(200):
        if os.path.exists(serial_sock):
            break
        time.sleep(0.1)
    else:
        print(f"[attempt {attempt_no}] FAIL: serial socket never appeared")
        cleanup_proc()
        return result

    lldb_pypath = subprocess.check_output(["lldb", "-P"]).decode().strip()
    if lldb_pypath not in sys.path:
        sys.path.insert(0, lldb_pypath)
    import lldb

    dbg = lldb.SBDebugger.Create()
    dbg.SetAsync(True)
    target = dbg.CreateTargetWithFileAndArch(None, "aarch64")
    listener = lldb.SBListener(f"c468c6-{attempt_no}")
    cerr = lldb.SBError()
    process = None
    for _ in range(100):
        process = target.ConnectRemote(listener, f"connect://127.0.0.1:{GDB_PORT}", "gdb-remote", cerr)
        if cerr.Success():
            break
        time.sleep(0.1)
    if not cerr.Success():
        print(f"[attempt {attempt_no}] FAIL: gdb-remote connect failed:", cerr)
        cleanup_proc()
        return result
    print(f"[attempt {attempt_no}] gdb-remote attached, CPU paused at reset -- "
          f"full-speed boot, tailing serial for the STALL marker")

    stop_flag = threading.Event()
    transcript = bytearray()
    transcript_lock = threading.Lock()

    # CHALICE C4-68C6: LIVE SERIAL TEE -- line-buffered flush to
    # serial_log_full as bytes arrive, not just a single write at the end
    # (c5's blindness: no mid-run visibility into the transcript at all).
    tee_f = open(serial_log_full, "w", buffering=1)
    tee_leftover = bytearray()
    tee_lock = threading.Lock()

    def tee_write(chunk):
        with tee_lock:
            tee_leftover.extend(chunk)
            if b"\n" in tee_leftover:
                text = tee_leftover.decode(errors="replace")
                lines = text.split("\n")
                tee_leftover.clear()
                tee_leftover.extend(lines[-1].encode(errors="replace"))
                for ln in lines[:-1]:
                    tee_f.write(ln + "\n")
                tee_f.flush()

    def tee_final_flush():
        with tee_lock:
            if tee_leftover:
                tee_f.write(tee_leftover.decode(errors="replace"))
                tee_f.flush()

    def serial_driver():
        s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        s.connect(serial_sock)
        s.settimeout(0.2)

        def drain(sec):
            end = time.time() + sec
            while time.time() < end and not stop_flag.is_set():
                try:
                    chunk = s.recv(65536)
                    if not chunk:
                        break
                    with transcript_lock:
                        transcript.extend(chunk)
                    tee_write(chunk)
                except socket.timeout:
                    pass
        drain(6)
        s.sendall(b"pe10 fex\r")
        drain(max(5, total_timeout - 10))
        s.close()

    th_serial = threading.Thread(target=serial_driver, daemon=True)
    th_serial.start()

    process.Continue()

    watch_addr = None
    marker_seen = False
    marker_deadline = time.time() + marker_wait_timeout
    while time.time() < marker_deadline:
        with transcript_lock:
            buf = bytes(transcript)
        if watch_addr is None:
            m = ARMED_RE.search(buf)
            if m:
                watch_addr = int(m.group(1), 16)
                print(f"[attempt {attempt_no}] live-read watchpoint addr from boot's own "
                      f"ARMED print: 0x{watch_addr:x}")
        if not result["stall_seen"] and STALL_RE.search(buf):
            result["stall_seen"] = True
            marker_seen = True
            print(f"[attempt {attempt_no}] STALL marker observed on serial -- "
                  f"guest is inside/entering its {STALL_SECONDS}s busy-wait, "
                  f"arming now (no race)")
            break
        if not th_serial.is_alive():
            break
        time.sleep(0.02)
    else:
        print(f"[attempt {attempt_no}] FAIL: STALL marker not observed within "
              f"marker_wait_timeout ({marker_wait_timeout}s)")

    if not marker_seen:
        stop_flag.set()
        tee_final_flush()
        with transcript_lock:
            txt = bytes(transcript)
        result["fault_seen"] = FAULT_RE.search(txt) is not None
        process.Kill()
        cleanup_proc()
        return result

    process.SendAsyncInterrupt()
    stopped = False
    ev0 = lldb.SBEvent()
    for _ in range(100):
        got = listener.WaitForEvent(1, ev0)
        if got and lldb.SBProcess.GetStateFromEvent(ev0) == lldb.eStateStopped:
            stopped = True
            break
    if not stopped:
        print(f"[attempt {attempt_no}] FAIL: process did not stop after interrupt, "
              f"state={process.GetState()}")
        cleanup_proc()
        return result
    print(f"[attempt {attempt_no}] vCPU interrupted during the {STALL_SECONDS}s stall window")

    if watch_addr is None:
        watch_addr = WATCH_ADDR_DEFAULT
        print(f"[attempt {attempt_no}] boot's own ARMED print not seen yet -- "
              f"using default watch addr 0x{watch_addr:x}")

    werr = lldb.SBError()
    wp = target.WatchAddress(watch_addr, WATCH_SIZE, False, True, werr)
    if not werr.Success() or not wp or not wp.IsValid():
        print(f"[attempt {attempt_no}] FAIL: watchpoint arm failed:", werr)
        cleanup_proc()
        return result
    result["armed"] = True
    result["watch_addr"] = watch_addr
    print(f"[attempt {attempt_no}] write watchpoint LATE-ARMED at 0x{watch_addr:x} "
          f"size={WATCH_SIZE} during STALL window -- resuming")

    hits_f = open(hits_log, "w")
    hits_f.write(f"# CHALICE C4-68C6 watch hits -- addr=0x{watch_addr:x} size={WATCH_SIZE} "
                 f"floor-stall late-arm STALL_SECONDS={STALL_SECONDS}\n")
    hits_f.flush()

    watch_start = time.time()
    process.Continue()

    hit_count = 0
    last_values = []
    zero_seen = False
    fault_seen = False
    non_wp_stop_count = 0
    ev = lldb.SBEvent()
    while (time.time() - watch_start < watched_timeout
           and time.time() < marker_deadline + watched_timeout + 30):
        if not result["release_seen"]:
            with transcript_lock:
                buf_r = bytes(transcript[-2048:])
            if RELEASE_RE.search(buf_r):
                result["release_seen"] = True
                print(f"[attempt {attempt_no}] guest RELEASE observed -- "
                      f"stall window over, watchpoint armed throughout")
        if zero_seen and not fault_seen:
            with transcript_lock:
                buf_tail = bytes(transcript[-4096:])
            if FAULT_RE.search(buf_tail):
                fault_seen = True
                hits_f.write("# fault trampoline elr=0x26D0C02B0 observed in serial -- "
                              "stopping watch loop\n")
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
                val = read_val(process, watch_addr)
                val_s = f"0x{val:016x}" if val is not None else "READ_FAIL"
                line = f"hit={hit_count} pc=0x{pc:016x} new_value={val_s}"
                hits_f.write(line + "\n")
                hits_f.flush()
                last_values.append(val)
                last_values = last_values[-10:]
                if val == 0 and not zero_seen:
                    zero_seen = True
                    hits_f.write(f"# ZERO WRITE at hit={hit_count} pc=0x{pc:016x}\n")
                    for k in ("x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
                              "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
                              "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25",
                              "x26", "x27", "x28", "fp", "lr", "sp", "pc", "cpsr"):
                        if k in regs:
                            hits_f.write(f"#   {k}={regs[k]}\n")
                    hits_f.write("# disasm window pc-64..pc+192 (256B):\n")
                    for dl in dump_disasm(process, pc, label).splitlines():
                        hits_f.write("#   " + dl + "\n")
                    hits_f.flush()
                    print(f"[attempt {attempt_no}] ZERO WRITE CAUGHT at hit={hit_count} "
                          f"pc=0x{pc:016x} -- continuing to observe fault/exit")
                process.Continue()
            else:
                non_wp_stop_count += 1
                pc, regs = read_regs(th) if th else (0, {})
                hits_f.write(f"# non-watchpoint stop #{non_wp_stop_count} reason={reason} pc=0x{pc:016x}\n")
                hits_f.flush()
                if non_wp_stop_count >= NON_WP_STOP_CAP:
                    hits_f.write(f"# BAIL: {NON_WP_STOP_CAP} non-watchpoint stops reached -- "
                                  "dumping state and stopping\n")
                    for k in ("x0", "x1", "x2", "x3", "x4", "x5", "x6", "x7", "x8", "x9",
                              "x10", "x11", "x12", "x13", "x14", "x15", "x16", "x17",
                              "x18", "x19", "x20", "x21", "x22", "x23", "x24", "x25",
                              "x26", "x27", "x28", "fp", "lr", "sp", "pc", "cpsr"):
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

    tee_final_flush()
    with transcript_lock:
        txt = bytes(transcript)
    with open(serial_log_short, "w") as f:
        f.write("\n".join(txt.decode(errors="replace").splitlines()[-80:]))

    process.Kill()
    cleanup_proc()
    if os.path.exists(serial_sock):
        try:
            os.remove(serial_sock)
        except Exception:
            pass

    fault_in_serial = FAULT_RE.search(txt) is not None
    print(f"[attempt {attempt_no}] total watchpoint hits: {hit_count} "
          f"fault_seen_in_serial={fault_in_serial} stall_seen={result['stall_seen']} "
          f"release_seen={result['release_seen']}")

    result.update(hit_count=hit_count, fault_seen=fault_in_serial, ok=True,
                  last_values=last_values)
    return result


# ---------------- single-attempt driver (c4-68c6, floor-stall arm) ----------------
TOTAL_TIMEOUT1 = int(os.environ.get("C468C6_TOTAL_TIMEOUT", "3600"))
WATCHED_TIMEOUT1 = int(os.environ.get("C468C6_WATCHED_TIMEOUT", "1800"))
results = []
r1 = run_attempt(1, total_timeout=TOTAL_TIMEOUT1, watched_timeout=WATCHED_TIMEOUT1)
results.append(r1)

print("=== SUMMARY ===")
for i, r in enumerate(results, 1):
    print(f"attempt {i}: stall_seen={r['stall_seen']} release_seen={r['release_seen']} "
          f"armed={r['armed']} hit_count={r['hit_count']} fault_seen={r['fault_seen']} "
          f"last_values={[hex(v) if v is not None else None for v in r['last_values']]}")
    print(f"  hits_log={r['hits_log']}")
    print(f"  serial_log_full={r['serial_log_full']}")
    print(f"  qemu_log={r['qemu_log']}")

any_hits = any(r["hit_count"] > 0 for r in results)
print(f"FINAL: any_watchpoint_hits={any_hits}")
