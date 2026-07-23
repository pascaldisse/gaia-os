// CHALICE C2a Atom A — NT service table + TEB/PEB floor.
// Struct layouts are hand-mirrored from reactos/sdk/include/ndk/{peb_teb.h,
// rtltypes.h,ldrtypes.h} (LP64 / _STRUCT64 branch) by walking field order +
// natural alignment; every offset used below is receipted against either an
// explicit C_ASSERT in the donor header or a hand-computed one (shown in the
// comment) derived the same way (sequential fields, 8-byte pointer/ULONG_PTR
// alignment, 4-byte ULONG, matching the well-known public Win64 TEB/PEB
// offsets as a cross-check). We use raw byte-offset writes (not nested C
// structs) so compiler struct-packing quirks can never silently diverge from
// the receipted layout — same pattern pemod.c uses for the PE header fields.
#include "kernel.h"
#include "ntsvc.h"
#include "ntfile.h"   // CHALICE C3a Atom 3A: loader-path services

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;

static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr16(u8 *p, u16 v)  { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static u64b rd64(const u8 *p) { u64b v = 0; for (int i = 0; i < 8; i++) v |= (u64b)p[i] << (8 * i); return v; }
static u32b rd32(const u8 *p) { u32b v = 0; for (int i = 0; i < 4; i++) v |= (u32b)p[i] << (8 * i); return v; }

static void *nt_zalloc(u64b size) {
    u8 *p = (u8 *)MAlloc(size);
    if (p) for (u64b i = 0; i < size; i++) p[i] = 0;
    return p;
}

// =====================================================================
// A3 — service table + the 3 real handlers.
// =====================================================================

static long NtStub(void) { return STATUS_NOT_IMPLEMENTED; }

// CHALICE C4-25: stashed by nt_wow64_cpureserved_set_context (below, ~line
// 1520 this file) so NtRaiseException (defined earlier in the file, can't
// forward-reference that function's own #defines) can dump the LIVE guest
// x86 I386_CONTEXT (real Eip/Esp/etc, continuously updated in-place by
// BTCpuSetContext with NO syscall round-trip -- C4-11 receipt) at the exact
// moment of a RAISE#1 STATUS_ILLEGAL_INSTRUCTION -- the guest-content
// frontier measurement C4-24 deferred.
static u8 *nt_wow64_cpureserved_dbg;

// IO_STATUS_BLOCK: union{NTSTATUS Status; PVOID Pointer;} (8B, PVOID-sized
// on LP64) + ULONG_PTR Information (8B) = 16B total (HANDOFF/A3 receipt:
// "IO_STATUS_BLOCK(Information=bytes), 16 B struct").
typedef struct { u64b status; u64b information; } NtIoStatusBlock;

long NtWriteFile(void *FileHandle, void *Event, void *ApcRoutine, void *ApcContext,
                  void *IoStatusBlock, const void *Buffer, unsigned long Length,
                  void *ByteOffset, unsigned long *Key) {
    (void)Event; (void)ApcRoutine; (void)ApcContext; (void)ByteOffset; (void)Key;
    if ((u64b)FileHandle != (u64b)NT_STDOUT_HANDLE) return STATUS_NOT_IMPLEMENTED;
    const u8 *p = (const u8 *)Buffer;
    for (unsigned long i = 0; i < Length; i++) putc((char)p[i]);
    if (IoStatusBlock) {
        NtIoStatusBlock *iosb = (NtIoStatusBlock *)IoStatusBlock;
        iosb->status = STATUS_SUCCESS;
        iosb->information = Length;
    }
    return STATUS_SUCCESS;
}

long NtClose(void *Handle) {
    // CHALICE C3a Atom 3A: releases file/section handle-table entries too
    // (ntfile.c); values below its handle base (e.g. NT_STDOUT_HANDLE) are
    // simply ignored there, preserving the original no-op-success behavior.
    nt_file_handle_close(Handle);
    return STATUS_SUCCESS;
}

// CHALICE C4-15: FEX BTCpuGetContext first validates THREAD_GET_CONTEXT via
// NtQueryObject(ObjectBasicInformation), then duplicates the pseudo current-
// thread handle (pinned donor Source/Windows/Common/Handle.h:45-58 and WOW64/
// Module.cpp:697-713). This floor has one process/thread + no ACL boundary:
// every non-null pseudo handle has full granted access; duplication aliases
// the same handle, with NtClose already a harmless no-op for pseudo handles.
#define NT_OBJECT_BASIC_INFORMATION_SIZE 0x38
long NtQueryObject(void *Handle, unsigned long ObjectInformationClass, void *ObjectInformation,
                   unsigned long ObjectInformationLength, unsigned long *ReturnLength) {
    if (!Handle || ObjectInformationClass != 0) return STATUS_INVALID_PARAMETER;
    if (!ObjectInformation) return STATUS_INVALID_PARAMETER;
    if (ObjectInformationLength < NT_OBJECT_BASIC_INFORMATION_SIZE) return STATUS_INFO_LENGTH_MISMATCH;
    u8 *out = (u8 *)ObjectInformation;
    for (int i = 0; i < NT_OBJECT_BASIC_INFORMATION_SIZE; i++) out[i] = 0;
    wr32(out + 4, 0x001FFFFFUL); // OBJECT_BASIC_INFORMATION.GrantedAccess
    wr32(out + 8, 1);           // HandleCount
    wr32(out + 12, 1);          // PointerCount
    if (ReturnLength) *ReturnLength = NT_OBJECT_BASIC_INFORMATION_SIZE;
    return STATUS_SUCCESS;
}

long NtDuplicateObject(void *SourceProcessHandle, void *SourceHandle, void *TargetProcessHandle,
                       void **TargetHandle, unsigned long DesiredAccess, unsigned long HandleAttributes,
                       unsigned long Options) {
    (void)SourceProcessHandle; (void)TargetProcessHandle; (void)DesiredAccess;
    (void)HandleAttributes; (void)Options;
    if (!SourceHandle || !TargetHandle) return STATUS_INVALID_PARAMETER;
    *TargetHandle = SourceHandle;
    return STATUS_SUCCESS;
}

// NtTerminateProcess: "unwind to PeRun w/ exit code — same mechanism PeRun
// already uses for entry return" (SLICE2-SPEC A3). PeRun brackets its call
// to entry (via pe_enter) with nt_setjmp/nt_longjmp; we just stash the exit
// code and jump. Sentinel val=1 (never 0) sidesteps the classic setjmp/
// longjmp "val==0 looks like the first call" ambiguity — the real exit
// code travels out-of-band via nt_exit_code, not via the longjmp val.
unsigned long *nt_exit_jmpbuf;
long nt_exit_code;

// CHALICE C4-45: one-shot LR-capturing diagnostic -- pin the SECOND
// STATUS_INVALID_IMAGE_FORMAT (0xC000007B) call site C4-44 named but left
// unpinned ("the CONTEXT-AT-SVC trace's pc is always the shared
// __wine_syscall_dispatcher trampoline address, not a caller return
// address"). Reuses the EXACT mechanism C4-25's RAISE#1 receipt proved
// live for this floor's WOW64 boot: nt_wow64_cpureserved_dbg points at the
// WOW64_CPURESERVED block FEX keeps as the guest's real I386_CONTEXT
// (BTCpuSetContext writes it in place, no NtSetInformationThread round-
// trip needed to read a fresh copy -- same struct, same offsets, same
// static already declared file-scope above for RAISE#1's own use). If
// this floor's guest-side call to ZwTerminateProcess(hProcess,
// STATUS_INVALID_IMAGE_FORMAT) is itself a genuine WOW64 guest syscall
// (not this floor's OWN synthetic C4-27 stub, which never passes this
// status), the live Eip/Esp this dumps IS the call site -- resolved
// against the image registry (nt_image_resolve) exactly like every other
// caller-pinning receipt this thread has produced.
static int nt_c4_45_dumped;

long NtTerminateProcess(void *ProcessHandle, long ExitStatus) {
    // CHALICE C4-55: unconditional ProcessHandle + live guest Eax/Eip/Esp +
    // guest-stack-window receipt (measure, not guess) -- distinguishes "this
    // call genuinely is our own C4-27 stub's `push eax; push -1; mov
    // eax,0x2C; call BOP`" (ProcessHandle would read back as the exact
    // 0xFFFFFFFF the stub pushed, and the live CPURESERVED Eax field would
    // still hold whatever PeMain's `ret` left there) from "this is a
    // DIFFERENT, FEX/wow64-internal teardown call that never went through
    // our stub at all" (ProcessHandle would NOT be 0xFFFFFFFF, or Eax would
    // not match ExitStatus).
    puts("CHALICE C4-55: NtTerminateProcess ProcessHandle="); puthex((u64b)(unsigned long)(unsigned int)(u64b)ProcessHandle);
    puts(" ExitStatus="); puthex((u64b)(u32b)ExitStatus); puts("\n");
    if (nt_wow64_cpureserved_dbg) {
        u8 *ictx = nt_wow64_cpureserved_dbg + 4;
        // literal offsets (CTX32_EIP_OFF/CTX32_ESP_OFF/CTX32_EAX_OFF's own
        // #defines live later in this file, after C4-45's pre-existing code
        // below which already duplicates this same pattern -- 0xB8/0xC4/0xB0,
        // wine/include/winnt.h _I386_CONTEXT field order, Eax@0xB0/Eip@0xB8/
        // Esp@0xC4).
        u32b g_eip = rd32(ictx + 0xB8), g_esp = rd32(ictx + 0xC4), g_eax = rd32(ictx + 0xB0);
        puts("CHALICE C4-55: live guest Eax="); puthex(g_eax);
        puts(" Eip="); puthex(g_eip);
        puts(" Esp="); puthex(g_esp); puts("\n");
        puts("CHALICE C4-55 GUEST STACK @Esp..+16:");
        for (int w = 0; w < 16; w += 4) {
            u64b a = (u64b)(u32b)((u64b)g_esp + (u64b)w);
            u32b word;
            puts(" ");
            if (nt_fault_read32(a, &word)) puthex(word);
            else puts("????????");
        }
        puts("\n");
    } else {
        puts("CHALICE C4-55: nt_wow64_cpureserved_dbg NULL -- no live guest context available\n");
    }
    (void)ProcessHandle;
    if (!nt_c4_45_dumped &&
        (unsigned long)(unsigned int)ExitStatus == 0xC000007BUL /* STATUS_INVALID_IMAGE_FORMAT */ &&
        nt_wow64_cpureserved_dbg) {
        nt_c4_45_dumped = 1;
        u8 *ictx = nt_wow64_cpureserved_dbg + 4;   // +4 == WOW64_CPURESERVED_HDR_SIZE (Flags+Machine)
        u32b g_eip = rd32(ictx + 0xB8), g_esp = rd32(ictx + 0xC4), g_ebp = rd32(ictx + 0xB4);
        puts("CHALICE C4-45: NtTerminateProcess(STATUS_INVALID_IMAGE_FORMAT) live guest Eip=");
        puthex(g_eip);
        char name[64]; u64b off = 0;
        if (g_eip && nt_image_resolve((u64b)g_eip, name, sizeof(name), &off)) {
            puts(" ("); puts(name); puts("+"); puthex(off); puts(")");
        } else {
            puts(" (unresolved -- not in any mapped image)");
        }
        puts(" Esp="); puthex(g_esp); puts(" Ebp="); puthex(g_ebp); puts("\n");
        // Guest-memory hexdump [Eip-16,Eip+32) -- same safe classified read
        // C4-25's own receipt used, lets a disasm confirm/refute the resolve.
        puts("CHALICE C4-45 GUEST BYTES @Eip-16..+32:");
        for (long w = -16; w < 32; w += 4) {
            u64b a = (u64b)(u32b)((long)g_eip + w);
            u32b word;
            puts(" ");
            if (nt_fault_read32(a, &word)) puthex(word);
            else puts("????????");
        }
        puts("\n");
        // One frame up the guest C stack, same [ebp+0]/[ebp+4] walk C4-41 used.
        if (g_ebp) {
            u32b ret_addr = 0, saved_ebp = 0;
            const u8 *ebp_p = (const u8 *)(u64b)g_ebp;
            ret_addr = rd32(ebp_p + 4);
            saved_ebp = rd32(ebp_p + 0);
            puts("CHALICE C4-45: [ebp+4](retaddr)="); puthex(ret_addr);
            char rname[64]; u64b roff = 0;
            if (ret_addr && nt_image_resolve(ret_addr, rname, sizeof(rname), &roff)) {
                puts(" ("); puts(rname); puts("+"); puthex(roff); puts(")");
            }
            puts(" saved_ebp="); puthex(saved_ebp); puts("\n");
        }
    }
    nt_exit_code = ExitStatus;
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    return STATUS_SUCCESS;   // unreachable
}

// CHALICE C3c-2k p1: NtRaiseException -- makes every future Wine internal
// abort/debug fault LEGIBLE instead of a silent kernel Data Abort. Before
// this, an out-of-range id (any real Wine syscall >= the old
// NT_SERVICE_COUNT=128 table cap, e.g. this one, real id 207) hit
// ntdisp.S's bad_service path and returned STATUS_INVALID_SYSTEM_SERVICE
// forever -- Wine's own "unimplemented function ... aborting" handler
// (stock ntdll) retries NtRaiseException in an unbroken loop on that
// answer (28233 consecutive hits observed, C3c-2j), degrading kernel state
// until a real ARM64 Data Abort. Growing the table (ntids.h,
// NT_SERVICE_COUNT 128->512) lets id 207 resolve to a REAL handler now:
// read EXCEPTION_RECORD.ExceptionCode(@0)/ExceptionAddress(@0x10) (winnt.h
// _EXCEPTION_RECORD layout), print one line naming the fault (resolved
// against the image registry, ntfile.c's nt_image_resolve, when the
// address falls inside a mapped image), then cleanly terminate the process
// via the same nt_setjmp/nt_longjmp unwind channel NtTerminateProcess uses
// (SLICE2-SPEC A3 mechanism, reused verbatim) -- turns an unrecoverable
// kernel crash into a one-line diagnosis + clean return to the shell.
// CHALICE C4-24: full-record instrumentation, capped -- step 3 of the
// STATUS_INVALID_DISPOSITION-loop task. Dumps EXCEPTION_RECORD (code,
// flags, ExceptionRecord-chain ptr, NumberParameters, params[0..3]),
// FirstChance, and CONTEXT pc/sp/lr(x30) for the first N syscall entries so
// the loop anatomy (new-vs-same record, growing-vs-static sp) is measured,
// not guessed. winnt.h _EXCEPTION_RECORD: Code@0 Flags@4 Record@8 Addr@0x10
// NumberParameters@0x18 Information[]@0x20 (8B each). CONTEXT (ARM64):
// Sp@0x100 Pc@0x108, X30(Lr)@0x008+30*8=0x0f0 (kmain.c ARM64_CONTEXT_*).
// CHALICE C4-30: tentative (no-initializer) forward file-scope declarations
// of the C4-27 stub-tracking statics (real definitions later in this file,
// ~line 577) -- legal C (multiple tentative definitions at file scope merge
// into one), needed so RAISE#1's receipt below (defined earlier in the
// file) can read them without restructuring the file.
static u64b nt_wow64_stub_addr;
static int  nt_wow64_stub_addr_valid;
static int  nt_wow64_stub_written;

static u64b nt_raise_trace_count;
long NtRaiseException(void *ExceptionRecord, void *Context, unsigned long FirstChance) {
    u8 *er = (u8 *)ExceptionRecord;
    u32b code = 0, flags = 0;
    u64b addr = 0, nparams = 0, rec_link = 0;
    if (er) {
        code = rd32(er + 0x00);
        flags = rd32(er + 0x04);
        rec_link = rd64(er + 0x08);
        for (int i = 0; i < 8; i++) addr |= (u64b)er[0x10 + i] << (8 * i);
        nparams = rd64(er + 0x18);
    }
    puts("CHALICE: NtRaiseException code="); puthex(code);
    puts(" addr="); puthex(addr);
    char name[64]; u64b off = 0;
    if (addr && nt_image_resolve(addr, name, sizeof(name), &off)) {
        puts(" ("); puts(name); puts("+"); puthex(off); puts(")");
    }
    puts("\n");
    if (nt_raise_trace_count < 24) {
        nt_raise_trace_count++;
        u8 *ctx = (u8 *)Context;
        puts("CHALICE RAISE#"); puthex(nt_raise_trace_count);
        puts(": flags="); puthex(flags);
        puts(" recordlink="); puthex(rec_link);
        puts(" nparams="); puthex(nparams);
        if (nparams > 4) nparams = 4;
        for (u64b i = 0; i < nparams; i++) {
            puts(" info["); puthex(i); puts("]="); puthex(rd64(er + 0x20 + i * 8));
        }
        puts(" firstchance="); puthex((u64b)FirstChance);
        if (ctx) {
            puts(" ctx.pc="); puthex(rd64(ctx + 0x108));
            puts(" ctx.sp="); puthex(rd64(ctx + 0x100));
            puts(" ctx.lr(x30)="); puthex(rd64(ctx + 0x008 + 30 * 8));
        } else {
            puts(" ctx=NULL");
        }
        puts("\n");
        // CHALICE C4-25: RAISE#1 STATUS_ILLEGAL_INSTRUCTION guest-content
        // frontier -- ctx.pc above is ALWAYS libwow64fex+0x34E4 (BTCpuSimulate
        // +0x10, FEX's RtlCaptureContext-anchored native unwind target,
        // Module.cpp:757-777) regardless of where the real x86 fault is; it
        // carries zero guest information by FEX's own design (memcpy'd over
        // the real native Context right before this syscall,
        // Module.cpp:947). The REAL guest Eip lives in the WOW64_CPURESERVED
        // block's embedded I386_CONTEXT (TEB64.TlsSlots[WOW64_TLS_
        // CPURESERVED]) -- BTCpuResetToConsistentStateImpl (Module.cpp:momoized
        // 871-946) reconstructs WowContext from the live FEXCore thread
        // state then calls BTCpuSetContext(...) to write it back into THIS
        // exact memory (Module.cpp:871-946), with NO NtSetInformationThread
        // syscall round-trip (confirmed live: no id=13 SET call appears
        // between the last BTCpuSimulate entry and this RAISE#1) -- read it
        // directly.
        // CHALICE C4-30: was `code == 0xC000001DUL` only -- C4-29's fix
        // changed the terminal code to 0xC0000005 (STATUS_ACCESS_VIOLATION)
        // on this pass; the receipt is useful at RAISE#1 regardless of which
        // status code got there, so key ONLY on nt_raise_trace_count==1.
        if (nt_raise_trace_count == 1 && nt_wow64_cpureserved_dbg) {
            // Offsets duplicated from I386_CONTEXT (winnt.h _CONTEXT, x86) --
            // canonical CTX32_* #defines live later in this file (~line 918),
            // can't forward-reference; same duplication convention this file
            // already uses for WOW64_CPURESERVED_HDR_SIZE.
            u8 *ictx = nt_wow64_cpureserved_dbg + 4; // +4 == WOW64_CPURESERVED_HDR_SIZE (Flags+Machine)
            u32b g_edi = rd32(ictx + 0x9C), g_esi = rd32(ictx + 0xA0), g_ebx = rd32(ictx + 0xA4);
            u32b g_edx = rd32(ictx + 0xA8), g_ecx = rd32(ictx + 0xAC), g_eax = rd32(ictx + 0xB0);
            u32b g_ebp = rd32(ictx + 0xB4), g_eip = rd32(ictx + 0xB8), g_cs  = rd32(ictx + 0xBC);
            u32b g_efl = rd32(ictx + 0xC0), g_esp = rd32(ictx + 0xC4), g_ss  = rd32(ictx + 0xC8);
            puts("CHALICE C4-25 GUEST CTX: Eip="); puthex(g_eip);
            char gname[64]; u64b goff = 0;
            if (g_eip && nt_image_resolve((u64b)g_eip, gname, sizeof(gname), &goff)) {
                puts(" ("); puts(gname); puts("+"); puthex(goff); puts(")");
            }
            puts(" Esp="); puthex(g_esp); puts(" Eax="); puthex(g_eax);
            puts(" Ecx="); puthex(g_ecx); puts(" Edx="); puthex(g_edx);
            puts(" Ebx="); puthex(g_ebx); puts(" Ebp="); puthex(g_ebp);
            puts(" Esi="); puthex(g_esi); puts(" Edi="); puthex(g_edi);
            puts(" Cs="); puthex(g_cs); puts(" Ss="); puthex(g_ss);
            puts(" EFlags="); puthex(g_efl); puts("\n");
            // Guest-memory hexdump [Eip-16, Eip+32) -- safe classified read
            // only (nt_fault_read32, ntfile.c), 4B at a time, 12 words = 48B.
            puts("CHALICE C4-25 GUEST BYTES @Eip-16..+32:");
            for (long w = -16; w < 32; w += 4) {
                u64b a = (u64b)(u32b)((long)g_eip + w);
                u32b word;
                puts(" ");
                if (nt_fault_read32(a, &word)) puthex(word);
                else puts("????????");
            }
            puts("\n");

            // CHALICE C4-30 MEASURE step 2: is the C4-27 stub return-address
            // still seeded on the stack? Hexdump [Esp-8,Esp+16) -- 6 words,
            // 24 bytes -- same safe classified read as the Eip dump above.
            puts("CHALICE C4-30 GUEST STACK @Esp-8..+16:");
            for (long w = -8; w < 16; w += 4) {
                u64b a = (u64b)(u32b)((long)g_esp + w);
                u32b word;
                puts(" ");
                if (nt_fault_read32(a, &word)) puthex(word);
                else puts("????????");
            }
            puts("\n");

            // CHALICE C4-30 MEASURE step 2: is the C4-27 stub page itself
            // intact -- 13 bytes (0xD): 50 6A FF B8 2C000000 E8 rel32.
            if (nt_wow64_stub_addr_valid) {
                puts("CHALICE C4-30 STUB PAGE addr="); puthex(nt_wow64_stub_addr);
                puts(" written="); puthex((u64b)nt_wow64_stub_written);
                puts(" bytes:");
                for (long w = 0; w < 16; w += 4) {
                    u64b a = nt_wow64_stub_addr + (u64b)w;
                    u32b word;
                    puts(" ");
                    if (nt_fault_read32(a, &word)) puthex(word);
                    else puts("????????");
                }
                puts("\n");
            } else {
                puts("CHALICE C4-30 STUB PAGE: addr not yet reserved\n");
            }
        }
    }
    // CHALICE C4-24 fix (STATUS_INVALID_DISPOSITION loop) -- donor-grounded
    // in wine/dlls/ntdll/exception.c:239-370 dispatch_exception(): the ONLY
    // call site in the whole donor that passes FirstChance=FALSE is
    // dispatch_exception's own tail, `return NtRaiseException(rec, context,
    // FALSE);`, reached exactly when call_vectored_handlers found nothing,
    // call_seh_handlers (signal_arm64.c:217-303, ARM64 .pdata/virtual_unwind
    // frame search) returned STATUS_UNHANDLED_EXCEPTION, and status ==
    // STATUS_UNHANDLED_EXCEPTION skipped the `RtlRaiseStatus(status)` branch
    // just above it. On real Windows this is the second-chance report to
    // the kernel: with no debugger attached (this floor has none -- PEB
    // BeingDebugged is unconditionally 0, ntsvc.c nt_build_teb receipt),
    // the kernel's own second-chance handling terminates the process; it
    // does NOT re-run KiUserExceptionDispatcher a second time. Every
    // RtlRaiseException-driven FIRST-CHANCE call (RtlRaiseException's own
    // asm, signal_arm64.c:745-771, unconditionally passes FirstChance=TRUE
    // at label "1:") is unaffected by this branch and keeps the existing
    // C4-18 dispatcher route below.
    //
    // Prior floor code called nt_raise_guest_exception() unconditionally,
    // ignoring FirstChance -- re-entering KiUserExceptionDispatcher on the
    // FALSE call re-ran dispatch_exception on the SAME already-unhandled
    // WOW64-boundary context (FEX's BTCpuSimulate-anchored TLS.EntryContext
    // reconstruction, Module.cpp:757-777), which recursed into wine's own
    // raise_status()/RtlRaiseException() defensive retry loop
    // (exception.c:358-367, `for(;;) RtlRaiseException(&rec)` -- meant to
    // never execute more than once since RtlRaiseException is
    // DECLSPEC_NORETURN on real hardware). MEASURED (C4-24 instrumented
    // capture, pe10 fex): RAISE#1 firstchance=0 code=0xC000001D
    // ctx.pc=libwow64fex+0x34E4(BTCpuSimulate+0x10) addr=0; RAISE#2..#24
    // firstchance=0 code=0xC0000026(STATUS_INVALID_DISPOSITION)
    // addr=ntdll+0x3868C(raise_status+0x18) ctx.pc==ctx.lr==that same
    // address every time, ctx.sp monotonically DECREASING by ~0x6c0 bytes
    // per raise (0x4116B0E0 -> 0x4116AA20 -> ... over RAISE#2..#18) --
    // genuine unbounded guest C-stack recursion driven purely by this
    // floor's improper re-dispatch of the FALSE call, one exception.c
    // dispatch_exception/call_seh_handlers/raise_status lap per syscall
    // round-trip. Not a new record each time in the Windows sense (same
    // ExceptionAddress, same code, same flags=EXCEPTION_NONCONTINUABLE=9
    // every iteration) -- it is wine's own raise_status loop, made to spin
    // by this floor re-entering the dispatcher after the exception was
    // already second-chance/unhandled.
    //
    // Fix: FirstChance==0 terminates cleanly (same nt_setjmp/nt_longjmp
    // channel NtTerminateProcess/NtRaiseHardError already use) instead of
    // re-entering KiUserExceptionDispatcher. FirstChance!=0 keeps the
    // C4-18 route unchanged.
    if (!FirstChance) {
        puts("CHALICE: NtRaiseException FirstChance=FALSE (second-chance, unhandled) -- terminating\n");
        nt_exit_code = (long)code;
        if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
        for (;;) asm volatile("wfe");
    }
    // C4-18: same KiUserExceptionDispatcher route as a hardware trap.
    // This does not return unless dispatcher policy itself eventually exits.
    nt_raise_guest_exception(ExceptionRecord, Context);
}

// CHALICE C3c-2k p1: NtRaiseHardError -- same clean-terminate treatment as
// NtRaiseException (optional per plan, cheap to add, same abort-legibility
// payoff for this sibling Wine fault-reporting path). Minimal: print
// ErrorStatus, terminate. Full response-option handling (Parameters array,
// Response out-param) not needed here -- nothing in this chain waits for a
// hard-error response, the process just needs to unwind cleanly instead of
// retry-looping into a bad_service crash the way NtRaiseException did.
long NtRaiseHardError(unsigned long ErrorStatus, unsigned long NumberOfParameters,
                       unsigned long UnicodeStringParameterMask, void *Parameters,
                       unsigned long ValidResponseOptions, void *Response) {
    (void)NumberOfParameters; (void)UnicodeStringParameterMask;
    (void)Parameters; (void)ValidResponseOptions; (void)Response;
    puts("CHALICE: NtRaiseHardError status="); puthex(ErrorStatus); puts("\n");
    nt_exit_code = (long)(ErrorStatus & 0xFFFFFFFFUL);
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    return STATUS_SUCCESS;   // unreachable
}

// =====================================================================
// Atom B — flat page-aligned VM arena (NtAllocateVirtualMemory /
// NtFreeVirtualMemory). HANDOFF: "flat arena OK, skip VAD tree".
// CHALICE C3c-2g: no longer backed by mem.c's 64 MB MAlloc heap (that
// starved the stock loader — ntdll's own image-load reservations alone
// exhausted it before kernel32.dll/kernelbase.dll could ever fit). Backed
// instead by ntfile.c's DLL-territory bump allocator (nt_territory_alloc,
// 0x45000000-0x7ff00000, ~0.75 GB of identity-mapped RAM under QEMU -m 1G,
// already the backing store for NtMapViewOfSection's image maps) — one
// shared territory bump pointer for VM-arena regions, FS-fallback file
// blobs (below), and mapped images, so none of the three can overlap.
// nt_territory_alloc zero-fills every page it hands back (ntdll assumes
// freshly committed VM reads as zero) and is page-rounded already, so no
// separate raw/aligned split is needed here (unlike the old MAlloc trick).
#define NT_PAGE_SIZE 4096UL
#define NT_VM_MAX_REGIONS 64

static struct {
    void   *raw;        // == aligned; territory is already page-aligned
    void   *aligned;     // page-aligned pointer handed to the PE side
    u64b    size;         // page-rounded size
    int     used;
} nt_vm_regions[NT_VM_MAX_REGIONS];

// CHALICE C4-8 (monad ruling #2): pool selection -- `high_first` asks for
// the HIGH (>=4GB, unconstrained) pool before the LOW (sub-4GB, 32-bit-
// visible) one, falling back to LOW if HIGH is exhausted; LOW is also the
// unconditional path when `high_first` is false (constrained/32-bit-
// ceiling requests never belong above 4GB). Caller (NtAllocateVirtualMemory
// below) derives `high_first` from ZeroBits==0 -- see the donor-grounded
// receipt at its call site.
static void *nt_vm_alloc_pool(u64b size, int high_first) {
    u8 *aligned = 0;
    if (high_first) aligned = nt_high_territory_alloc(size);   // ntfile.h
    if (!aligned) aligned = nt_territory_alloc(size);           // ntfile.h; LOW pool / fallback
    if (!aligned) return 0;
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (nt_vm_regions[i].used) continue;
        nt_vm_regions[i].raw = aligned;
        nt_vm_regions[i].aligned = aligned;
        nt_vm_regions[i].size = size;
        nt_vm_regions[i].used = 1;
        return aligned;
    }
    // no tracking slot left — the territory bump has no reclaim anyway, so
    // there's nothing to undo here; just fail this call cleanly.
    return 0;
}

// CHALICE C3a Atom 3A: does `addr` fall inside a live tracked VM region?
// Backs NtAllocateVirtualMemory's RESERVE-then-COMMIT-at-base growth path.
int nt_vm_region_query(unsigned long addr, unsigned long *base_out, unsigned long *size_out) {
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (!nt_vm_regions[i].used) continue;
        u64b base = (u64b)nt_vm_regions[i].aligned;
        if (addr < base || addr >= base + nt_vm_regions[i].size) continue;
        if (base_out) *base_out = base;
        if (size_out) *size_out = nt_vm_regions[i].size;
        return 1;
    }
    return 0;
}

unsigned long nt_vm_region_next(unsigned long addr) {
    u64b next = 0;
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (!nt_vm_regions[i].used) continue;
        u64b base = (u64b)nt_vm_regions[i].aligned;
        if (base > addr && (!next || base < next)) next = base;
    }
    return next;
}

static int nt_vm_region_contains(void *addr) {
    return nt_vm_region_query((u64b)addr, 0, 0);
}

// Returns page-rounded size of the freed region via *out_size, or 0 if
// aligned wasn't a live region (unknown/already-freed base).
// CHALICE C3c-2g: the territory backing this region is bump-only (no
// reclaim) — matches HANDOFF's own "0.75 GB is ample for load-time, do NOT
// over-engineer a reclaiming allocator now". Free just marks the tracking
// slot unused (frees the *handle*, not the bump space); nt_vm_region_contains
// correctly stops seeing it as live either way.
static int nt_vm_free(void *aligned, u64b *out_size) {
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (!nt_vm_regions[i].used || nt_vm_regions[i].aligned != aligned) continue;
        if (out_size) *out_size = nt_vm_regions[i].size;
        // CHALICE C-hello7 (continuation #3): nt_territory_free is now a
        // no-op accounting stub (ntfile.c receipt -- the free-list pool
        // this call used to feed was reverted per monad ruling). Kept as an
        // API-surface call for symmetry/documentation; the real reclaim for
        // this tracking slot is nt_vm_regions_release_since below.
        nt_territory_free((unsigned char *)nt_vm_regions[i].aligned, (unsigned long)nt_vm_regions[i].size);
        nt_vm_regions[i].used = 0;
        return 1;
    }
    return 0;
}

// CHALICE C-hello7 (continuation #3): process-epoch reclaim for the VM-arena
// tracking table, companion to ntfile.c's nt_territory_mark/
// _release_since. Root cause of a live pe8 in-single-boot exhaustion found
// AFTER the territory bump-pointer reset already landed: this table is a
// SEPARATE fixed-size (NT_VM_MAX_REGIONS=64) bookkeeping array from the
// territory bump pointer, and nothing was ever resetting IT across
// nt_boot_process calls -- every stock boot's own kernelbase heap-init
// negotiates many top-level VirtualAlloc reservations (observed: 15-20+
// live 0xFD0000 heaps kept per boot, matching real Wine/Windows kernelbase
// startup creating several genuinely-not-freed process heaps) and NONE of
// them are ever released by a real NtFreeVirtualMemory during this floor's
// single-process-at-a-time model, so their tracking slots stayed "used"
// forever, permanently consuming the 64-slot table across pe6+pe7+pe8 in
// one QEMU session -- nt_vm_alloc started failing (STATUS_NO_MEMORY) with
// HUNDREDS of MB of raw territory still free, because the search for a
// free TRACKING SLOT (not territory space) came up empty. Same fix shape
// as the territory bump reset: since a shell verb is a complete,
// non-overlapping process lifetime (SLICE3-SPEC), nothing after it returns
// needs its own heap regions to stay tracked -- so nt_boot_process drops
// every tracking slot this boot created (by high-water-mark address
// comparison, mirroring nt_territory_release_since's own mark test)
// alongside the bump-pointer reset it already does.
void nt_vm_regions_release_since(unsigned long mark) {
    for (int i = 0; i < NT_VM_MAX_REGIONS; i++) {
        if (nt_vm_regions[i].used && (u64b)nt_vm_regions[i].aligned >= mark)
            nt_vm_regions[i].used = 0;
    }
}

// CHALICE C3c-2e p1: heap-init alloc I/O trace, gated on the same
// nt_dispatch_log_enabled flag pe6 already arms (shell.c) -- prints the
// full 64-bit in-value/out-value of the two in-out params (*BaseAddress,
// *RegionSize) plus AllocationType/Protect/status, so a truncated-width or
// unzeroed-page bug in the LLP64 in-out contract shows up directly in the
// serial trace rather than being inferred from the eventual fault.
static void nt_avm_log(const char *tag, void *base, u64b size, u64b atype,
                        u64b protect, long status) {
    if (!nt_dispatch_log_enabled) return;
    puts("CHALICE AVM "); puts(tag);
    puts(" base="); puthex((u64b)base);
    puts(" size="); puthex(size);
    puts(" atype="); puthex(atype);
    puts(" prot="); puthex(protect);
    puts(" status="); puthex((u64b)status);
    puts("\n");
}

// CHALICE C4-8 (monad ruling #3): loop-breaker hygiene -- N consecutive
// *failed* NtAllocateVirtualMemory requests for the SAME page-rounded size
// (both pools exhausted) means the caller is retrying with zero backoff --
// unbounded and unrecoverable at this floor's territory sizing (HANDOFF
// leg-7: Box64's own init settles into exactly this pattern, requesting a
// stuck-fixed 0x20000/128KB chunk forever once both pools fill). N default
// ~256 (parameterized, not hardcoded) -- one legible CHALICE FAULT line
// (size + resolved caller module+offset, via the real x9 caller LR stashed
// at the syscall frame's stub_lr slot, NT_FRAME_STUB_LR_OFF -- ntdisp.S/
// ntids.h receipt) then a clean process termination through the same
// nt_setjmp/nt_longjmp unwind channel NtTerminateProcess/NtRaiseException
// already use above -- never an infinite kernel-side spin again.
#define NT_AVM_LOOPBREAK_N 256

static u64b nt_avm_loopbreak_last_size = 0;
static unsigned long nt_avm_loopbreak_count = 0;

static void nt_avm_loopbreak_reset(void) {
    nt_avm_loopbreak_last_size = 0;
    nt_avm_loopbreak_count = 0;
}

static void nt_avm_loopbreak_hit(u64b pages) {
    if (pages == nt_avm_loopbreak_last_size) {
        nt_avm_loopbreak_count++;
    } else {
        nt_avm_loopbreak_last_size = pages;
        nt_avm_loopbreak_count = 1;
    }
    if (nt_avm_loopbreak_count < NT_AVM_LOOPBREAK_N) return;

    u64b teb; __asm__ volatile("mov %0, x18" : "=r"(teb));
    u64b frame = rd64((const u8 *)(teb + NT_TEB_SYSCALL_FRAME_OFF));
    u64b caller = frame ? rd64((const u8 *)(frame + NT_FRAME_STUB_LR_OFF)) : 0;
    char name[64]; u64b off = 0;
    puts("CHALICE FAULT: NtAllocateVirtualMemory loop-broken N=");
    puthex((u64b)nt_avm_loopbreak_count);
    puts(" size="); puthex(pages);
    puts(" caller="); puthex(caller);
    if (caller && nt_image_resolve(caller, name, sizeof(name), &off)) {
        puts(" ("); puts(name); puts("+"); puthex(off); puts(")");
    }
    puts(" -- terminating process cleanly\n");

    nt_exit_code = (long)STATUS_NO_MEMORY;
    if (nt_exit_jmpbuf) nt_longjmp(nt_exit_jmpbuf, 1);
    // unreachable if jmpbuf set (the normal case, a real guest process);
    // falls through to the ordinary STATUS_NO_MEMORY return otherwise,
    // counter left saturated so this line keeps firing rather than going
    // silent again.
}

// CHALICE C4-15: real NtFlushInstructionCache floor. FEX's WinAPI wrapper
// forwards FlushInstructionCache straight here (FEX/Source/Windows/Common/
// WinAPI/Alloc.cpp:110-111); the next-stage JIT executes bytes written into
// freshly allocated ARM64 code pages, so STATUS_SUCCESS without cache
// maintenance is not sufficient. Same dc-cvau/dsb/ic-ivau/dsb/isb sequence
// already used by this kernel's own JIT (`hc.c:657-663`). NULL/zero means a
// process-wide synchronization request: barriers still run, no range walk.
long NtFlushInstructionCache(void *ProcessHandle, const void *BaseAddress, unsigned long Length) {
    (void)ProcessHandle;
    u64b start = (u64b)BaseAddress;
    u64b end = start + (u64b)Length;
    if (Length && (!start || end < start)) return STATUS_INVALID_PARAMETER;

    if (Length) {
        for (u64b p = start & ~63ULL; p < end; p += 64)
            __asm__ volatile("dc cvau, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish" ::: "memory");
    if (Length) {
        for (u64b p = start & ~63ULL; p < end; p += 64)
            __asm__ volatile("ic ivau, %0" :: "r"(p) : "memory");
    }
    __asm__ volatile("dsb ish; isb" ::: "memory");
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-27: the syscall hinge -- a floor-built i386 stub that PeMain's
// own `ret` lands in (ntboot.c seeds [x86_stack_esp] = this stub's address
// as PeMain's synthetic return address -- conductor's fixed design ruling:
// minimal donor-true RtlUserThreadStart-equivalent, replaced once the full
// 32-bit ntdll boot lands, C4-28 note in HANDOFF). The stub routes
// NtTerminateProcess(NtCurrentProcess(), status=eax) through the REAL
// 32->64 transition -- FEX's own BOP ("bridge op") trampoline, the exact
// address wow64.dll's process_init publishes as Wow64Transition on real
// Windows (donor wine/dlls/wow64/syscall.c:878-880 init_image_mapping,
// :1047 *__wine_syscall_dispatcher=pBTCpuGetBopCode()). FEX's own
// BTCpuGetBopCode (FEX Source/Windows/WOW64/Module.cpp:689-691) just
// returns BridgeInstrs::Syscall, a 4-byte RWX page BTCpuProcessInit
// allocates itself via a REAL NtAllocateVirtualMemory call (Module.cpp:
// 557-561: `SIZE_T Size = 4; NtAllocateVirtualMemory(NtCurrentProcess(),
// &Addr, (1U<<31)-1, &Size, MEM_RESERVE|MEM_COMMIT, PAGE_EXECUTE_
// READWRITE); *(uint32_t*)Addr = 0x2ecd2ecd; BridgeInstrs::Syscall = Addr;`)
// -- a call THIS FLOOR ALREADY SERVICES (ZeroBits!=0 LOW-pool branch,
// below). IRON law (never hardcode a captured address): the hook in that
// branch derives the live BOP address straight off this exact real
// syscall's own return value, every boot -- zero constants.
//
// Calling convention (measured against FEX's own HandleSyscallImpl,
// Module.cpp:438-461, and donor Wow64SystemServiceEx, wine/dlls/wow64/
// syscall.c:1202-1211): guest EAX=syscall id (low 12 bits select
// syscall_tables[0], wow64.dll's own internal ntdll thunk table -- not a
// floor table; id 0x2C == NtTerminateProcess, wine-arm64-syscalls.txt:36,
// matching this floor's own dispatch numbering, same canonical Wine
// numbering both sides already share). Guest stack at the `call [BOP]`
// instruction must already hold [esp+4]=arg0, [esp+8]=arg1 in normal
// __stdcall push order (right-to-left) -- FEX derives its `args` pointer
// as (poppedReturnRSP)+4, exactly the address a genuine "mov eax,id; lea
// edx,[esp+4]; call [Wow64Transition]" ntdll stub would compute BEFORE its
// own internal `call` pushes a return address (EDX itself is never read by
// FEX -- it recomputes purely off ESP, so this floor's stub never needs to
// set EDX). wow64_NtTerminateProcess (wine/dlls/wow64/process.c:1144-1152)
// unpacks args[0]=handle/args[1]=exit_code in that same declared order and
// only special-cases a NULL handle (extra BTCpuProcessTerm call) -- this
// stub pushes -1 (NtCurrentProcess() pseudo-handle) to skip that branch
// (this floor's own NtTerminateProcess ignores ProcessHandle entirely
// anyway, ntsvc.c above -- -1 is the minimal-fault-surface choice, not a
// functional requirement).
// =====================================================================
static u64b nt_wow64_stub_addr;
static int  nt_wow64_stub_addr_valid;
static u64b nt_wow64_bop_addr;
static int  nt_wow64_bop_valid;
static int  nt_wow64_stub_written;

// Called by ntboot.c once, right after allocating the small guest-reachable
// (<4GB, nt_territory_alloc's own LOW-pool/64KB-aligned guarantee) code
// buffer this stub's bytes will live in -- BEFORE the BOP address is known
// (BTCpuProcessInit hasn't run yet at that point in boot), so only the
// ADDRESS is recorded here; the bytes themselves are written lazily by
// nt_wow64_stub_write_if_ready below, the moment BOP is discovered
// (guaranteed to happen before BTCpuThreadInit/BTCpuSimulate ever reads
// the guest stack this stub's address was seeded into -- ProcessInit always
// precedes ThreadInit/Simulate in the real wow64.dll init chain).
void nt_wow64_stub_reserve(unsigned long addr) {
    nt_wow64_stub_addr = addr;
    nt_wow64_stub_addr_valid = 1;
}

static void nt_wow64_stub_write_if_ready(void) {
    if (nt_wow64_stub_written) return;
    if (!nt_wow64_stub_addr_valid || !nt_wow64_bop_valid) return;

    // CHALICE C4-55: ONE extra dummy 4B slot (`push 0`) inserted here, right
    // before `mov eax,id; call BOP` -- FIX for the C4-55-named arg-shift bug.
    // Root cause (measured, not guessed, via a live ProcessHandle/ExitStatus/
    // guest-stack receipt at NtTerminateProcess itself): FEX's own args_ptr
    // formula (donor comment above, `poppedReturnRSP+4`) is calibrated for a
    // REAL ntdll Nt* stub's stack shape, which ALWAYS has TWO return-address
    // slots above the args at trap time -- (1) the external caller's own
    // return address (present because a real ntdll function is always
    // reached via a `call` instruction from outside) and (2) the stub's own
    // `call [Wow64Transition]` return address. This floor's C4-27 stub is
    // instead reached via PeMain's `ret` (no external `call`, so slot (1)
    // never existed) and only ever pushed its own 2 args + slot (2) -- ONE
    // slot short. Live receipt (`scratch/c4-55_pe10_fex_receipt_full.log`)
    // confirmed the exact off-by-one-slot symptom predicted by this gap:
    // ProcessHandle arrived as 0x2A (42 -- our ExitStatus push value, read
    // one slot too shallow) and ExitStatus arrived as 0x0 (stale memory one
    // slot past our real args, never written by this stub at all). Adding
    // this one dummy slot (value never read by anything) restores the exact
    // stack shape FEX's formula expects: args_ptr now lands on our REAL
    // ProcessHandle(-1)/ExitStatus(eax) pair, matching every other syscall
    // this floor already forwards correctly through real ntdll stubs (which
    // naturally have this extra slot already).
    u8  *s = (u8 *)nt_wow64_stub_addr;
    u32b i = 0;
    s[i++] = 0x50;                    // push eax        (ExitStatus = PeMain's own return value)
    s[i++] = 0x6A; s[i++] = 0xFF;     // push -1         (hProcess = NtCurrentProcess() pseudo-handle)
    s[i++] = 0x6A; s[i++] = 0x00;     // push 0          (C4-55 FIX: dummy external-caller-retaddr slot)
    s[i++] = 0xB8;                    // mov eax, imm32
    wr32(s + i, 0x2C); i += 4;        // NtTerminateProcess syscall id (table0, low 12 bits)
    s[i] = 0xE8;                       // call rel32 -> BOP
    {
        u32b rel32 = (u32b)(nt_wow64_bop_addr - (nt_wow64_stub_addr + i + 5));
        wr32(s + i + 1, rel32);
    }
    i += 5;

    nt_wow64_stub_written = 1;
    puts("CHALICE C4-27: guest stub written addr="); puthex(nt_wow64_stub_addr);
    puts(" bop="); puthex(nt_wow64_bop_addr);
    puts(" bytes="); puthex((u64b)i); puts("\n");
}

// CHALICE C4-54: getter for ntfile.c's own nt_wow64_maybe_notify_map_view
// -- the FIRST place nt_wow64_ready flips true -- to retroactively register
// this floor-internal stub page (see that call site's own receipt for why
// it can never be reached via any guest-visible NtAllocateVirtualMemory
// call). Returns 1 (and fills addr/size) only once the bytes are actually
// written (nt_wow64_stub_written); size is the fixed 16B buffer ntboot.c
// allocated (HandleMemoryProtectionNotification aligns to page bounds
// internally, C4-53's own disasm receipt -- no rounding needed here).
int nt_wow64_stub_range(unsigned long *addr_out, unsigned long *size_out) {
    if (!nt_wow64_stub_written) return 0;
    if (addr_out) *addr_out = nt_wow64_stub_addr;
    if (size_out) *size_out = 16;
    return 1;
}

long NtAllocateVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long ZeroBits,
                              unsigned long *RegionSize, unsigned long AllocationType,
                              unsigned long Protect) {
    // CHALICE C4-54: capture the guest TEB as the ABSOLUTE FIRST statement,
    // same x18-clobber-safety idiom C4-48/53 established for NtMapViewOf-
    // Section/NtProtectVirtualMemory (every callee below -- nt_arena_*/
    // nt_vm_alloc_pool/etc -- is plain aarch64-elf C, free to clobber x18).
    // Threaded down to nt_wow64_maybe_notify_protect's own pe_enter6 call
    // site (ntfile.c) at each real EXEC-protect success return below.
    u64b nt_c454_teb; __asm__ volatile("mov %0, x18" : "=r"(nt_c454_teb));
    (void)ProcessHandle;

    void   *in_base = BaseAddress ? *BaseAddress : 0;
    u64b    in_size = RegionSize  ? *RegionSize  : 0;
    nt_avm_log("IN ", in_base, in_size, AllocationType, Protect, 0);

    if (!BaseAddress || !RegionSize) {
        nt_avm_log("OUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // exotic flags -> STATUS_INVALID_PARAMETER (SLICE2-SPEC Atom B); must
    // specify at least one of COMMIT/RESERVE, both together also accepted
    // ("MEM_COMMIT|MEM_RESERVE both = alloc"). CHALICE C4-14: mask widened
    // to match real Wine's own type_mask exactly (wine/dlls/ntdll/unix/
    // virtual.c:5249) -- MEM_TOP_DOWN/MEM_WRITE_WATCH/MEM_RESET are real,
    // commonly-set placement/tracking hints this floor's flat bump
    // allocator doesn't act on but must not reject either (see ntids.h
    // receipt -- this was rejecting FEX's own heap-growth calls).
    if (AllocationType == 0 || (AllocationType & ~(u64b)(MEM_COMMIT | MEM_RESERVE | MEM_TOP_DOWN | MEM_WRITE_WATCH | MEM_RESET)) != 0) {
        nt_avm_log("OUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    u64b size = *RegionSize;
    if (size == 0) {
        nt_avm_log("OUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    u64b pages = (size + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);

    // DEVIATION (documented, not silent): a caller-supplied *BaseAddress
    // hint can't be honored by a flat bump/free-list arena (no VAD tree, no
    // control over where MAlloc's free list happens to have room) --
    // rejected explicitly rather than silently ignored, UNLESS it's the
    // Atom 3A growth path below. hello3's probe always passes NULL
    // (system-chosen base), so this path isn't exercised by the Atom B gate.
    if (*BaseAddress != 0) {
        // CHALICE C4-9 (monad ruling #3): commit-at-existing-base inside the
        // RESERVE/COMMIT arena -- donor virtual.c:5199 commit-only branch
        // (find_view must succeed, i.e. a live reservation must cover
        // base/size, else STATUS_NOT_MAPPED_VIEW). Checked first: arena VA
        // (>=8GB, ntfile.c NT_ARENA_BASE) never overlaps the LOW/HIGH pools'
        // tracked ranges (<5GB) the nt_vm_region_contains path below covers,
        // so this is an unambiguous routing split, not a priority guess.
        if (nt_arena_contains((u64b)*BaseAddress)) {
            if (!(AllocationType & MEM_COMMIT)) {
                nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_INVALID_PARAMETER);
                return STATUS_INVALID_PARAMETER;
            }
            if (!nt_arena_commit((u64b)*BaseAddress, pages)) {
                // Donor: find_view fails (no covering reservation) ->
                // STATUS_NOT_MAPPED_VIEW (virtual.c:5199) -- this floor's
                // nt_arena_commit also returns 0 on HIGH-pool/L2-table-page
                // exhaustion; both are legitimate "can't commit" outcomes,
                // reported the same way (the wrong-base case is the common
                // real one; loop-breaker below still catches sustained
                // exhaustion-driven retries of THIS path via the caller's
                // own retry loop hitting the *BaseAddress==0 branch instead
                // in practice, per Wine's own heap-growth pattern).
                nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_NOT_MAPPED_VIEW);
                return STATUS_NOT_MAPPED_VIEW;
            }
            *RegionSize = pages;
            // CHALICE C4-54: real guest EXEC-protect allocation (donor gap,
            // symmetric to C4-46/53) -- derive base/size from the ACTUAL
            // commit result (IRON), not the caller's hint.
            if (Protect & 0xF0) nt_wow64_maybe_notify_protect((u64b)*BaseAddress, pages, Protect, nt_c454_teb);
            nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }

        // CHALICE C3a Atom 3A: RESERVE-then-COMMIT-at-given-base growth
        // (RtlCreateHeap prerequisite) -- *BaseAddress inside an
        // already-tracked region + MEM_COMMIT => bookkeeping no-op success
        // (flat arena: every tracked page is already committed). Untracked
        // base => keep rejecting (spec: "keep rejecting").
        if (!(AllocationType & MEM_COMMIT)) {
            nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
        if (!nt_vm_region_contains(*BaseAddress)) {
            nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
        // CHALICE C4-54: EXEC-protect notify, same as the commit-at-base
        // branch above -- this is bookkeeping-only success (page already
        // committed), but the guest's own Protect argument is still a real
        // VirtualAlloc(MEM_COMMIT) call FEX's InvalidationTracker needs to
        // learn about if it carries an EXEC bit.
        if (Protect & 0xF0) nt_wow64_maybe_notify_protect((u64b)*BaseAddress, pages, Protect, nt_c454_teb);
        nt_avm_log("OUT", *BaseAddress, pages, AllocationType, Protect, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    // CHALICE C4-9 (monad ruling #1): ZeroBits==0 -> unconstrained ->
    // RESERVATION ARENA now, not the HIGH pool directly (that was the C4-8
    // interim design; superseded here -- HIGH pool still backs the arena's
    // COMMIT windows underneath, just no longer bump-allocates whole
    // regions up front regardless of RESERVE-vs-COMMIT). Donor-grounded
    // ZeroBits routing unchanged from C4-8 (receipt kept, still true):
    // wow64's own thunks ALWAYS pass a nonzero zero_bits (explicit or
    // wow64's own default_zero_bits=0x7fffffff, wine/dlls/wow64/
    // syscall.c:41) through get_zero_bits() (wine/dlls/wow64/
    // wow64_private.h:94-97) into the real 64-bit NtAllocateVirtualMemory;
    // ntdll's own get_zero_bits_limit() (wine/dlls/ntdll/unix/
    // unix_private.h:569-586) turns a nonzero zero_bits into a concrete
    // ceiling, while zero_bits==0 hits `limit=0` == no ceiling (wine/
    // dlls/ntdll/unix/virtual.c:5290-5297) -- genuinely unconstrained
    // native-ARM64 callers (wowbox64.dll's own dynarec heap/code-cache
    // growth) are exactly the ZeroBits==0 case, a 32-bit guest's own
    // VirtualAlloc never is.
    if (ZeroBits == 0) {
        unsigned char *base = nt_arena_reserve(pages);
        if (!base) {
            nt_avm_loopbreak_hit(pages);   // arena VA exhausted -- may terminate the process
            nt_avm_log("OUT", 0, pages, AllocationType, Protect, STATUS_NO_MEMORY);
            return STATUS_NO_MEMORY;
        }
        if (AllocationType & MEM_COMMIT) {
            // MEM_RESERVE|MEM_COMMIT together (or MEM_COMMIT alone with
            // base==0, same donor branch, virtual.c:5176-5180): commit the
            // whole freshly-reserved range up front.
            if (!nt_arena_commit((u64b)base, pages)) {
                nt_avm_loopbreak_hit(pages);   // HIGH-pool backing exhausted -- same loop-breaker as before
                nt_avm_log("OUT", base, pages, AllocationType, Protect, STATUS_NO_MEMORY);
                return STATUS_NO_MEMORY;
            }
        }
        nt_avm_loopbreak_reset();
        *BaseAddress = base;
        *RegionSize = pages;
        // CHALICE C4-54: EXEC-protect notify -- unconstrained-arena growth
        // path (wowbox64.dll's own dynarec code-cache growth is exactly this
        // case, per this function's own pre-existing C4-9 receipt above).
        if (Protect & 0xF0) nt_wow64_maybe_notify_protect((u64b)base, pages, Protect, nt_c454_teb);
        nt_avm_log("OUT", base, pages, AllocationType, Protect, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    // ZeroBits != 0: unchanged LOW-pool-only path (32-bit-ceiling requests
    // never belong in the arena, ruling #1).
    void *mem = nt_vm_alloc_pool(pages, 0);
    if (!mem) {
        nt_avm_loopbreak_hit(pages);   // CHALICE C4-8 ruling #3 -- may terminate the process
        nt_avm_log("OUT", 0, pages, AllocationType, Protect, STATUS_NO_MEMORY);
        return STATUS_NO_MEMORY;
    }
    nt_avm_loopbreak_reset();

    // CHALICE C4-27: FEX's BOP trampoline allocation, live-detected --
    // Module.cpp:557-561's exact shape (Size=4, PAGE_EXECUTE_READWRITE,
    // MEM_RESERVE|MEM_COMMIT, reached this branch so ZeroBits!=0 already
    // holds) -- see the C4-27 block above NtAllocateVirtualMemory for the
    // full donor citation. `size` here is the ORIGINAL requested size (4),
    // captured before page-rounding, still in scope. First match wins
    // (nt_wow64_bop_valid guard) -- BTCpuProcessInit allocates this exactly
    // once per boot.
    if (!nt_wow64_bop_valid && size == 4 && Protect == PAGE_EXECUTE_READWRITE &&
        (AllocationType & (MEM_RESERVE | MEM_COMMIT)) == (MEM_RESERVE | MEM_COMMIT)) {
        nt_wow64_bop_addr = (u64b)mem;
        nt_wow64_bop_valid = 1;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE C4-27: BOP addr discovered="); puthex(nt_wow64_bop_addr); puts("\n");
        }
        nt_wow64_stub_write_if_ready();
    }

    *BaseAddress = mem;
    *RegionSize = pages;
    // CHALICE C4-54: EXEC-protect notify -- LOW-pool path (covers the BOP
    // trampoline allocation above too; harmless no-op there since
    // nt_wow64_ready is still 0 at that point in boot, same gate every
    // other notify call site on this floor already shares).
    if (Protect & 0xF0) nt_wow64_maybe_notify_protect((u64b)mem, pages, Protect, nt_c454_teb);
    nt_avm_log("OUT", mem, pages, AllocationType, Protect, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

// CHALICE C3c-2f p1: NtFreeVirtualMemory I/O trace, gated on the same
// nt_dispatch_log_enabled flag nt_avm_log above uses -- logs the in-out
// *BaseAddress/*RegionSize pair full 64-bit plus FreeType/status, AND
// whether nt_vm_free actually found the region (a partial/base-offset
// release from a live over-reservation would miss the exact-match lookup
// and surface here as "found=0" rather than silently doing nothing).
static void nt_fvm_log(const char *tag, void *base, u64b size, u64b ftype,
                        int found, long status) {
    if (!nt_dispatch_log_enabled) return;
    puts("CHALICE FVM "); puts(tag);
    puts(" base="); puthex((u64b)base);
    puts(" size="); puthex(size);
    puts(" ftype="); puthex(ftype);
    puts(" found="); puthex((u64b)found);
    puts(" status="); puthex((u64b)status);
    puts("\n");
}

long NtFreeVirtualMemory(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                          unsigned long FreeType) {
    (void)ProcessHandle;

    void *in_base = BaseAddress ? *BaseAddress : 0;
    u64b  in_size = RegionSize  ? *RegionSize  : 0;
    nt_fvm_log("IN ", in_base, in_size, FreeType, -1, 0);

    if (!BaseAddress || !*BaseAddress) {
        nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (FreeType != MEM_RELEASE && FreeType != MEM_DECOMMIT) {
        nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // CHALICE C4-9 (monad ruling): arena addresses use their own
    // reservation table (nt_arena_release), not nt_vm_regions -- both
    // MEM_RELEASE and MEM_DECOMMIT are accounting-only here too (ruling:
    // "NtFreeVirtualMemory MEM_RELEASE/MEM_DECOMMIT = accounting-only OK"),
    // real reclaim is the per-boot wholesale reset (ntboot.c).
    if (nt_arena_contains((u64b)*BaseAddress)) {
        if (FreeType == MEM_RELEASE) {
            u64b freed = 0;
            int found = nt_arena_release((u64b)*BaseAddress, &freed);
            if (!found) {
                nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
                return STATUS_INVALID_PARAMETER;
            }
            if (RegionSize) *RegionSize = freed;
            nt_fvm_log("OUT", in_base, freed, FreeType, 1, STATUS_SUCCESS);
            return STATUS_SUCCESS;
        }
        if (RegionSize) *RegionSize = (*RegionSize + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
        nt_fvm_log("OUT", in_base, RegionSize ? *RegionSize : 0, FreeType, 1, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }

    if (FreeType == MEM_RELEASE) {
        u64b freed = 0;
        int found = nt_vm_free(*BaseAddress, &freed);
        if (!found) {
            nt_fvm_log("OUT", in_base, in_size, FreeType, 0, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
        if (RegionSize) *RegionSize = freed;
        nt_fvm_log("OUT", in_base, freed, FreeType, 1, STATUS_SUCCESS);
        return STATUS_SUCCESS;
    }
    // MEM_DECOMMIT: flat arena has no separate commit/decommit state (every
    // page is committed at alloc time) -- no-op success, matches HANDOFF's
    // "flat arena OK" simplification.
    if (RegionSize) *RegionSize = (*RegionSize + (NT_PAGE_SIZE - 1)) & ~(NT_PAGE_SIZE - 1);
    nt_fvm_log("OUT", in_base, RegionSize ? *RegionSize : 0, FreeType, 1, STATUS_SUCCESS);
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-13: NtAllocateVirtualMemoryEx (svc 107, wine-arm64-syscalls.txt
// :103) -- the Ex form FEX's own heap-growth path calls where Box64 calls
// the plain NtAllocateVirtualMemory (measured live, leg-12: two "svc
// id=107 UNIMPLEMENTED" lines immediately before a genuine Data Abort
// INSIDE ntdll.dll's own heap code, far=0x7 -- a small-int deref off an
// uninitialized *ret/*size_ptr the real syscall never got to fill in).
// Donor-grounded (wine/dlls/ntdll/unix/virtual.c:5324 get_extended_params +
// :5384 NtAllocateVirtualMemoryEx, fetched this leg -- no local wine/
// donor clone present this session, HANDOFF's "main working copy has it"
// not true this leg either, same as leg-11 -- fetched the specific files
// needed via curl instead, same grounding standard): same core allocator
// as plain NtAllocateVirtualMemory (donor's allocate_virtual_memory), fed
// by a MEM_EXTENDED_PARAMETER array instead of a ZeroBits scalar -- this
// floor mirrors that by parsing MemExtendedParameterAddressRequirements
// (winnt.h _MEM_ADDRESS_REQUIREMENTS: LowestStartingAddress@0/
// HighestEndingAddress@8/Alignment@16, 24B; MEM_EXTENDED_PARAMETER itself:
// Type:8-bitfield QWORD@0 + union QWORD@8, 16B total, DECLSPEC_ALIGN(8))
// into the same LOW-vs-arena pool choice NtAllocateVirtualMemory's
// ZeroBits==0-vs-!=0 branch already makes: a HighestEndingAddress below
// the HIGH pool's own floor (4GB -- "keep me under 4GB", exactly what a
// WOW64-adjacent 32-bit-visible allocation needs, FEX's own likely use
// case per this leg's task) routes to the LOW pool; no constraint (no
// param, or a param with everything zero) is genuinely unconstrained,
// same as ZeroBits==0. MemExtendedParameterAttributeFlags/ImageMachine are
// parsed+logged only (no ARM64EC support on this floor, so their real
// donor-side effect -- commit_arm64ec_map/set_arm64ec_range, virtual.c
// :5218-5221 -- never applies here); MemExtendedParameterNumaNode/
// PartitionHandle/UserPhysicalHandle are logged+IGNORED, matching donor's
// own FIXME+continue exactly (get_extended_params never rejects these, so
// neither do we); any other/out-of-range Type -> STATUS_INVALID_PARAMETER,
// matching donor's own default case (donor is the ground truth here,
// followed exactly rather than inventing a distinct STATUS_NOT_SUPPORTED
// this floor's task text speculated about).
// =====================================================================
#define MEM_EXTENDED_PARAMETER_ADDRESSREQUIREMENTS 1UL
#define MEM_EXTENDED_PARAMETER_NUMANODE             2UL
#define MEM_EXTENDED_PARAMETER_PARTITIONHANDLE       3UL
#define MEM_EXTENDED_PARAMETER_USERPHYSICALHANDLE    4UL
#define MEM_EXTENDED_PARAMETER_ATTRIBUTEFLAGS        5UL
#define MEM_EXTENDED_PARAMETER_IMAGEMACHINE          6UL
// == ntfile.c's own NT_HIGH_TERRITORY_BASE (private #define there, no
// header exposes it -- duplicated value, not meaning, same convention
// ntids.h's NT_FRAME_STUB_LR_OFF receipt documents for this exact
// situation).
#define NT_C413_HIGH_POOL_FLOOR 0x100000000ULL

long NtAllocateVirtualMemoryEx(void *ProcessHandle, void **BaseAddress, unsigned long *RegionSize,
                                unsigned long AllocationType, unsigned long Protect,
                                void *Parameters, unsigned long ParameterCount) {
    (void)ProcessHandle;

    void *in_base = BaseAddress ? *BaseAddress : 0;
    u64b  in_size = RegionSize ? *RegionSize : 0;
    nt_avm_log("EXIN ", in_base, in_size, AllocationType, Protect, 0);

    if (!BaseAddress || !RegionSize) {
        nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }
    if (ParameterCount && !Parameters) {
        nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    u64b limit_high = 0;      // 0 == unconstrained (donor's own default)
    int  have_addr_req = 0;

    const u8 *params = (const u8 *)Parameters;
    for (unsigned long i = 0; i < ParameterCount; i++) {
        const u8 *p = params + i * 16;   // MEM_EXTENDED_PARAMETER is 16B
        u64b type_field = rd64(p + 0);
        u32b type = (u32b)(type_field & 0xFFUL);   // Type:8 bits, LSB of the first QWORD
        u64b value = rd64(p + 8);

        if (type == MEM_EXTENDED_PARAMETER_ADDRESSREQUIREMENTS) {
            const u8 *req = (const u8 *)(u64b)value;   // Pointer -- MEM_ADDRESS_REQUIREMENTS*
            have_addr_req = 1;
            if (req) {
                u64b highest = rd64(req + 8);   // HighestEndingAddress
                if (highest != 0) limit_high = highest;
            }
        } else if (type == MEM_EXTENDED_PARAMETER_ATTRIBUTEFLAGS ||
                   type == MEM_EXTENDED_PARAMETER_IMAGEMACHINE) {
            // logged only, no ARM64EC support on this floor -- see receipt above.
        } else if (type == MEM_EXTENDED_PARAMETER_NUMANODE ||
                   type == MEM_EXTENDED_PARAMETER_PARTITIONHANDLE ||
                   type == MEM_EXTENDED_PARAMETER_USERPHYSICALHANDLE) {
            // donor FIXME+continue -- ignored, not rejected (get_extended_params ground truth).
        } else {
            if (nt_dispatch_log_enabled) {
                puts("CHALICE: NtAllocateVirtualMemoryEx invalid param type="); puthex(type); puts("\n");
            }
            nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
            return STATUS_INVALID_PARAMETER;
        }
    }

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtAllocateVirtualMemoryEx count="); puthex(ParameterCount);
        puts(" have_addr_req="); puthex((u64b)have_addr_req);
        puts(" limit_high="); puthex(limit_high); puts("\n");
    }

    // Donor: *ret set (growth-at-base) + any address-requirement param ->
    // STATUS_INVALID_PARAMETER (virtual.c:5384 "if (*ret && (align ||
    // limit_low || limit_high)) return STATUS_INVALID_PARAMETER", align/
    // limit_low not modeled separately here since this floor's flat arena
    // never honors a caller base hint anyway -- the have_addr_req flag
    // alone already captures "a requirements param was present").
    if (in_base != 0 && have_addr_req) {
        nt_avm_log("EXOUT", in_base, in_size, AllocationType, Protect, STATUS_INVALID_PARAMETER);
        return STATUS_INVALID_PARAMETER;
    }

    // Route: a HighestEndingAddress under the HIGH pool's own floor (4GB,
    // NT_C413_HIGH_POOL_FLOOR above) means "keep me under 4GB" -- exactly
    // this floor's existing LOW-pool contract (ZeroBits!=0's own routing,
    // NtAllocateVirtualMemory below) -- reuse it verbatim by delegating
    // with an equivalent nonzero ZeroBits value (any nonzero value routes
    // LOW-only there, same as any real WOW64 ZeroBits!=0 caller already
    // does -- the exact numeric value is never inspected past nonzero).
    unsigned long effective_zero_bits =
        (in_base == 0 && have_addr_req && limit_high != 0 && limit_high < NT_C413_HIGH_POOL_FLOOR) ? 1UL : 0UL;

    long status = NtAllocateVirtualMemory(ProcessHandle, BaseAddress, effective_zero_bits,
                                           RegionSize, AllocationType, Protect);
    nt_avm_log("EXOUT", *BaseAddress, *RegionSize, AllocationType, Protect, status);
    return status;
}

// =====================================================================
// Atom B — NtQueryInformationProcess / NtQuerySystemInformation. Only the
// two classes SLICE2-SPEC calls out; everything else STATUS_NOT_IMPLEMENTED.
// =====================================================================

// PROCESS_BASIC_INFORMATION (winternl.h), LP64 layout: ExitStatus(NTSTATUS,
// 4B+4 pad)@0, PebBaseAddress@0x08, AffinityMask@0x10, BasePriority(8,
// padded to keep UniqueProcessId 8-aligned)@0x18, UniqueProcessId@0x20,
// InheritedFromUniqueProcessId@0x28 -- size 0x30 (48B, matches the
// well-known public sizeof(PROCESS_BASIC_INFORMATION) on x64/ARM64).
#define PBI_SIZE 0x30
#define PBI_PEBBASEADDRESS_OFF 0x08
#define PBI_AFFINITYMASK_OFF   0x10
#define PBI_BASEPRIORITY_OFF   0x18
#define PBI_UNIQUEPROCESSID_OFF 0x20

// CHALICE C4-6: single-process-kernel stash for ProcessWow64Information's
// answer -- the PEB32 address wow64.dll's own process_init reads back via
// NtQueryInformationProcess RIGHT AFTER RtlWow64GetProcessMachines
// succeeds (donor dlls/wow64/syscall.c:995 "NtQueryInformationProcess(
// GetCurrentProcess(), ProcessWow64Information, &peb32, sizeof(peb32),
// NULL); wow64info = (WOW64INFO *)(peb32 + 1);" -- confirmed live, pe10
// probe: an unanswered class 26 left peb32 as stack garbage, faulting on
// `wow64info->NativeSystemPageSize = ...` a few instructions later,
// wow64.dll+0x1d170). Set once by ntboot.c right after
// nt_build_wow64_teb32 succeeds (the SAME peb32 buffer that helper built).
static void *nt_current_wow64_peb32;
void nt_wow64_set_peb32(void *peb32) { nt_current_wow64_peb32 = peb32; }
// CHALICE C4-37: getter, see ntsvc.h receipt.
void *nt_wow64_get_peb32(void) { return nt_current_wow64_peb32; }

// CHALICE C4-44: single-process-kernel stash for ProcessImageInformation
// (class 37) -- the SAME donor need nt_current_wow64_peb32 above answers
// for class 26, one boot-process's worth of state. Set once by ntboot.c
// right after PeMapImage maps the main exe (real, runtime TransferAddress
// known only post-map) -- see ntfile.h for the field-shape receipt.
static PeImageInfo nt_current_main_image_info;
static u64b        nt_current_main_image_transfer;
static int         nt_current_main_image_valid;
void nt_set_main_image_info(const PeImageInfo *info, unsigned long transfer_address) {
    if (!info) { nt_current_main_image_valid = 0; return; }
    nt_current_main_image_info = *info;
    nt_current_main_image_transfer = (u64b)transfer_address;
    nt_current_main_image_valid = 1;
}

long NtQueryInformationProcess(void *ProcessHandle, unsigned long ProcessInformationClass,
                                void *ProcessInformation, unsigned long ProcessInformationLength,
                                unsigned long *ReturnLength) {
    (void)ProcessHandle;
    if (ProcessInformationClass == 26 /* ProcessWow64Information */) {
        if (!ProcessInformation) return STATUS_INVALID_PARAMETER;
        if (ProcessInformationLength < 8) return STATUS_INFO_LENGTH_MISMATCH;
        wr64((u8 *)ProcessInformation, (u64b)nt_current_wow64_peb32);
        if (ReturnLength) *ReturnLength = 8;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtQueryInformationProcess ProcessWow64Information -> peb32=");
            puthex((u64b)nt_current_wow64_peb32); puts("\n");
        }
        return STATUS_SUCCESS;
    }
    // CHALICE C4-44: ProcessImageInformation (class 37) -- donor wine/dlls/
    // ntdll/loader.c build_main_module (~2895-2906) queries this on ITS OWN
    // process right after mapping the main exe, into a LOCAL declared
    // WITHOUT `={0}` (unlike the sibling wow64/syscall.c copy), then tests
    // `info.ImageCharacteristics & IMAGE_FILE_DLL` unconditionally -- an
    // unanswered (STATUS_NOT_IMPLEMENTED, buffer untouched) call here left
    // that local as guest-stack garbage, false-positiving the DLL bit on a
    // genuine EXE (hello8_x86.exe) and self-terminating (pe10 ret=0xC000007B,
    // C4-43 frontier). Same SECTION_IMAGE_INFORMATION shape/offsets
    // NtQuerySection(SectionImageInformation) already fills (ntfile.h SII_*
    // defines, shared -- one struct, two producers: NtQuerySection answers
      // pre-map from a section's raw PE blob, this answers post-map from
      // THIS booted process's own main image, stashed by ntboot.c via
      // nt_set_main_image_info right after PeMapImage returns the real
      // (relocated) entry address). No wow64/thunking seam here: this
      // syscall is dispatched by the 32-bit guest through wow64.dll's own
      // syscall thunk (dlls/wow64/syscall.c's NtQueryInformationProcess
      // wrapper) which, for ProcessImageInformation, passes the SAME 64-bit-
      // shaped SECTION_IMAGE_INFORMATION straight through to the native
      // syscall UNTRANSLATED (donor syscall.c has no per-class thunk table
      // entry for class 37 -- unlike e.g. ProcessBasicInformation's PROCESS_
      // BASIC_INFORMATION, which .../wow64/syscall.c DOES shrink to a 32-bit
      // shape; ProcessImageInformation is passed through because build_
      // main_module calls it directly against the NATIVE ntdll, not via a
      // WOW64 guest call at all -- it runs during process init, BEFORE the
      // wow64 CPU simulation rig even starts). Confirmed against this
      // floor's own boot log (C4-43 receipt): the call is logged as plain
      // syscall id 25 with the same 0x40-byte SECTION_IMAGE_INFORMATION
      // length NtQuerySection already uses -- native shape, no thunk.
    if (ProcessInformationClass == 37 /* ProcessImageInformation */) {
        if (!ProcessInformation) return STATUS_INVALID_PARAMETER;
        if (ProcessInformationLength < SII_SIZE) return STATUS_INFO_LENGTH_MISMATCH;
        if (!nt_current_main_image_valid) return STATUS_NOT_IMPLEMENTED;

        const PeImageInfo *info = &nt_current_main_image_info;
        u8 *out = (u8 *)ProcessInformation;
        for (unsigned long i = 0; i < SII_SIZE; i++) out[i] = 0;

        wr64(out + SII_TRANSFERADDRESS_OFF, nt_current_main_image_transfer);
        wr64(out + SII_MAXSTACKSIZE_OFF, (u64b)info->stack_reserve);
        wr64(out + SII_COMMITSTACKSIZE_OFF, (u64b)info->stack_commit);
        wr32(out + SII_SUBSYSTEMTYPE_OFF, (u32b)info->subsystem);
        wr16(out + SII_MINORSUBSYS_OFF, info->subsystem_minor);
        wr16(out + SII_MAJORSUBSYS_OFF, info->subsystem_major);
        wr16(out + SII_MAJOROS_OFF, info->os_major);
        wr16(out + SII_MINOROS_OFF, info->os_minor);
        wr16(out + SII_IMAGECHAR_OFF, info->image_characteristics);
        wr16(out + SII_DLLCHAR_OFF, info->dll_characteristics);
        wr16(out + SII_MACHINE_OFF, info->machine);
        out[SII_CONTAINSCODE_OFF] = info->contains_code;
        wr32(out + SII_LOADERFLAGS_OFF, (u32b)info->loader_flags);
        wr32(out + SII_IMAGEFILESIZE_OFF, (u32b)info->file_size);

        if (ReturnLength) *ReturnLength = SII_SIZE;
        if (nt_dispatch_log_enabled) {
            puts("CHALICE: NtQueryInformationProcess ProcessImageInformation -> ImageCharacteristics=");
            puthex(info->image_characteristics); puts(" TransferAddress=");
            puthex(nt_current_main_image_transfer); puts("\n");
        }
        return STATUS_SUCCESS;
    }
    if (ProcessInformationClass != 0 /* ProcessBasicInformation */) return STATUS_NOT_IMPLEMENTED;
    if (!ProcessInformation) return STATUS_INVALID_PARAMETER;
    if (ProcessInformationLength < PBI_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    // Current TEB (x18, Windows ARM64 ABI reserved platform reg) -> PEB.
    // Single-address-space, single-"process" kernel: reading our own live
    // x18 at call time is the only "current process" we have.
    u64b teb; __asm__ volatile("mov %0, x18" : "=r"(teb));
    u64b peb = 0;
    if (teb) peb = *(u64b *)(teb + 0x60);   // TEB_PEB_OFF (A4 section below, ~line 314)

    u8 *out = (u8 *)ProcessInformation;
    for (int i = 0; i < PBI_SIZE; i++) out[i] = 0;
    wr64(out + PBI_PEBBASEADDRESS_OFF, peb);
    wr64(out + PBI_AFFINITYMASK_OFF, 1);        // single logical CPU (KUSER floor, ntsvc.c)
    wr64(out + PBI_BASEPRIORITY_OFF, 8);        // NORMAL_PRIORITY_CLASS-ish, not load-bearing
    wr64(out + PBI_UNIQUEPROCESSID_OFF, 1);     // single-process kernel: PID is always 1

    if (ReturnLength) *ReturnLength = PBI_SIZE;
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-9: NtQueryInformationThread, ThreadWow64Context (class 29)
// ONLY -- the exact next-required syscall pe10 measured this leg. Ground
// truth (disassembly, not guessed): wowbox64.dll's OWN BTCpuGetContext
// (stock/wow64-chain/wowbox64.dll+0x20e0) tail-calls straight into this
// real syscall (x1=w1=0x1d=29, x2=ContextPtr, x3=w3=0x2cc=sizeof(I386_
// CONTEXT), x4=xzr) -- confirmed live: our kernel's own "svc id=37
// UNIMPLEMENTED" line fires at exactly this call site (pe10 probe), and
// the caller (wine/dlls/wow64/syscall.c:1069 thread_init) then reads
// ctx.Esp (I386_CONTEXT offset 0xc4, wine/include/winnt.h _I386_CONTEXT)
// to compute `ctx_ptr = (I386_CONTEXT*)ctx.Esp - 1; *ctx_ptr = ctx;` --
// an unanswered call leaves ctx.Esp=0 (thread_init's own local `ctx =
// {CONTEXT_I386_FULL}` zero-initializer), so ctx_ptr underflows to
// 0-sizeof(ctx) and the very next store faults (live-confirmed: elr=
// ntdll.dll+0x68458, inside memcpy's own store loop, far=
// 0xFFFFFFFFFFFFFD34 == -0x2CC == -sizeof(I386_CONTEXT), exactly this).
// Fix: answer with the x86 guest thread's real initial Eip/Esp (stashed
// by ntboot.c right after building the wow64 TEB32, same per-boot-state
// pattern as nt_wow64_set_peb32 above) -- Esp points into a FRESH, LOW-
// territory (<4GB, so a 32-bit ESP can address it), zeroed, writable
// stack block just for this, distinct from the ARM64 boot thread's own
// stack (still live/in-use on the native call chain making THIS very
// syscall).
// CTX32_SIZE mirrors the WOW64_CPURESERVED section's own I386_CONTEXT_SIZE
// (0x2cc, defined later in this file) -- same value, distinct name here
// since that other #define lives below this point in the file (C needs it
// declared before use, and moving either section isn't worth the diff).
#define CTX32_SIZE       0x2CC
#define CTX32_EBP_OFF    0xB4
#define CTX32_EIP_OFF    0xB8
#define CTX32_SEGCS_OFF  0xBC
#define CTX32_EFLAGS_OFF 0xC0
#define CTX32_ESP_OFF    0xC4
#define CTX32_SEGSS_OFF  0xC8
// CHALICE C4-41: wine/include/winnt.h _CONTEXT (i386) field order -- Eax
// sits 4 bytes before Ebp (Edi@0x9C,Esi@0xA0,Ebx@0xA4,Edx@0xA8,Ecx@0xAC,
// Eax@0xB0,Ebp@0xB4 -- matches this file's own EBP_OFF=0xB4 exactly, one
// ULONG per field, no gap).
#define CTX32_EAX_OFF    0xB0
// Real WOW64 flat usermode selectors (donor-known constants, x86 Windows
// ABI -- CS=0x1B, SS=0x23 under the flat 32-bit code/data/stack segments
// WOW64 sets up for every 32-bit thread); EFlags=0x202 (IF set + the
// always-1 reserved bit 1) is the standard initial-thread default.
#define WOW64_SEGCS 0x1BUL
#define WOW64_SEGSS 0x23UL
#define WOW64_EFLAGS 0x202UL

static u64b nt_wow64_ctx_eip, nt_wow64_ctx_esp;
static int  nt_wow64_ctx_valid;

// Called by ntboot.c once per wow64 boot, right after nt_build_wow64_teb32
// succeeds -- same call-site convention as nt_wow64_set_peb32 just above.
void nt_wow64_set_initial_context(unsigned long eip, unsigned long esp) {
    nt_wow64_ctx_eip = eip;
    nt_wow64_ctx_esp = esp;
    nt_wow64_ctx_valid = 1;
}

// CHALICE C4-11: see ntsvc.h receipt -- WOW64_CPURESERVED layout (this
// file's own WOW64_CPURESERVED_HDR_SIZE/CTX32_* offsets above): Flags(2)+
// Machine(2) header, then an inline I386_CONTEXT starting at +4. Same
// field values NtQueryInformationThread(ThreadWow64Context) already
// answers with (Ebp==Esp empty-frame convention, WOW64_SEGCS/SEGSS/
// EFLAGS constants above) -- one real write, not a guess: this is the
// exact struct native_pass0 dereferences directly (measured live, no
// intervening syscall between TEB build and the fault).
void nt_wow64_cpureserved_set_context(void *cpureserved_v, unsigned long eip, unsigned long esp) {
    u8 *cr = (u8 *)cpureserved_v;
    if (!cr) return;
    nt_wow64_cpureserved_dbg = cr;   // CHALICE C4-25: stash for NtRaiseException's RAISE#1 guest-Eip dump
    // literal 4 == WOW64_CPURESERVED_HDR_SIZE (Flags+Machine, defined later
    // in this file where nt_build_wow64_teb32 lives -- can't forward-
    // reference a #define, so the value is duplicated here, not the meaning).
    u8 *ctx = cr + 4;
    wr32(ctx + CTX32_EBP_OFF, (u32b)esp);
    wr32(ctx + CTX32_EIP_OFF, (u32b)eip);
    wr32(ctx + CTX32_SEGCS_OFF, (u32b)WOW64_SEGCS);
    wr32(ctx + CTX32_EFLAGS_OFF, (u32b)WOW64_EFLAGS);
    wr32(ctx + CTX32_ESP_OFF, (u32b)esp);
    wr32(ctx + CTX32_SEGSS_OFF, (u32b)WOW64_SEGSS);
}

long NtQueryInformationThread(void *ThreadHandle, unsigned long ThreadInformationClass,
                               void *ThreadInformation, unsigned long ThreadInformationLength,
                               unsigned long *ReturnLength) {
    (void)ThreadHandle;
    // CHALICE C4-15: FEX GetThreadTLS asks ThreadBasicInformation solely for
    // TebBaseAddress (pinned donor WOW64/Module.cpp:141-145). One live thread
    // model -> current x18 TEB + stable synthetic ClientId; real Win64 layout:
    // ExitStatus@0, TebBaseAddress@8, CLIENT_ID@0x10, total 0x30.
    if (ThreadInformationClass == 0 /* ThreadBasicInformation */) {
        if (!ThreadInformation) return STATUS_INVALID_PARAMETER;
        if (ThreadInformationLength < 0x30) return STATUS_INFO_LENGTH_MISMATCH;
        u8 *out = (u8 *)ThreadInformation;
        for (int i = 0; i < 0x30; i++) out[i] = 0;
        u64b teb; __asm__ volatile("mov %0, x18" : "=r"(teb));
        wr64(out + 0x08, teb);
        wr64(out + 0x10, 1); // ClientId.UniqueProcess
        wr64(out + 0x18, 1); // ClientId.UniqueThread
        if (ReturnLength) *ReturnLength = 0x30;
        return STATUS_SUCCESS;
    }
    if (ThreadInformationClass != 29 /* ThreadWow64Context */) return STATUS_NOT_IMPLEMENTED;
    if (!ThreadInformation) return STATUS_INVALID_PARAMETER;
    if (ThreadInformationLength < CTX32_SIZE) return STATUS_INFO_LENGTH_MISMATCH;
    if (!nt_wow64_ctx_valid) return STATUS_NOT_IMPLEMENTED;   // non-wow64 boot -- nothing to answer with

    u8 *out = (u8 *)ThreadInformation;
    for (unsigned long i = 0; i < CTX32_SIZE; i++) out[i] = 0;
    wr32(out + CTX32_EBP_OFF, (u32b)nt_wow64_ctx_esp);   // Ebp==Esp, empty-frame convention (no caller frame yet)
    wr32(out + CTX32_EIP_OFF, (u32b)nt_wow64_ctx_eip);
    wr32(out + CTX32_SEGCS_OFF, (u32b)WOW64_SEGCS);
    wr32(out + CTX32_EFLAGS_OFF, (u32b)WOW64_EFLAGS);
    wr32(out + CTX32_ESP_OFF, (u32b)nt_wow64_ctx_esp);
    wr32(out + CTX32_SEGSS_OFF, (u32b)WOW64_SEGSS);

    if (ReturnLength) *ReturnLength = CTX32_SIZE;
    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtQueryInformationThread ThreadWow64Context -> eip=");
        puthex(nt_wow64_ctx_eip); puts(" esp="); puthex(nt_wow64_ctx_esp); puts("\n");
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-11: NtSetInformationThread, ThreadWow64Context (class 29)
// ONLY -- measured live (pe10 probe): svc id=13, wine-arm64-syscalls.txt:
// 210, fires with class=0x1D(29)/len=0x2CC(=CTX32_SIZE) right after our
// existing NtQueryInformationThread(ThreadWow64Context) answers eip/esp --
// the documented real-Windows SET counterpart of that same class (Nt{Query,
// Set}InformationThread ThreadWow64Context get/set a thread's WOW64
// CONTEXT; this is a standard NT contract, not Wine/Hangover-specific --
// no donor grounding needed beyond the same I386_CONTEXT field offsets
// this file already uses for the GET side, CTX32_EIP_OFF/CTX32_ESP_OFF
// above). BTCpuThreadInit/BTCpuProcessInit calls this to install the
// (possibly backend-adjusted) x86 context as the thread's authoritative
// WOW64 context before continuing -- our previously-silent NtStub->
// STATUS_NOT_IMPLEMENTED left the caller believing the write failed/no-
// oped and it dereferenced forward assuming success (wowbox64.dll+0xF88BC
// Data Abort, far=0x2, immediately downstream of this exact call site).
// Fix: parse Eip/Esp back OUT of the incoming buffer into the same
// nt_wow64_ctx_eip/esp stash nt_wow64_set_initial_context populates, so a
// later GET reflects whatever the backend just SET (single-thread kernel,
// one stash, same simplification the rest of this WOW64 floor uses) --
// then STATUS_SUCCESS.
// =====================================================================
long NtSetInformationThread(void *ThreadHandle, unsigned long ThreadInformationClass,
                             const void *ThreadInformation, unsigned long ThreadInformationLength) {
    (void)ThreadHandle;
    if (ThreadInformationClass != 29 /* ThreadWow64Context */) return STATUS_NOT_IMPLEMENTED;
    if (!ThreadInformation) return STATUS_INVALID_PARAMETER;
    if (ThreadInformationLength < CTX32_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    const u8 *in = (const u8 *)ThreadInformation;
    nt_wow64_ctx_eip = rd32(in + CTX32_EIP_OFF);
    nt_wow64_ctx_esp = rd32(in + CTX32_ESP_OFF);
    nt_wow64_ctx_valid = 1;

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtSetInformationThread ThreadWow64Context <- eip=");
        puthex(nt_wow64_ctx_eip); puts(" esp="); puthex(nt_wow64_ctx_esp); puts("\n");
    }

    // CHALICE C4-41: this SET call is exactly how FEX reflects the guest
    // 32-bit CPU state at the moment of the C4-40-named 7th-fault NULL
    // IMAGE_DOS_SIGNATURE check (syswow64/ntdll.dll+0x2B34E, inside
    // _RtlImageNtHeader@4, RVA 0x2B310-0x2B37F, donor stock/i386-windows/
    // ntdll.dll disasm this atom) -- catch it the FIRST time eip lands on
    // that exact RVA and dump the live incoming CONTEXT's Eax (the NULL
    // BaseAddress argument RtlImageNtHeader received) + Ebp (frame
    // pointer), then walk the GUEST stack directly (this floor's one-
    // address-space model, C4-35 -- guest memory is host-dereferenceable)
    // to read [Ebp+4]=return address (the caller, i.e. who pushed the NULL
    // pointer) and [Ebp+8]=the argument slot itself (should mirror Eax).
    // No control-flow change -- diagnostic only, same doctrine as C4-38's
    // PTE dump.
    {
        static int c4_41_dumped;
        u64b ntdll_base = 0, ntdll_size = 0;
        if (!c4_41_dumped &&
            nt_image_base_by_name("syswow64/ntdll.dll", &ntdll_base, &ntdll_size) &&
            nt_wow64_ctx_eip == ntdll_base + 0x2B34EUL) {
            c4_41_dumped = 1;
            u32b eax = rd32(in + CTX32_EAX_OFF);
            u32b ebp = rd32(in + CTX32_EBP_OFF);
            u32b ret_addr = 0, arg0 = 0;
            const u8 *ebp_p = (const u8 *)(u64b)ebp;
            if (ebp) { ret_addr = rd32(ebp_p + 4); arg0 = rd32(ebp_p + 8); }
            puts("CHALICE C4-41: fault-site eip=ntdll.dll+0x2B34E (RtlImageNtHeader cmpw MZ) eax(BaseAddress)=");
            puthex(eax); puts(" ebp="); puthex(ebp);
            puts(" [ebp+4](retaddr)="); puthex(ret_addr);
            puts(" [ebp+8](arg0)="); puthex(arg0); puts("\n");
            char name[64]; u64b off = 0;
            if (ret_addr && nt_image_resolve(ret_addr, name, sizeof(name), &off)) {
                puts("CHALICE C4-41: caller = "); puts(name); puts("+"); puthex(off); puts("\n");
            } else {
                puts("CHALICE C4-41: caller unresolved (not in any mapped image)\n");
            }
            void *peb32 = nt_wow64_get_peb32();
            if (peb32) {
                u32b imagebase32 = rd32((const u8 *)peb32 + 0x08);
                puts("CHALICE C4-41: live PEB32.ImageBaseAddress="); puthex(imagebase32); puts("\n");
            }
            // CHALICE C4-41 (2nd pass): walk ONE more frame up -- ebp here is
            // RtlImageNtHeader's OWN frame ptr; [ebp+0] is the SAVED ebp,
            // i.e. alloc_module's frame ptr, so [[ebp+0]+4] is alloc_module's
            // OWN return address = whoever called alloc_module (disambiguates
            // the two static call sites this atom's disasm found: loader_init
            // main-image bootstrap vs build_module's DLL-dependency walk).
            u32b alloc_module_ebp = rd32(ebp_p + 0);
            u32b alloc_module_retaddr = 0;
            if (alloc_module_ebp) alloc_module_retaddr = rd32((const u8 *)(u64b)alloc_module_ebp + 4);
            puts("CHALICE C4-41: alloc_module ebp="); puthex(alloc_module_ebp);
            puts(" alloc_module's own retaddr="); puthex(alloc_module_retaddr);
            char name2[64]; u64b off2 = 0;
            if (alloc_module_retaddr && nt_image_resolve(alloc_module_retaddr, name2, sizeof(name2), &off2)) {
                puts(" -> "); puts(name2); puts("+"); puthex(off2);
            }
            puts("\n");
        }
    }
    return STATUS_SUCCESS;
}

// SYSTEM_BASIC_INFORMATION (ntsvc.c receipt: reactos/sdk/include/ndk/
// extypes.h _SYSTEM_BASIC_INFORMATION), LP64 layout: Reserved(4)@0,
// TimerResolution(4)@4, PageSize(4)@8, NumberOfPhysicalPages(4)@0xC,
// LowestPhysicalPageNumber(4)@0x10, HighestPhysicalPageNumber(4)@0x14,
// AllocationGranularity(4)@0x18 + 4 pad, MinimumUserModeAddress(8)@0x20,
// MaximumUserModeAddress(8)@0x28, ActiveProcessorsAffinityMask(8)@0x30,
// NumberOfProcessors(1)@0x38 -- size 0x40 (64B).
#define SBI_SIZE 0x40
#define SBI_PAGESIZE_OFF 0x08
#define SBI_NUMBEROFPHYSICALPAGES_OFF 0x0C
#define SBI_MINUSERADDR_OFF 0x20
#define SBI_MAXUSERADDR_OFF 0x28
#define SBI_NUMBEROFPROCESSORS_OFF 0x38

// CHALICE C4-42: the zero field feeding alloc_module, traced end to end.
// wow64.dll's own process_init (donor wine/dlls/wow64/syscall.c:996-997)
// calls NtQuerySystemInformation(SystemEmulationBasicInformation=62, &info,
// sizeof(info), NULL) then UNCONDITIONALLY does
// `highest_user_address = info.HighestUserAddress;` -- NO status check.
// This floor's NtQuerySystemInformation only accepted class 0
// (SystemBasicInformation), returning STATUS_NOT_IMPLEMENTED for class 62
// -- info stayed untouched (host stack garbage/zero), so
// wow64.dll's global `highest_user_address` (default 0x7ffeffff at
// link time, syscall.c:40) got overwritten with a bogus ~0 value. Every
// subsequent WOW64 NtQueryVirtualMemory(MemoryBasicInformation) call from
// 32-bit guest code -- including build_ntdll_module's own
// NtQueryVirtualMemory(-1, LdrInitializeThunk, MemoryBasicInformation, buf,
// 0x1c, NULL), C4-41/C4-42's own disasm-traced call site -- then fails
// wow64_NtQueryVirtualMemory's own `(ULONG_PTR)addr > highest_user_address`
// gate (wine/dlls/wow64/virtual.c:585) and returns STATUS_INVALID_PARAMETER
// WITHOUT EVER calling the native NtQueryVirtualMemory this floor
// implements below (proof: zero NtQueryVirtualMemory ENTRY log lines this
// boot ever show BaseAddress==LdrInitializeThunk, out of 61 total calls --
// every one of the 61 is a HOST-side/FEX-internal probe, never a genuine
// guest WOW64 VirtualQuery). loader_init's own build_ntdll_module does NOT
// check that call's return status either (donor source, quoted HANDOFF
// C4-42) -- it reads `meminfo.AllocationBase` regardless, which stayed at
// its guest-stack zero-init value, feeding ecx=0 into alloc_module ->
// RtlImageNtHeader(0) -> the NULL 'MZ' check AV C4-40 first named.
// Donor-exact fix (wine/dlls/ntdll/unix/virtual.c:7249-7254): real Wine's
// own unix NtQuerySystemInformation treats SystemEmulationBasicInformation
// (62) as a PURE ALIAS of SystemBasicInformation (0), same struct, same
// values -- `case SystemBasicInformation: case SystemEmulationBasicInformation:
// ... return NtQuerySystemInformation(SystemBasicInformation, info, len, retlen);`.
// Also fills the two fields this floor's writer never touched
// (HighestUserAddress@0x28, the load-bearing one; LowestUserAddress@0x20,
// same struct, donor-conventional 0x10000 null-page reservation) --
// HighestUserAddress must be large enough to admit every real 32-bit guest
// address (images/stack observed up to ~0x50030000 this boot); reuses
// wow64.dll's OWN static pre-process_init default (0x7ffeffff,
// syscall.c:40) -- the exact value a real Windows 32-bit process reports,
// not a made-up constant.
#define SBI_LOWUSERADDR_VALUE  0x10000ULL
#define SBI_HIGHUSERADDR_VALUE 0x7ffeffffULL

long NtQuerySystemInformation(unsigned long SystemInformationClass, void *SystemInformation,
                               unsigned long SystemInformationLength, unsigned long *ReturnLength) {
    if (SystemInformationClass != 0 /* SystemBasicInformation */ &&
        SystemInformationClass != 62 /* SystemEmulationBasicInformation -- donor alias, C4-42 */)
        return STATUS_NOT_IMPLEMENTED;
    if (!SystemInformation) return STATUS_INVALID_PARAMETER;
    if (SystemInformationLength < SBI_SIZE) return STATUS_INFO_LENGTH_MISMATCH;

    u8 *out = (u8 *)SystemInformation;
    for (int i = 0; i < SBI_SIZE; i++) out[i] = 0;
    wr32(out + SBI_PAGESIZE_OFF, (u32b)NT_PAGE_SIZE);
    wr32(out + SBI_NUMBEROFPHYSICALPAGES_OFF, NT_KUSER_NUM_PHYS_PAGES);
    wr64(out + SBI_MINUSERADDR_OFF, SBI_LOWUSERADDR_VALUE);
    wr64(out + SBI_MAXUSERADDR_OFF, SBI_HIGHUSERADDR_VALUE);
    out[SBI_NUMBEROFPROCESSORS_OFF] = 1;

    if (ReturnLength) *ReturnLength = SBI_SIZE;
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-6: NtQuerySystemInformationEx, ONE class only --
// SystemSupportedProcessorArchitectures(181)/...Architectures2(230) --
// the call RtlWow64GetProcessMachines (donor wine/dlls/ntdll/process.c:
// 107-124) makes to learn current/native machine, which loader_init's
// init_wow64 path (via wow64.dll's own process_init, dlls/wow64/
// syscall.c:984) calls FIRST, before touching anything else -- observed
// live (pe10 probe) as the exact next required syscall (id 203, wine-
// arm64-syscalls.txt:205) once wow64.dll itself is mapped+relocated.
// Struct (winnt.h _SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES_INFORMATION):
// ONE bitfield DWORD per entry -- Machine:16, KernelMode:1, UserMode:1,
// Native:1, Process:1, WoW64Container:1, ReservedZero0:11 -- array is
// Machine==0-terminated (donor system.c:4176 `for (i=0;machines[i].
// Machine;i++)`). This floor is single-arch-pair only (this C4 leg):
// entry0 = native (ARM64, 0xAA64), entry1 = the guest/current machine
// (0x14C i386, Process=1), entry2 = terminator. `query` is a HANDLE (the
// process to report Process=1 for) -- unused here (single-process kernel,
// same simplification NtQueryInformationProcess above already takes: the
// live x18/TEB IS "the current process", there is no other to compare
// against), so the guest/current entry is unconditionally Process=1.
// =====================================================================
#define SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES 181
#define SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES2 230

long NtQuerySystemInformationEx(unsigned long SystemInformationClass, void *Query, unsigned long QueryLength,
                                 void *SystemInformation, unsigned long SystemInformationLength,
                                 unsigned long *ReturnLength) {
    (void)Query; (void)QueryLength;
    if (SystemInformationClass != SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES &&
        SystemInformationClass != SYSTEM_SUPPORTED_PROCESSOR_ARCHITECTURES2)
        return STATUS_NOT_IMPLEMENTED;

    u32b need = 3 * 4;   // 2 real entries + 1 Machine==0 terminator, 4B each
    if (ReturnLength) *ReturnLength = need;
    if (!SystemInformation) return STATUS_INVALID_PARAMETER;
    if (SystemInformationLength < need) return STATUS_INFO_LENGTH_MISMATCH;   // no STATUS_BUFFER_TOO_SMALL constant this floor, same-shape "too small" signal

    u8 *out = (u8 *)SystemInformation;
    // entry0: native == ARM64, this floor's own host arch.
    wr32(out + 0, 0xAA64UL | (1UL << 16) /* KernelMode */ | (1UL << 17) /* UserMode */ |
                  (1UL << 18) /* Native */);
    // entry1: current/guest == i386, always Process=1 (single-process kernel).
    wr32(out + 4, 0x14CUL | (1UL << 17) /* UserMode */ | (1UL << 19) /* Process */ |
                  (1UL << 20) /* WoW64Container */);
    wr32(out + 8, 0);   // terminator

    if (nt_dispatch_log_enabled) {
        puts("CHALICE: NtQuerySystemInformationEx class="); puthex((u64b)SystemInformationClass);
        puts(" -> native=0xAA64 current=0x14C\n");
    }
    return STATUS_SUCCESS;
}

// =====================================================================
// CHALICE C4-16: real ARM64 CPU-register reads. This kernel runs at EL1
// (kmain.c mmu_on) -- ID_AA64*/MIDR/CTR reads are unconditionally legal
// there (ARM ARM D17.2: EL1 access is trapped to EL2 only if a hypervisor
// sets HCR_EL2.TID3, and this floor never installs an EL2 -- we ARE the
// only privilege level below EL1's own guest, same "we ARE the unix side"
// framing nt_kuser_init already uses). Direct mrs, not the Linux-side
// trap+emulate dance donor wine/dlls/ntdll/unix/system.c's
// get_core_id_regs_arm64 needs (that code reads via getauxval(HWCAP_CPUID)
// + an emulated mrs trap because IT runs at EL0 under a real Linux kernel;
// we have no such intermediary to trap through, so we skip that whole
// mechanism and read the real hardware register directly).
//
// Windows' CentralProcessor value name "CP xxxx" packs an AArch64 sysreg
// S<op0>_<op1>_C<CRn>_C<CRm>_<op2> as:
//   ((op0 - 2) << 14) | (op1 << 11) | (CRn << 7) | (CRm << 3) | op2.
// Derivation: CP 4030 = S3_0_C0_C6_0 (ID_AA64ISAR0_EL1): 0x4000 + 6<<3;
// CP 5801 = S3_3_C0_C0_1 (CTR_EL0): 0x4000 + 3<<11 + 1.  The complete
// table below is therefore encoded from architectural register names, not
// an unverified CP-number list. NtQueryValueKey exposes each raw mrs value.
// =====================================================================
#define NT_READ_SYSREG(name, reg) \
    unsigned long name(void) { unsigned long v; __asm__ volatile("mrs %0, " #reg : "=r"(v)); return v; }

NT_READ_SYSREG(nt_read_midr_el1, midr_el1)
NT_READ_SYSREG(nt_read_id_aa64pfr0, id_aa64pfr0_el1)
NT_READ_SYSREG(nt_read_id_aa64pfr1, id_aa64pfr1_el1)
// Raw architectural spelling: the default freestanding assembler target lacks
// +sve and rejects its friendly ID_AA64ZFR0_EL1 alias; S3_0_C0_C4_4 is the
// identical encoding and remains readable even when the register reports 0.
NT_READ_SYSREG(nt_read_id_aa64zfr0, S3_0_C0_C4_4)
NT_READ_SYSREG(nt_read_id_aa64isar0, id_aa64isar0_el1)
NT_READ_SYSREG(nt_read_id_aa64isar1, id_aa64isar1_el1)
NT_READ_SYSREG(nt_read_id_aa64isar2, id_aa64isar2_el1)
NT_READ_SYSREG(nt_read_id_aa64mmfr0, id_aa64mmfr0_el1)
NT_READ_SYSREG(nt_read_id_aa64mmfr1, id_aa64mmfr1_el1)
NT_READ_SYSREG(nt_read_id_aa64mmfr2, id_aa64mmfr2_el1)
NT_READ_SYSREG(nt_read_ctr_el0, ctr_el0)

#undef NT_READ_SYSREG

// =====================================================================
// Atom B — KUSER_SHARED_DATA @ NT_KUSER_SHARED_DATA_ADDR (fixed physical
// address, we populate directly — HANDOFF: "we ARE the unix side").
// =====================================================================
void nt_kuser_init(void) {
    u8 *k = (u8 *)(u64b)NT_KUSER_SHARED_DATA_ADDR;
    // Zero the page first: QEMU RAM content at an address never previously
    // written is otherwise unspecified, and only a handful of fields below
    // are populated -- the rest of KUSER_SHARED_DATA must read as zero, not
    // whatever pattern happened to be in DRAM.
    for (u64b i = 0; i < NT_PAGE_SIZE; i++) k[i] = 0;

    wr32(k + NT_KUSER_NTBUILDNUMBER_OFF, 19041);   // arbitrary Win10 build, not load-bearing
    wr32(k + NT_KUSER_NTMAJORVERSION_OFF, 10);
    wr32(k + NT_KUSER_NTMINORVERSION_OFF, 0);
    wr32(k + NT_KUSER_NUMBEROFPHYSICALPAGES_OFF, NT_KUSER_NUM_PHYS_PAGES);

    // ProcessorFeatures[]: two tiers, per donor dlls/ntdll/unix/system.c
    // init_shared_data_cpuinfo (fetched this leg, non-__arm__/aarch64
    // branch) --
    // (1) unconditional-TRUE-on-any-AArch64-host facts (NOT host-specific,
    //     so no mrs read backs these -- matches donor exactly, every
    //     AArch64 CPU real Windows-on-ARM64 ever ran on has them):
    k[NT_KUSER_PROCESSORFEATURES_OFF + 18] = 1;   // PF_ARM_VFP_32_REGISTERS_AVAILABLE
    k[NT_KUSER_PROCESSORFEATURES_OFF + 19] = 1;   // PF_ARM_NEON_INSTRUCTIONS_AVAILABLE
    k[NT_KUSER_PROCESSORFEATURES_OFF + 25] = 1;   // PF_ARM_64BIT_LOADSTORE_ATOMIC
    k[NT_KUSER_PROCESSORFEATURES_OFF + 29] = 1;   // PF_ARM_V8_INSTRUCTIONS_AVAILABLE

    // (2) host-SPECIFIC facts -- CHALICE C4-13: derived from a real
    // ID_AA64ISAR0_EL1 mrs read (nt_read_id_aa64isar0 above), IRON law "no
    // hardcoded feature lists, parameterize nothing by hand" -- mapping
    // table is the ARM ARM's ID_AA64ISAR0_EL1 field layout (also exactly
    // the 3 PF_* box64-for-WOW64's DetectHostCpuFeatures actually queries
    // via IsProcessorFeaturePresent, box64/src/os/hostext_wine.c:8, v0.4.2
    // donor in-tree gitignored quarry -- SHA1/SHA2/PMULL/RDM/SHA3/SM3/SM4/
    // DP/FHM/TS/TLB/RNDR fields exist in the same register but box64 never
    // asks their PF_* counterparts, so this floor doesn't derive them
    // either -- exactly the surface consumed, no more, no less):
    //   AES    bits[7:4]   nonzero -> PF_ARM_V8_CRYPTO_INSTRUCTIONS_AVAILABLE(30)
    //   CRC32  bits[19:16] nonzero -> PF_ARM_V8_CRC32_INSTRUCTIONS_AVAILABLE(31)
    //   Atomic bits[23:20] nonzero -> PF_ARM_V81_ATOMIC_INSTRUCTIONS_AVAILABLE(34)
    {
        u64b isar0 = nt_read_id_aa64isar0();
        if (((isar0 >> 4)  & 0xFUL) != 0) k[NT_KUSER_PROCESSORFEATURES_OFF + 30] = 1; // AES
        if (((isar0 >> 16) & 0xFUL) != 0) k[NT_KUSER_PROCESSORFEATURES_OFF + 31] = 1; // CRC32
        if (((isar0 >> 20) & 0xFUL) != 0) k[NT_KUSER_PROCESSORFEATURES_OFF + 34] = 1; // Atomic/LSE
    }
}

// =====================================================================
// CHALICE C3c Atom 3C-1 — dispatcher call-log (removable-later debug aid,
// see ntsvc.h). Runtime-flag-gated (default OFF); shell.c's pe6 verb turns
// it on only around nt_boot_process so earlier gates (pe1-pe5, zc, aot,
// ...) stay quiet. Called from ntdisp.S's nt_dispatch on every service
// entry (`bl nt_dispatch_log`, both the resolved-handler path and the
// bad_service/out-of-range path).
// =====================================================================
int nt_dispatch_log_enabled;
static u64b nt_dispatch_log_count;
#define NT_DISPATCH_LOG_FIRST_N 64

static const char *nt_svc_name(u64b id) {
    switch (id) {
    case NT_SVC_NTALLOCATEVIRTUALMEMORY:    return "NtAllocateVirtualMemory";
    case NT_SVC_NTALLOCATEVIRTUALMEMORYEX:  return "NtAllocateVirtualMemoryEx";
    case NT_SVC_NTFREEVIRTUALMEMORY:        return "NtFreeVirtualMemory";
    case NT_SVC_NTFLUSHINSTRUCTIONCACHE:    return "NtFlushInstructionCache";
    case NT_SVC_NTPROTECTVIRTUALMEMORY:     return "NtProtectVirtualMemory";
    case NT_SVC_NTCREATESECTION:            return "NtCreateSection";
    case NT_SVC_NTMAPVIEWOFSECTION:         return "NtMapViewOfSection";
    case NT_SVC_NTCREATEFILE:               return "NtCreateFile";
    case NT_SVC_NTOPENFILE:                 return "NtOpenFile";
    case NT_SVC_NTREADFILE:                 return "NtReadFile";
    case NT_SVC_NTWRITEFILE:                return "NtWriteFile";
    case NT_SVC_NTCLOSE:                    return "NtClose";
    case NT_SVC_NTQUERYOBJECT:              return "NtQueryObject";
    case NT_SVC_NTDUPLICATEOBJECT:          return "NtDuplicateObject";
    case NT_SVC_NTCREATETHREAD:             return "NtCreateThread";
    case NT_SVC_NTRESUMETHREAD:             return "NtResumeThread";
    case NT_SVC_NTTERMINATETHREAD:          return "NtTerminateThread";
    case NT_SVC_NTTERMINATEPROCESS:         return "NtTerminateProcess";
    case NT_SVC_NTWAITFORSINGLEOBJECT:      return "NtWaitForSingleObject";
    case NT_SVC_NTCREATEEVENT:              return "NtCreateEvent";
    case NT_SVC_NTQUERYINFORMATIONPROCESS:  return "NtQueryInformationProcess";
    case NT_SVC_NTQUERYINFORMATIONTHREAD:   return "NtQueryInformationThread";
    case NT_SVC_NTSETINFORMATIONTHREAD:      return "NtSetInformationThread";
    case NT_SVC_NTQUERYSYSTEMINFORMATION:   return "NtQuerySystemInformation";
    case NT_SVC_NTQUERYSYSTEMINFORMATIONEX: return "NtQuerySystemInformationEx";
    case NT_SVC_NTCONTINUE:                 return "NtContinue";
    case NT_SVC_NTCREATEKEY:                return "NtCreateKey";
    case NT_SVC_NTOPENKEY:                  return "NtOpenKey";
    case NT_SVC_NTOPENKEYEX:                return "NtOpenKeyEx";
    case NT_SVC_NTQUERYVALUEKEY:            return "NtQueryValueKey";
    case NT_SVC_NTQUERYVIRTUALMEMORY:       return "NtQueryVirtualMemory";
    case NT_SVC_NTQUERYSECTION:              return "NtQuerySection";
    case NT_SVC_NTRAISEEXCEPTION:            return "NtRaiseException";
    case NT_SVC_NTRAISEHARDERROR:            return "NtRaiseHardError";
    case NT_SVC_NTINITIALIZENLSFILES:        return "NtInitializeNlsFiles";
    case NT_SVC_NTGETNLSSECTIONPTR:           return "NtGetNlsSectionPtr";
    case NT_SVC_NTSETVALUEKEY:                return "NtSetValueKey";
    case NT_SVC_NTQUERYDEFAULTLOCALE:          return "NtQueryDefaultLocale";
    default: return 0;
    }
}

static void nt_log_putdec(u64b v) {
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + (char)(v % 10); v /= 10; } while (v);
    puts(&b[i]);
}

void nt_dispatch_log_reset(void) { nt_dispatch_log_count = 0; }

void nt_dispatch_log(unsigned long id_arg, void *handler) {
    u64b id = (u64b)id_arg;
    volatile u8 *ctx = nt_exception_context_watch;
    // C4-21 → every floor re-entry after exception delivery; brackets owner.
    if (ctx) {
        puts("CHALICE CONTEXT AT SVC: id="); nt_log_putdec(id);
        puts(" flags="); puthex(rd32((const u8 *)ctx + 0));
        puts(" pc="); puthex(rd64((const u8 *)ctx + 0x108));
        puts(" sp="); puthex(rd64((const u8 *)ctx + 0x100)); puts("\n");
    }
    if (!nt_dispatch_log_enabled) return;
    nt_dispatch_log_count++;
    int unknown = (handler == 0);
    int unimpl  = (!unknown && handler == (void *)NtStub);
    if (!unknown && !unimpl && nt_dispatch_log_count > NT_DISPATCH_LOG_FIRST_N) return;

    puts("CHALICE: svc id="); nt_log_putdec(id);
    const char *name = unknown ? 0 : nt_svc_name(id);
    if (name) { puts(" ("); puts(name); puts(")"); }
    if (unknown) puts(" UNKNOWN");
    else if (unimpl) puts(" UNIMPLEMENTED");
    puts("\n");
}

static void  *nt_svc_table0[NT_SERVICE_COUNT];
static u8     nt_svc_argtable0[NT_SERVICE_COUNT];
NtServiceTable nt_service_tables[4];

static int nt_svc_inited;

void nt_service_tables_init(void) {
    if (nt_svc_inited) return;
    nt_svc_inited = 1;
    for (int i = 0; i < NT_SERVICE_COUNT; i++) {
        nt_svc_table0[i] = (void *)NtStub;
        nt_svc_argtable0[i] = 0;             // register-only, all ignored by NtStub
    }
    nt_svc_table0[NT_SVC_NTWRITEFILE]        = (void *)NtWriteFile;
    nt_svc_argtable0[NT_SVC_NTWRITEFILE]     = 9 * 8;   // 9 args (HANDOFF/A2 receipt)
    nt_svc_table0[NT_SVC_NTCLOSE]            = (void *)NtClose;
    nt_svc_argtable0[NT_SVC_NTCLOSE]         = 1 * 8;
    nt_svc_table0[NT_SVC_NTQUERYOBJECT]      = (void *)NtQueryObject;
    nt_svc_argtable0[NT_SVC_NTQUERYOBJECT]   = 5 * 8;
    nt_svc_table0[NT_SVC_NTDUPLICATEOBJECT]  = (void *)NtDuplicateObject;
    nt_svc_argtable0[NT_SVC_NTDUPLICATEOBJECT] = 7 * 8;
    nt_svc_table0[NT_SVC_NTTERMINATEPROCESS] = (void *)NtTerminateProcess;
    nt_svc_argtable0[NT_SVC_NTTERMINATEPROCESS] = 2 * 8;

    // ---- Atom B real handlers ----
    nt_svc_table0[NT_SVC_NTALLOCATEVIRTUALMEMORY] = (void *)NtAllocateVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTALLOCATEVIRTUALMEMORY] = 6 * 8;
    // CHALICE C4-13: NtAllocateVirtualMemoryEx(ProcessHandle, BaseAddress,
    // RegionSize, AllocationType, Protect, Parameters, ParameterCount) -- 7 args.
    nt_svc_table0[NT_SVC_NTALLOCATEVIRTUALMEMORYEX] = (void *)NtAllocateVirtualMemoryEx;
    nt_svc_argtable0[NT_SVC_NTALLOCATEVIRTUALMEMORYEX] = 7 * 8;
    nt_svc_table0[NT_SVC_NTFREEVIRTUALMEMORY] = (void *)NtFreeVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTFREEVIRTUALMEMORY] = 4 * 8;
    nt_svc_table0[NT_SVC_NTFLUSHINSTRUCTIONCACHE] = (void *)NtFlushInstructionCache;
    nt_svc_argtable0[NT_SVC_NTFLUSHINSTRUCTIONCACHE] = 3 * 8;
    nt_svc_table0[NT_SVC_NTQUERYINFORMATIONTHREAD] = (void *)NtQueryInformationThread;
    nt_svc_table0[NT_SVC_NTQUERYINFORMATIONPROCESS] = (void *)NtQueryInformationProcess;
    nt_svc_argtable0[NT_SVC_NTQUERYINFORMATIONPROCESS] = 5 * 8;
    nt_svc_argtable0[NT_SVC_NTQUERYINFORMATIONTHREAD] = 5 * 8;
    // CHALICE C4-11: NtSetInformationThread(ThreadHandle, ThreadInformationClass,
    // ThreadInformation, ThreadInformationLength) -- 4 args.
    nt_svc_table0[NT_SVC_NTSETINFORMATIONTHREAD] = (void *)NtSetInformationThread;
    nt_svc_argtable0[NT_SVC_NTSETINFORMATIONTHREAD] = 4 * 8;
    nt_svc_table0[NT_SVC_NTQUERYSYSTEMINFORMATION] = (void *)NtQuerySystemInformation;
    nt_svc_argtable0[NT_SVC_NTQUERYSYSTEMINFORMATION] = 4 * 8;
    // CHALICE C4-6: NtQuerySystemInformationEx(class, query, query_len,
    // info, size, ret_size) -- 6 args.
    nt_svc_table0[NT_SVC_NTQUERYSYSTEMINFORMATIONEX] = (void *)NtQuerySystemInformationEx;
    nt_svc_argtable0[NT_SVC_NTQUERYSYSTEMINFORMATIONEX] = 6 * 8;
    nt_svc_table0[NT_SVC_NTCONTINUE] = (void *)NtContinue;
    nt_svc_argtable0[NT_SVC_NTCONTINUE] = 2 * 8;

    // ---- CHALICE C3a Atom 3A: loader-path services (ntfile.c) ----
    nt_svc_table0[NT_SVC_NTOPENFILE] = (void *)NtOpenFile;
    nt_svc_argtable0[NT_SVC_NTOPENFILE] = 6 * 8;
    nt_svc_table0[NT_SVC_NTREADFILE] = (void *)NtReadFile;
    nt_svc_argtable0[NT_SVC_NTREADFILE] = 9 * 8;
    nt_svc_table0[NT_SVC_NTCREATESECTION] = (void *)NtCreateSection;
    nt_svc_argtable0[NT_SVC_NTCREATESECTION] = 7 * 8;
    nt_svc_table0[NT_SVC_NTMAPVIEWOFSECTION] = (void *)NtMapViewOfSection;
    nt_svc_argtable0[NT_SVC_NTMAPVIEWOFSECTION] = 10 * 8;
    nt_svc_table0[NT_SVC_NTPROTECTVIRTUALMEMORY] = (void *)NtProtectVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTPROTECTVIRTUALMEMORY] = 5 * 8;
    nt_svc_table0[NT_SVC_NTQUERYVIRTUALMEMORY] = (void *)NtQueryVirtualMemory;
    nt_svc_argtable0[NT_SVC_NTQUERYVIRTUALMEMORY] = 6 * 8;
    // CHALICE C3c-2i: NtQuerySection(SectionHandle, SectionInformationClass,
    // SectionInformation, SectionInformationLength, ReturnLength) -- 5 args.
    nt_svc_table0[NT_SVC_NTQUERYSECTION] = (void *)NtQuerySection;
    nt_svc_argtable0[NT_SVC_NTQUERYSECTION] = 5 * 8;

    // ---- CHALICE C3c-2k p1: legible-abort handlers ----
    nt_svc_table0[NT_SVC_NTRAISEEXCEPTION] = (void *)NtRaiseException;
    nt_svc_argtable0[NT_SVC_NTRAISEEXCEPTION] = 3 * 8;
    nt_svc_table0[NT_SVC_NTRAISEHARDERROR] = (void *)NtRaiseHardError;
    nt_svc_argtable0[NT_SVC_NTRAISEHARDERROR] = 6 * 8;

    // ---- CHALICE C3c-2n: NtInitializeNlsFiles(BaseAddress, DefaultLocaleId,
    // DefaultCasingTableSize) -- 3 args (ntfile.c) ----
    nt_svc_table0[NT_SVC_NTINITIALIZENLSFILES] = (void *)NtInitializeNlsFiles;
    nt_svc_argtable0[NT_SVC_NTINITIALIZENLSFILES] = 3 * 8;

    // ---- CHALICE C3c-2n: NtGetNlsSectionPtr(Type, Id, Unknown, BaseAddress,
    // Size) -- 5 args (ntfile.c) ----
    nt_svc_table0[NT_SVC_NTGETNLSSECTIONPTR] = (void *)NtGetNlsSectionPtr;
    nt_svc_argtable0[NT_SVC_NTGETNLSSECTIONPTR] = 5 * 8;
    // CHALICE C3c-2o: minimal in-memory registry floor -- see ntfile.c for
    // the per-handler NTSTATUS-contract receipts.
    nt_svc_table0[NT_SVC_NTCREATEKEY] = (void *)NtCreateKey;
    nt_svc_argtable0[NT_SVC_NTCREATEKEY] = 7 * 8;
    nt_svc_table0[NT_SVC_NTOPENKEY] = (void *)NtOpenKey;
    nt_svc_argtable0[NT_SVC_NTOPENKEY] = 3 * 8;
    // CHALICE C4-14: NtOpenKeyEx -- see ntfile.c receipt.
    nt_svc_table0[NT_SVC_NTOPENKEYEX] = (void *)NtOpenKeyEx;
    nt_svc_argtable0[NT_SVC_NTOPENKEYEX] = 4 * 8;
    nt_svc_table0[NT_SVC_NTQUERYVALUEKEY] = (void *)NtQueryValueKey;
    nt_svc_argtable0[NT_SVC_NTQUERYVALUEKEY] = 6 * 8;
    nt_svc_table0[NT_SVC_NTSETVALUEKEY] = (void *)NtSetValueKey;
    nt_svc_argtable0[NT_SVC_NTSETVALUEKEY] = 6 * 8;
    nt_svc_table0[NT_SVC_NTQUERYDEFAULTLOCALE] = (void *)NtQueryDefaultLocale;
    nt_svc_argtable0[NT_SVC_NTQUERYDEFAULTLOCALE] = 2 * 8;

    nt_service_tables[0].ServiceTable  = nt_svc_table0;
    nt_service_tables[0].CounterTable  = 0;
    nt_service_tables[0].ServiceLimit  = NT_SERVICE_COUNT;
    nt_service_tables[0].ArgumentTable = nt_svc_argtable0;
    // tables[1..3]: zero-initialized statics -> ServiceLimit=0 -> any id on
    // them is STATUS_INVALID_SYSTEM_SERVICE (spec A3: "1-3 limit=0").

    nt_kuser_init();   // Atom B: populate KUSER_SHARED_DATA before any PE
                        // code (which reads it before its first service
                        // call, per HANDOFF's trapdoor framing) can run.
}

// =====================================================================
// A4 — TEB/PEB floor. Offsets receipted against
// reactos/sdk/include/ndk/{peb_teb.h,rtltypes.h,ldrtypes.h} (_STRUCT64).
// =====================================================================

// ---- TEB (peb_teb.h _TEB) ----
// NT_TIB64.Self: wine/include/wine/winternl.h:732 "ULONG64 Self; /* 0030 */"
#define TEB_NTTIB_SELF_OFF   0x30
// EnvironmentPointer: peb_teb.h:494 C_ASSERT(...EnvironmentPointer)==0x038)
// -> NtTib occupies exactly [0x00,0x38). ProcessEnvironmentBlock follows
// EnvironmentPointer(8B)+ClientId(2 ULONG_PTR=16B)+ActiveRpcHandle(8B)+
// ThreadLocalStoragePointer(8B): 0x38+8+16+8+8 = 0x60 (matches the
// well-known public Win64 TEB.ProcessEnvironmentBlock offset).
#define TEB_PEB_OFF           0x60
// CHALICE C-hello7 fix (pe8 gate): TEB.StaticUnicodeString, offset
// confirmed by disassembling stock kernelbase.dll!GetModuleHandleA ->
// file_name_AtoW (AllocateDestinationString=FALSE path): `add x19, x18,
// #0x1258` builds the destination UNICODE_STRING* passed to
// RtlAnsiStringToUnicodeString/RtlOemStringToUnicodeString. Left zeroed
// (MaximumLength=0), that Rtl call returns STATUS_BUFFER_OVERFLOW for ANY
// non-empty ANSI name, which GetModuleHandleA turns into ERROR_MOD_NOT_FOUND
// (0x7E) -- root cause of GetModuleHandleA("kernel32.dll") always failing
// while GetModuleHandleA(NULL) (a different, PEB-based path) works fine.
#define TEB_STATICUNICODESTRING_OFF 0x1258
#define TEB_STATICUNICODEBUFFER_OFF 0x1268
#define TEB_STATICUNICODEBUFFER_LEN 0x2C0   // bytes; ballpark of real Windows' capacity, plenty for any single module/path name
// CHALICE C4-28 gate-salvage fix: TEB.ActivationContextStackPointer,
// well-known public Win64 TEB offset 0x2c8 (winternl.h _STRUCT64 TEB,
// matches every published TEB layout reference). Left zeroed (this
// floor's prior state), stock ntdll's RtlFindActivationContextSectionString
// (embedded-symbol resolved, map-fex-offsets.sh) unconditionally does
// `ldr x8,[x18,#0x2c8]; ldr x8,[x8]` -- dereferences TEB->
// ActivationContextStackPointer->ActiveFrame with NO null check on the
// stack-pointer itself (only checks the result), so a NULL
// ActivationContextStackPointer faults immediately (live-caught this pass,
// address 0x0 -> now genuinely unmapped by the C4-28 guard). Real Windows
// always wires this at thread creation (RtlInitializeActivationContextStack)
// so it is never NULL; fix: point it at a small zeroed
// ACTIVATION_CONTEXT_STACK (only the leading ActiveFrame pointer -- offset
// 0 -- is read on this path; zeroed satisfies the `cbz` fast-empty-stack
// branch immediately after).
#define TEB_ACTIVATIONCONTEXTSTACKPOINTER_OFF 0x2c8
#define ACTIVATION_CONTEXT_STACK_SIZE 0x40   // generous; only offset 0 (ActiveFrame) is load-bearing on this path
// CHALICE C4-3: TEB_SIZE 0x1600->0x3000 -- our TEB layout tracks the REAL
// TEB64 (winternl.h _STRUCT64 TEB, C_ASSERT sizeof==0x1838: TlsSlots@0x1480,
// WowTebOffset@0x180c) closely enough that 0x1600 didn't even reach
// WowTebOffset, let alone the TEB32 view Wine places at teb+0x2000
// (wine/dlls/ntdll/unix/unix_private.h:176 `teb_offset = 0x2000`, virtual.c
// :4002 `TEB32 *teb32 = (TEB32*)((char*)ptr + teb_offset)`). 0x3000 covers
// the real TEB64 body (0x1838) AND a full TEB32 (0x1000, winternl.h:1061
// _TEB32 C_ASSERT sizeof==0x1000) at +0x2000..+0x3000, in ONE allocation --
// same block nt_build_teb already hands back, no parallel allocator. Purely
// additive (every existing offset below is unchanged; callers that never
// touch the WOW64 fields see only a bigger, still-zeroed buffer -- no
// regression risk, same growth pattern as scar #10's 0x400->0x1600 bump).
#define TEB_SIZE              0x3000
// ---- CHALICE C4-3: TEB64 WOW64 fields (winternl.h _STRUCT64 TEB, i.e. the
// SAME struct this file's other TEB_* offsets above already track) ----
#define TEB64_TLSSLOTS_OFF     0x1480   // winternl.h:1238 "TlsSlots[64]; /* 1480 */"
#define TEB64_WOWTEBOFFSET_OFF 0x180c   // winternl.h:1300 "WowTebOffset; /* 180c */"
#define TEB_WOW64_VIEW_OFF     0x2000   // unix_private.h:176 teb_offset
#define WOW64_TLS_CPURESERVED  1        // winternl.h:1327
#define WOW64_TLS_WOW64INFO    10       // winternl.h:1336

// ---- TEB32 (winternl.h:1061 _TEB32, C_ASSERT sizeof==0x1000) ----
#define TEB32_SIZE                     0x1000
#define TEB32_TIB_EXCEPTIONLIST_OFF    0x00
#define TEB32_TIB_FIBERDATA_OFF        0x10
#define TEB32_TIB_SELF_OFF             0x18   // winnt.h:2564 NT_TIB32.Self
#define TEB32_PEB_OFF                  0x30
// CHALICE C4-43 fix: TEB32.ActivationContextStackPointer, winternl.h:1087
// (_TEB32 C_ASSERT-anchored offset 0x1a8) -- disasm-confirmed byte-exact
// against stock/i386-windows/ntdll.dll (i686-w64-mingw32-objdump) at the
// exact pe10 fault site (syswow64/ntdll.dll+0xFF05, C4-42's own named-but-
// not-chased frontier): `mov %fs:0x18,%eax; mov 0x1a8(%eax),%eax; mov
// (%eax),%eax` inside RtlFindActivationContextSectionString -- a NULL
// TEB32.ActivationContextStackPointer deref (this floor's TEB32 builder,
// nt_build_wow64_teb32, never wired it -- only ImageBaseAddress/
// ProcessParameters wired so far, C4-29/C4-37). Donor-exact fix (wine/
// dlls/ntdll/unix/virtual.c:4010-4013, the SAME real Wine function that
// wires the 64-bit TEB's ActivationContextStackPointer the C4-28 fix below
// mirrors): the field points at an EMBEDDED ACTIVATION_CONTEXT_STACK32
// (winternl.h:687-694, _TEB32 C_ASSERT-anchored offset 0x184 IN-PLACE
// inside TEB32 itself -- NOT a separate allocation, exactly as donor code
// does it: `teb32->ActivationContextStackPointer = PtrToUlong(&teb32->
// ActivationContextStack)`), with FrameListCache (a LIST_ENTRY32,
// winternl.h:661-665) self-linked (Flink==Blink==&FrameListCache, donor
// virtual.c:4011-4013) and ActiveFrame left 0 -- the ONLY field this
// fault site's own disasm reads (`mov (%eax),%eax; test %eax,%eax; je
// ...`, a null-check on the RESULT taking the fast empty-stack branch),
// same load-bearing-field-only pattern as the C4-28 64-bit fix.
#define TEB32_ACTIVATIONCONTEXTSTACK_OFF        0x184   // winternl.h:1084
#define TEB32_ACTCTXSTACK32_ACTIVEFRAME_OFF     0x00
#define TEB32_ACTCTXSTACK32_FRAMELISTCACHE_OFF  0x04    // LIST_ENTRY32 Flink(4)+Blink(4)
#define TEB32_ACTCTXSTACK32_FLAGS_OFF           0x0C
#define TEB32_ACTCTXSTACK32_NEXTCOOKIE_OFF      0x10
#define TEB32_ACTCTXSTACK32_STACKID_OFF         0x14
#define TEB32_ACTIVATIONCONTEXTSTACKPOINTER_OFF 0x1a8   // winternl.h:1087
#define TEB32_STATICUNICODESTRING_OFF  0xbf8
#define TEB32_STATICUNICODEBUFFER_OFF  0xc00
#define TEB32_STATICUNICODEBUFFER_LEN  0x20a  // 261 WCHAR * 2 (winternl.h:1113 StaticUnicodeBuffer[261])
#define TEB32_GDIBATCHCOUNT_OFF        0xf70
#define TEB32_WOWTEBOFFSET_OFF         0xfdc
#define US32_BUFFER_OFF                0x04   // UNICODE_STRING32 (winternl.h:673): Length(2)@0,MaximumLength(2)@2,Buffer(4)@4

// ---- PEB32 (winternl.h:833 _PEB32, C_ASSERT sizeof==0x480) ----
#define PEB32_SIZE                    0x480
#define PEB32_PROCESSPARAMETERS_OFF   0x10   // winternl.h:849 _PEB32.ProcessParameters -- CHALICE C4-37
#define PEB32_NUMBEROFPROCESSORS_OFF  0x64
#define PEB32_OSMAJORVERSION_OFF      0xa4
#define PEB32_OSMINORVERSION_OFF      0xa8
#define PEB32_OSBUILDNUMBER_OFF       0xac

// ---- CHALICE C4-37: RTL_USER_PROCESS_PARAMETERS32 (wine include/
// winternl.h:733-768) + CURDIR32/UNICODE_STRING32 (:673-678, :705-708).
// Offsets confirmed against BOTH the donor header AND a live disasm of
// stock/i386-windows/ntdll.dll's _init_user_process_params (RVA 0x1E8F0-
// 0x1EA30): `movl 0x290(%ebx),%esi` (the fault site this atom fixes) is
// EnvironmentSize, byte-exact with the struct's own field order (0x24
// CurrentDirectory=12B CURDIR32 -> 0x30 DllPath -> 0x38 ImagePathName ->
// 0x40 CommandLine -> 0x48 Environment(ptr) -> ... -> 0x90 DLCurrentDirectory
// [0x20]*16B=0x200 -> 0x290 EnvironmentSize -> 0x29c ProcessGroupId, this
// last one ALSO confirmed live: `movl 0x29c(%ebx),%eax` / `movl %eax,
// 0x29c(%esi)` in the same function's field-copy loop). Only the fields
// this atom actually wires get their own #define; the rest (dwX../
// WindowTitle../ProcessGroupId) are read/copied by value at 0, safe.
#define PARAMS32_CURRENTDIRECTORY_OFF 0x24   // CURDIR32: UNICODE_STRING32(8)+Handle(4)
#define PARAMS32_DLLPATH_OFF          0x30   // UNICODE_STRING32
#define PARAMS32_IMAGEPATHNAME_OFF    0x38   // UNICODE_STRING32
#define PARAMS32_COMMANDLINE_OFF      0x40   // UNICODE_STRING32
#define PARAMS32_SIZE                 0x2a4  // through LoaderThreads, rounded up
#define US32_LENGTH_OFF                0x00  // UNICODE_STRING32.Length
#define US32_MAXLENGTH_OFF             0x02  // UNICODE_STRING32.MaximumLength

// ---- WOW64INFO (winternl.h:4395, C_ASSERT sizeof==40); stored
// immediately after PEB32 (wine/dlls/wow64/syscall.c:1000 "wow64info =
// (WOW64INFO *)(peb32 + 1)") ----
#define WOW64INFO_SIZE                     0x28
#define WOW64INFO_NATIVESYSTEMPAGESIZE_OFF 0x00
#define WOW64INFO_CPUFLAGS_OFF             0x04
#define WOW64INFO_CROSSPROCESSWORKLIST_OFF 0x18
#define WOW64INFO_NATIVEMACHINETYPE_OFF    0x20
#define WOW64INFO_EMULATEDMACHINETYPE_OFF  0x22

// ---- CROSS_PROCESS_WORK_LIST (winternl.h:306-333). Real init sizes the
// shared section to 0x4000 (syscall.c:952 "map_size = 0x4000") -- matched
// exactly (not shrunk) so a later atom's real BTCpu-facing push/pop needs
// no resize. Free list built with the SAME prepend-every-entry algorithm
// as create_cross_process_work_list (syscall.c:972-973) +
// RtlWow64PushCrossProcessWorkOntoFreeList (ntdll/process.c:411-425: new
// entry's `next` = old first, `first` becomes byte-offset of new entry
// FROM THE LIST BASE, counter++) -- built directly here since no code
// executes yet to call the real Rtl function. ----
#define CPWL_HDR_SIZE      0x30   // free_list(8)+work_list(8)+unknown[4](32)
#define CPWL_ENTRY_SIZE    40     // next(4)+id(4)+addr(8)+size(8)+args[4](16), winternl.h:308-315
#define CPWL_MAP_SIZE      0x4000
#define CPWL_FREELIST_OFF  0x00
#define CPWL_ENTRIES_OFF   CPWL_HDR_SIZE

// ---- WOW64_CPURESERVED (winternl.h:4375): USHORT Flags + USHORT Machine,
// followed by an inline i386 CONTEXT (winnt.h:1177 I386_CONTEXT, sizeof
// 0xcc+0x200==0x2cc) + CONTEXT_EX (winnt.h:1596: 3 CONTEXT_CHUNK(8B each)
// + ULONG64 align under _WIN64 == 0x20) when Machine==i386 (our only guest
// arch this leg). Pure per-thread CPU-sim scratch -- BTCpuSimulate reads/
// writes it once wired (C4-4/5); this atom allocates+zeroes it and wires
// TlsSlots[WOW64_TLS_CPURESERVED] to it, no simulate call happens yet. ----
#define WOW64_CPURESERVED_HDR_SIZE    4
#define I386_CONTEXT_SIZE             0x2cc
#define CONTEXT_EX_SIZE                0x20
#define WOW64_CPURESERVED_SIZE (WOW64_CPURESERVED_HDR_SIZE + I386_CONTEXT_SIZE + CONTEXT_EX_SIZE)
#define WOW64_CPURESERVED_MACHINE_OFF 0x02

// ---- PEB (peb_teb.h _PEB), sequential from field order + C_ASSERT anchors
// (Ldr==0x18, FastPebLock==0x038, TlsExpansionCounter==0x070,
// NtGlobalFlag==0x0BC, GdiSharedHandleTable==0x0F8, LoaderLock==0x110,
// ImageSubsystem==0x128 — all confirmed in peb_teb.h _STRUCT64 block) ----
#define PEB_BEINGDEBUGGED_OFF     0x02
#define PEB_IMAGEBASEADDRESS_OFF  0x10
#define PEB_LDR_OFF               0x18   // C_ASSERT-anchored
#define PEB_PROCESSPARAMETERS_OFF 0x20
#define PEB_PROCESSHEAP_OFF       0x30
// NumberOfProcessors precedes NtGlobalFlag(C_ASSERT 0x0BC) by one ULONG:
// UnicodeCaseTableData(ptr)@0xB0 -> NumberOfProcessors(ULONG)@0xB8 -> 0xBC.
#define PEB_NUMBEROFPROCESSORS_OFF 0xB8
// OSMajorVersion/Minor/Build precede ImageSubsystem(C_ASSERT 0x128):
// LoaderLock(ptr)@0x110 -> OSMajorVersion(ULONG)@0x118 -> OSMinorVersion@0x11C
// -> OSBuildNumber(USHORT)@0x120 -> OSCSDVersion(USHORT)@0x122 ->
// OSPlatformId(ULONG)@0x124 -> ImageSubsystem@0x128. Matches public Win64 offsets.
#define PEB_OSMAJORVERSION_OFF    0x118
#define PEB_OSMINORVERSION_OFF    0x11C
#define PEB_OSBUILDNUMBER_OFF     0x120
#define PEB_SIZE                  0x140

// ---- RTL_USER_PROCESS_PARAMETERS (rtltypes.h ~:1548) ----
// MaximumLength(4)+Length(4)+Flags(4)+DebugFlags(4)=0x10 -> ConsoleHandle
// (HANDLE, 8-align)@0x10 -> ConsoleFlags(ULONG)@0x18 -> StandardInput
// (HANDLE, 8-align, pad 4)@0x20 -> StandardOutput@0x28 -> StandardError@0x30.
// 0x28 matches SLICE2-SPEC A4's own receipt ("StdOutput @ +0x28").
#define PARAMS_STANDARDOUTPUT_OFF 0x28
// CHALICE C3b Atom 3B: bumped 0x60 -> 0x90 -- headroom for ntboot.c's
// nt_boot_process to also populate StandardInput/StandardError (already
// laid out below Atom A's StandardOutput receipt) + ImagePathName/
// CommandLine UNICODE_STRINGs (well-known public RTL_USER_PROCESS_
// PARAMETERS offsets: CurrentDirectory@0x38, DllPath@0x50,
// ImagePathName@0x60, CommandLine@0x70, Environment ptr@0x80) -- purely a
// larger zeroed MAlloc, no change to Atom A/3A's existing byte usage
// (StandardOutput@0x28 untouched, PeRun/hello3/hello4 unaffected).
#define PARAMS_SIZE               0x90

// ---- PEB_LDR_DATA (ldrtypes.h ~:119, WIN7+ branch: ShutdownThreadId present) ----
// Length(4)+Initialized(1,pad3)=8 -> SsHandle(8)@0x08 -> InLoadOrderModuleList
// (LIST_ENTRY,16B)@0x10 -> InMemoryOrderModuleList@0x20 ->
// InInitializationOrderModuleList@0x30 -> EntryInProgress@0x40 ->
// ShutdownInProgress(1,pad7)@0x48 -> ShutdownThreadId@0x50 -> size 0x58.
#define LDR_INLOADORDER_OFF        0x10
#define LDR_INMEMORYORDER_OFF      0x20
#define LDR_INEINITORDER_OFF       0x30
#define LDR_SIZE                   0x60

// ---- LDR_DATA_TABLE_ENTRY (ldrtypes.h ~:140) ----
// InLoadOrderLinks(16)@0 -> InMemoryOrderLinks(16)@0x10 ->
// InInitializationOrderLinks(16)@0x20 -> DllBase(8)@0x30 -> EntryPoint(8)@0x38
// -> SizeOfImage(4,pad4)@0x40 -> FullDllName(UNICODE_STRING,16B)@0x48 ->
// BaseDllName(16B)@0x58 -> Flags(4)@0x68 -> size 0xA0 (rounded, headroom for
// the trailing union/PVOID fields not needed by Atom A).
#define MOD_INLOADORDER_OFF        0x00
#define MOD_INMEMORYORDER_OFF      0x10
#define MOD_ININITORDER_OFF        0x20
#define MOD_DLLBASE_OFF            0x30
#define MOD_ENTRYPOINT_OFF         0x38
#define MOD_SIZEOFIMAGE_OFF        0x40
#define MOD_BASEDLLNAME_OFF        0x58   // UNICODE_STRING, 16B (FullDllName@0x48 left unset)
#define MOD_SIZE                   0xA0
#define US_LENGTH_OFF              0x00
#define US_MAXLENGTH_OFF           0x02
#define US_BUFFER_OFF              0x08

void *nt_build_teb(void *image_base, void *entry, unsigned long image_size) {
    nt_service_tables_init();

    u8 *teb    = (u8 *)nt_zalloc(TEB_SIZE);
    u8 *peb    = (u8 *)nt_zalloc(PEB_SIZE);
    u8 *params = (u8 *)nt_zalloc(PARAMS_SIZE);
    u8 *ldr    = (u8 *)nt_zalloc(LDR_SIZE);
    u8 *mod    = (u8 *)nt_zalloc(MOD_SIZE);
    u8 *heap   = (u8 *)nt_zalloc(4096);   // dummy ProcessHeap
    u8 *actctx = (u8 *)nt_zalloc(ACTIVATION_CONTEXT_STACK_SIZE);
    if (!teb || !peb || !params || !ldr || !mod || !heap || !actctx) return 0;

    // ---- trapdoor #1: TEB.NtTib.Self ----
    wr64(teb + TEB_NTTIB_SELF_OFF, (u64b)teb);
    // ---- trapdoor #2: TEB.ProcessEnvironmentBlock ----
    wr64(teb + TEB_PEB_OFF, (u64b)peb);
    // ---- CHALICE C4-28 gate-salvage fix: TEB.ActivationContextStackPointer
    // (see offset comment above) -- zeroed ACTIVATION_CONTEXT_STACK, never
    // NULL. ----
    wr64(teb + TEB_ACTIVATIONCONTEXTSTACKPOINTER_OFF, (u64b)actctx);
    // ---- A1 dispatch slots ----
    wr64(teb + NT_TEB_SERVICE_TABLE_OFF, (u64b)nt_service_tables);
    wr64(teb + NT_TEB_SYSCALL_FRAME_OFF, 0);
    wr64(teb + NT_TEB_DISPATCH_ENTRY_OFF, (u64b)nt_dispatch);

    // ---- CHALICE C-hello7 fix: TEB.StaticUnicodeString -- an empty,
    // properly-capacitied UNICODE_STRING pointing at the in-TEB scratch
    // buffer right after it (see offset comment above). Must be valid
    // BEFORE any guest code resolves an ANSI module/path name (kernelbase's
    // GetModuleHandleA/GetModuleFileNameA etc all route through this slot
    // when AllocateDestinationString=FALSE). ----
    wr16(teb + TEB_STATICUNICODESTRING_OFF + US_LENGTH_OFF, 0);
    wr16(teb + TEB_STATICUNICODESTRING_OFF + US_MAXLENGTH_OFF, TEB_STATICUNICODEBUFFER_LEN);
    wr64(teb + TEB_STATICUNICODESTRING_OFF + US_BUFFER_OFF, (u64b)(teb + TEB_STATICUNICODEBUFFER_OFF));

    // ---- trapdoor #3: PEB.Ldr ----
    wr64(peb + PEB_LDR_OFF, (u64b)ldr);
    // ---- trapdoor #4: PEB.ProcessParameters ----
    wr64(peb + PEB_PROCESSPARAMETERS_OFF, (u64b)params);
    wr64(peb + PEB_IMAGEBASEADDRESS_OFF, (u64b)image_base);
    wr64(peb + PEB_PROCESSHEAP_OFF, (u64b)heap);
    peb[PEB_BEINGDEBUGGED_OFF] = 0;
    wr32(peb + PEB_NUMBEROFPROCESSORS_OFF, 1);
    wr32(peb + PEB_OSMAJORVERSION_OFF, 10);
    wr32(peb + PEB_OSMINORVERSION_OFF, 0);
    wr16(peb + PEB_OSBUILDNUMBER_OFF, 19041);   // arbitrary Win10 build, not load-bearing

    // ---- ProcessParameters: only hStdOutput is load-bearing for Atom A ----
    wr64(params + PARAMS_STANDARDOUTPUT_OFF, (u64b)NT_STDOUT_HANDLE);

    // ---- PEB_LDR_DATA + one LDR_DATA_TABLE_ENTRY (the image), circular
    // doubly-linked lists of exactly one node (self-consistent). ----
    wr32(ldr + 0x00, LDR_SIZE);
    ldr[0x04] = 1;   // Initialized
    wr64(ldr + LDR_INLOADORDER_OFF + 0, (u64b)(mod + MOD_INLOADORDER_OFF));
    wr64(ldr + LDR_INLOADORDER_OFF + 8, (u64b)(mod + MOD_INLOADORDER_OFF));
    wr64(ldr + LDR_INMEMORYORDER_OFF + 0, (u64b)(mod + MOD_INMEMORYORDER_OFF));
    wr64(ldr + LDR_INMEMORYORDER_OFF + 8, (u64b)(mod + MOD_INMEMORYORDER_OFF));
    wr64(ldr + LDR_INEINITORDER_OFF + 0, (u64b)(mod + MOD_ININITORDER_OFF));
    wr64(ldr + LDR_INEINITORDER_OFF + 8, (u64b)(mod + MOD_ININITORDER_OFF));

    wr64(mod + MOD_INLOADORDER_OFF + 0, (u64b)(ldr + LDR_INLOADORDER_OFF));
    wr64(mod + MOD_INLOADORDER_OFF + 8, (u64b)(ldr + LDR_INLOADORDER_OFF));
    wr64(mod + MOD_INMEMORYORDER_OFF + 0, (u64b)(ldr + LDR_INMEMORYORDER_OFF));
    wr64(mod + MOD_INMEMORYORDER_OFF + 8, (u64b)(ldr + LDR_INMEMORYORDER_OFF));
    wr64(mod + MOD_ININITORDER_OFF + 0, (u64b)(ldr + LDR_INEINITORDER_OFF));
    wr64(mod + MOD_ININITORDER_OFF + 8, (u64b)(ldr + LDR_INEINITORDER_OFF));
    wr64(mod + MOD_DLLBASE_OFF, (u64b)image_base);
    wr64(mod + MOD_ENTRYPOINT_OFF, (u64b)entry);
    wr32(mod + MOD_SIZEOFIMAGE_OFF, (u32b)image_size);

    return teb;
}

// =====================================================================
// CHALICE C4-3 (C4-SPEC.md atom C4-3): 32-bit TEB32/PEB32 floor.
// Extends an already-built 64-bit TEB (nt_build_teb, above -- TEB_SIZE now
// 0x3000, grown this atom specifically to make room here) with the WOW64
// view Wine's own code expects: a TEB32 in-place at teb64+0x2000, a PEB32
// + trailing WOW64INFO, a CROSS_PROCESS_WORK_LIST, and a WOW64_CPURESERVED
// per-thread CPU-sim scratch block -- wired into TEB64.TlsSlots exactly the
// way dlls/wow64/syscall.c's process_init/BTCpuSimulate call sites read
// them (offset citations in the struct-offset block above). No guest code
// runs here (C4-2's own doctrine, unchanged) -- this only builds+wires the
// structures and hands back their addresses for a serial proof dump.
// =====================================================================
long nt_build_wow64_teb32(void *teb64_v, unsigned short native_machine,
                           unsigned short emulated_machine,
                           struct NtWow64Info *out) {
    u8 *teb64 = (u8 *)teb64_v;
    if (!teb64 || !out) return -1;

    // ---- TEB32 view, in-place at teb64+0x2000 (already zeroed -- part of
    // teb64's own nt_zalloc(TEB_SIZE) block). ----
    u8 *teb32 = teb64 + TEB_WOW64_VIEW_OFF;
    wr32(teb32 + TEB32_TIB_EXCEPTIONLIST_OFF, 0xFFFFFFFFUL);        // virtual.c:4009 Tib.ExceptionList = ~0u
    wr32(teb32 + TEB32_TIB_FIBERDATA_OFF, 0x1e00);                  // virtual.c:4010
    wr32(teb32 + TEB32_TIB_SELF_OFF, (u32b)(u64b)teb32);
    wr16(teb32 + TEB32_STATICUNICODESTRING_OFF + 0 /* Length */, 0);
    wr16(teb32 + TEB32_STATICUNICODESTRING_OFF + 2 /* MaximumLength */, TEB32_STATICUNICODEBUFFER_LEN);
    wr32(teb32 + TEB32_STATICUNICODESTRING_OFF + US32_BUFFER_OFF,
         (u32b)(u64b)(teb32 + TEB32_STATICUNICODEBUFFER_OFF));
    wr32(teb32 + TEB32_GDIBATCHCOUNT_OFF, (u32b)(u64b)teb64);       // virtual.c:4016 GdiBatchCount = PtrToUlong(teb64)
    wr32(teb32 + TEB32_WOWTEBOFFSET_OFF, (u32b)(0UL - TEB_WOW64_VIEW_OFF));   // virtual.c:4017 WowTebOffset = -teb_offset

    // ---- CHALICE C4-43 fix: TEB32.ActivationContextStackPointer -- points
    // at the embedded ACTIVATION_CONTEXT_STACK32 IN-PLACE (donor virtual.c
    // :4010, see offset comment above), FrameListCache self-linked (donor
    // virtual.c:4011-4013), ActiveFrame/Flags/NextCookieSequenceNumber/
    // StackId left 0 (teb32 is nt_zalloc'd -- only ActiveFrame is load-
    // bearing on the fault site this atom fixes, and 0 satisfies its
    // fast-empty-stack branch). ----
    wr32(teb32 + TEB32_ACTIVATIONCONTEXTSTACKPOINTER_OFF,
         (u32b)(u64b)(teb32 + TEB32_ACTIVATIONCONTEXTSTACK_OFF));
    wr32(teb32 + TEB32_ACTIVATIONCONTEXTSTACK_OFF + TEB32_ACTCTXSTACK32_FRAMELISTCACHE_OFF + 0 /* Flink */,
         (u32b)(u64b)(teb32 + TEB32_ACTIVATIONCONTEXTSTACK_OFF + TEB32_ACTCTXSTACK32_FRAMELISTCACHE_OFF));
    wr32(teb32 + TEB32_ACTIVATIONCONTEXTSTACK_OFF + TEB32_ACTCTXSTACK32_FRAMELISTCACHE_OFF + 4 /* Blink */,
         (u32b)(u64b)(teb32 + TEB32_ACTIVATIONCONTEXTSTACK_OFF + TEB32_ACTCTXSTACK32_FRAMELISTCACHE_OFF));

    // ---- cross-link on the 64-bit TEB (virtual.c:4018 "teb64->WowTebOffset = teb_offset") ----
    wr32(teb64 + TEB64_WOWTEBOFFSET_OFF, TEB_WOW64_VIEW_OFF);

    // ---- PEB32 + trailing WOW64INFO (syscall.c:1000 "(peb32 + 1)"). PEB32's
    // own trapdoor fields (ImageBaseAddress/LdrData/ProcessParameters/
    // ApiSetMap) are left 0 this atom -- no 32-bit ntdll/exe image mapped
    // yet (that's C4-4/5 territory); documented gap, not silent (dumped as
    // 0 by the pe9 proof line, not hidden). ----
    u8 *peb32 = (u8 *)nt_zalloc(PEB32_SIZE + WOW64INFO_SIZE);
    if (!peb32) return -1;
    u8 *wow64info = peb32 + PEB32_SIZE;

    wr32(peb32 + PEB32_NUMBEROFPROCESSORS_OFF, 1);
    wr32(peb32 + PEB32_OSMAJORVERSION_OFF, 10);
    wr32(peb32 + PEB32_OSMINORVERSION_OFF, 0);
    wr32(peb32 + PEB32_OSBUILDNUMBER_OFF, 19041);
    wr32(teb32 + TEB32_PEB_OFF, (u32b)(u64b)peb32);

    wr32(wow64info + WOW64INFO_NATIVESYSTEMPAGESIZE_OFF, 0x1000);           // syscall.c:1001
    // WOW64INFO.CpuFlags: left 0 -- grep of the vanilla donor (dlls/wow64,
    // dlls/ntdll) shows NO assignment to ->CpuFlags anywhere; it's read
    // (syscall.c:252) but never written in-tree, presumably a backend-
    // specific (BTCpuProcessInit) job -- measured, not guessed at.
    wr16(wow64info + WOW64INFO_NATIVEMACHINETYPE_OFF, native_machine);      // syscall.c:1002
    wr16(wow64info + WOW64INFO_EMULATEDMACHINETYPE_OFF, emulated_machine);  // syscall.c:1003

    // ---- CROSS_PROCESS_WORK_LIST: exact 0x4000 size real Wow64 uses
    // (syscall.c:952), free list fully populated (prepend-all-entries,
    // matching syscall.c:972-973 + process.c:411-425). ----
    u8 *cpwl = (u8 *)nt_zalloc(CPWL_MAP_SIZE);
    if (!cpwl) return -1;
    unsigned int n_entries = (CPWL_MAP_SIZE - CPWL_ENTRIES_OFF) / CPWL_ENTRY_SIZE;
    u32b first_off = 0;
    for (unsigned int i = 0; i < n_entries; i++) {
        u8 *entry = cpwl + CPWL_ENTRIES_OFF + (u64b)i * CPWL_ENTRY_SIZE;
        wr32(entry + 0 /* next */, first_off);
        first_off = (u32b)(u64b)(entry - cpwl);
    }
    wr32(cpwl + CPWL_FREELIST_OFF + 0 /* .first */, first_off);
    wr32(cpwl + CPWL_FREELIST_OFF + 4 /* .counter */, n_entries);
    wr64(wow64info + WOW64INFO_CROSSPROCESSWORKLIST_OFF, (u64b)cpwl);       // syscall.c:975

    // ---- TlsSlots wiring (real TEB64 offset 0x1480, winternl.h:1238) ----
    wr64(teb64 + TEB64_TLSSLOTS_OFF + WOW64_TLS_WOW64INFO * 8, (u64b)wow64info);   // syscall.c:1004

    // ---- WOW64_CPURESERVED per-thread CPU-sim scratch; TlsSlots[WOW64_TLS_
    // CPURESERVED] is how BTCpuSimulate's caller locates it (syscall.c:1393
    // "WOW64_CPURESERVED *cpu = NtCurrentTeb()->TlsSlots[WOW64_TLS_CPURESERVED]"). ----
    u8 *cpureserved = (u8 *)nt_zalloc(WOW64_CPURESERVED_SIZE);
    if (!cpureserved) return -1;
    wr16(cpureserved + WOW64_CPURESERVED_MACHINE_OFF, emulated_machine);
    wr64(teb64 + TEB64_TLSSLOTS_OFF + WOW64_TLS_CPURESERVED * 8, (u64b)cpureserved);

    out->teb32 = teb32;
    out->peb32 = peb32;
    out->wow64info = wow64info;
    out->crossproclist = cpwl;
    out->crossproclist_size = CPWL_MAP_SIZE;
    out->crossproclist_entries = n_entries;
    out->cpureserved = cpureserved;
    return 0;
}

// =====================================================================
// CHALICE C4-37: PEB32.ProcessParameters -- wires a real, non-NULL 32-bit
// RTL_USER_PROCESS_PARAMETERS32 into PEB32 (PEB32_PROCESSPARAMETERS_OFF,
// above). C4-36 reached ntdll32's own _init_user_process_params for the
// first time and it faulted dereferencing this exact field (always 0
// before this atom -- nt_build_wow64_teb32 only ever wired ImageBaseAddress).
//
// Disasm receipt (stock/i386-windows/ntdll.dll RVA 0x1E8F0-0x1EA30, this
// atom, ImageBase 0x7BC00000): after `movl fs:0x18,%eax; movl 0x30(%eax),
// %eax; movl 0x10(%eax),%ebx` (TEB32->PEB32->ProcessParameters == our
// struct, %ebx), the function:
//   - `movl 0x290(%ebx),%esi` -- EnvironmentSize, read UNCONDITIONALLY
//     (the fault site: far=0x290 byte-matched this offset exactly).
//   - `pushl 0x48(%ebx)` (Environment ptr) -- only reached if esi!=0
//     (`testl %esi,%esi; je ...`) -- left 0/0 (empty env), never derefed.
//   - `leal 0x24/0x30/0x38/0x40/0x70/0x78/0x80/0x88(%ebx),%e*` then a
//     fastcall to an internal RtlCreateProcessParametersEx-shaped helper
//     (`_alloc_process_params`, ecx=flags=1, edx=&ImagePathName@0x38,
//     stack args DllPath@0x30/CurrentDirectory@0x24/CommandLine@0x40/
//     WindowTitle@0x70/Desktop@0x78/ShellInfo@0x80/RuntimeInfo@0x88) --
//     UNICODE_STRING32s copied BY VALUE; safe left-zeroed (Length=0) per
//     Rtl's own `if (str->Length) memcpy(...)` idiom, EXCEPT
//     CurrentDirectory: `RtlSetCurrentDirectory_U` (called right after,
//     on the NEWLY built params' copy of this same field) and this
//     floor's own C4-28 scar (get_full_path_helper) both proved some
//     ntdll helpers deref DosPath.Buffer unconditionally -- given a real
//     "C:\\" buffer here, never zeroed.
//   - simple ULONG fields (DebugFlags/ConsoleHandle/ConsoleFlags/hStd*/
//     dwX../ProcessGroupId@0x29c, also live-confirmed via its own
//     `movl 0x29c(%ebx),%eax`/`movl %eax,0x29c(%esi)` copy) are copied by
//     value only -- safe at 0.
//
// Minimal-but-real per this atom's brief: CurrentDirectory/DllPath get a
// real "C:\\" UNICODE_STRING32 (own static buffer, guest-visible <4GB --
// this file's whole heap is nt_zalloc/MAlloc-backed, HEAP_BASE 0x41000000,
// mem.c); ImagePathName/CommandLine reuse the SAME wide buffers the
// 64-bit PEB.ProcessParameters already uses (WOW64 FEX runs guest+host in
// one address space -- C4-35 -- no duplication, no 64-bit-space pointer,
// caller truncates to u32b); WindowTitle/Desktop/ShellInfo/RuntimeInfo/
// Environment left zeroed (Length=0/EnvironmentSize=0 -- valid empty-
// string state per the disasm above, never dereferenced).
// =====================================================================
void nt_wow64_build_params32(void *peb32_v,
                              const unsigned short *image_path, unsigned long image_path_chars,
                              const unsigned short *command_line, unsigned long command_line_chars) {
    u8 *peb32 = (u8 *)peb32_v;
    if (!peb32) return;

    u8 *params32 = (u8 *)nt_zalloc(PARAMS32_SIZE);
    if (!params32) return;

    // CurrentDirectory (CURDIR32 @ 0x24) + DllPath (@ 0x30): real "C:\\",
    // heap-backed buffer -- C4-28 scar law: some ntdll helpers deref
    // DosPath.Buffer regardless of Length. MUST be guest-visible heap
    // memory (nt_zalloc/mem.c HEAP_BASE 0x41000000), NOT a `static const`
    // kernel global -- a first live-caught version of this fix used the
    // latter and faulted (translation fault, DFSC level-1) the moment
    // ntdll32's own memcpy tried to read it: kernel .rodata is EL1-only,
    // never mapped into the guest/EL0 execution context the FEX JIT runs
    // under, unlike this heap range (already proven guest-accessible by
    // every other wide buffer in this file/ntboot.c).
    u16 *curdir_w = (u16 *)nt_zalloc(4 * 2);
    if (!curdir_w) return;
    curdir_w[0] = 'C'; curdir_w[1] = ':'; curdir_w[2] = '\\'; curdir_w[3] = 0;
    wr16(params32 + PARAMS32_CURRENTDIRECTORY_OFF + US32_LENGTH_OFF, 3 * 2);
    wr16(params32 + PARAMS32_CURRENTDIRECTORY_OFF + US32_MAXLENGTH_OFF, 4 * 2);
    wr32(params32 + PARAMS32_CURRENTDIRECTORY_OFF + US32_BUFFER_OFF, (u32b)(u64b)curdir_w);

    // CHALICE C4-45: DllPath -- was aliased to curdir_w ("C:\\", a bare
    // drive root, single ';'-free segment) -- root cause of the SECOND
    // STATUS_INVALID_IMAGE_FORMAT (0xC000007B) C4-44 left unpinned. Donor
    // trace (wine/dlls/ntdll/loader.c): process_init sets `default_load_path
    // = peb->ProcessParameters->DllPath.Buffer` (loader.c:4482) whenever
    // DllPath is non-empty (it always was here, just wrong-shaped); the
    // later `load_dll(NULL, L"kernel32.dll", ...)` call (loader.c:4493,
    // process_init's own hardcoded KERNEL32 dependency load, independent of
    // hello8_x86.exe's own EMPTY import table -- not the C4-44 build_main_
    // module gate) resolves load_path=NULL to that exact default_load_path,
    // then find_dll_file->search_dll_file (loader.c:3236-3238) splits it on
    // ';' -- a bare "C:\\" has none, so exactly ONE candidate is tried:
    // "C:\\kernel32.dll" (byte-for-byte the `NtOpenFile kernel32.dll dir=C:`
    // this floor's own receipt showed). This floor's own NtOpenFile dirname
    // redirect (ntfile.c, C4-4) only rewrites dirname=="system32"(+wow64)/
    // "syswow64"/"sysnative" to the syswow64/ blob root -- dirname "C:"
    // matches none of those, so the lookup falls through to the BARE-name
    // blob registry entry, which mkdisk.sh registers FIRST as the NATIVE
    // (AMD64) kernel32.dll, not the 32-bit syswow64 one. Donor's own
    // is_valid_binary (loader.c:2515-2529, checked via NtQuerySection
    // SectionImageInformation.Machine, already answered correctly by this
    // floor since C3c-2i) then rejects that wrong-arch mapping ->
    // STATUS_NOT_SUPPORTED -> search_dll_file's single-candidate loop
    // exhausts -> STATUS_NOT_SUPPORTED propagates -> find_dll_file's own
    // conversion (loader.c:3323) turns it into STATUS_INVALID_IMAGE_FORMAT
    // -> process_init's kernel32-load check (loader.c:4493-4499) calls
    // `NtTerminateProcess(GetCurrentProcess(), status)` -- exactly this
    // floor's second 0xC000007B, now pinned end-to-end via donor source,
    // no guesswork. Fix (donor-exact default): real Windows/Wine's own
    // DllPath default is "%SystemRoot%\\System32" (get_dll_load_path,
    // unix/loader.c) -- a single "C:\\windows\\system32" segment gives
    // dirname="system32" on the SAME lookup, which this floor's EXISTING
    // redirect (nt_wow64_process_active is true for every wow64 boot,
    // ntboot.c nt_wow64_set_active) already turns into the CORRECT
    // "syswow64/kernel32.dll" blob open -- zero new redirect logic needed,
    // just a correctly-shaped DllPath value.
    static const char dllpath_ascii[] = "C:\\windows\\system32";
    #define C4_45_DLLPATH_CHARS ((unsigned long)(sizeof(dllpath_ascii) - 1))
    u16 *dllpath_w = (u16 *)nt_zalloc((C4_45_DLLPATH_CHARS + 1) * 2);
    if (!dllpath_w) return;
    for (unsigned long i = 0; i < C4_45_DLLPATH_CHARS; i++) dllpath_w[i] = (u16)(unsigned char)dllpath_ascii[i];
    dllpath_w[C4_45_DLLPATH_CHARS] = 0;
    wr16(params32 + PARAMS32_DLLPATH_OFF + US32_LENGTH_OFF, (u16)(C4_45_DLLPATH_CHARS * 2));
    wr16(params32 + PARAMS32_DLLPATH_OFF + US32_MAXLENGTH_OFF, (u16)((C4_45_DLLPATH_CHARS + 1) * 2));
    wr32(params32 + PARAMS32_DLLPATH_OFF + US32_BUFFER_OFF, (u32b)(u64b)dllpath_w);

    if (image_path && image_path_chars) {
        wr16(params32 + PARAMS32_IMAGEPATHNAME_OFF + US32_LENGTH_OFF, (u16)(image_path_chars * 2));
        wr16(params32 + PARAMS32_IMAGEPATHNAME_OFF + US32_MAXLENGTH_OFF, (u16)(image_path_chars * 2));
        wr32(params32 + PARAMS32_IMAGEPATHNAME_OFF + US32_BUFFER_OFF, (u32b)(u64b)image_path);
    }
    if (command_line && command_line_chars) {
        wr16(params32 + PARAMS32_COMMANDLINE_OFF + US32_LENGTH_OFF, (u16)(command_line_chars * 2));
        wr16(params32 + PARAMS32_COMMANDLINE_OFF + US32_MAXLENGTH_OFF, (u16)(command_line_chars * 2));
        wr32(params32 + PARAMS32_COMMANDLINE_OFF + US32_BUFFER_OFF, (u32b)(u64b)command_line);
    }

    wr32(peb32 + PEB32_PROCESSPARAMETERS_OFF, (u32b)(u64b)params32);
    puts("CHALICE C4-37: PEB32.ProcessParameters=");
    puthex((u64b)(*(volatile u32b *)(peb32 + PEB32_PROCESSPARAMETERS_OFF)));
    puts("\n");
}

// CHALICE C3c-2l Atom A/B — nt_ldr_add_module: insert a SECOND (or Nth)
// LDR_DATA_TABLE_ENTRY into an already-built TEB's PEB.Ldr module list.
// nt_build_teb (above) only ever registers ONE self-entry (the image it
// was called with, always the EXE for nt_boot_process) -- ntdll itself
// (mapped directly by ntboot.c's own C code via nt_territory_alloc,
// bypassing the guest's own NtCreateSection/NtMapViewOfSection dependency-
// load path that kernel32/kernelbase go through) never gets a module-list
// entry at all. Phase-A receipt (C3c-2l): the guest's own find_named_export
// module lookup for "ntdll.dll" during KERNEL32's __wine_dbg_header import
// fixup walks PEB.Ldr looking for a module named "ntdll.dll" -- with zero
// such entries present, the lookup can't succeed regardless of base
// alignment; this is the missing-registration gap, not a base-math bug.
// Inserts at the TAIL of all three circular doubly-linked lists (InLoad/
// InMemory/InInit order) -- same list-splice pattern nt_build_teb already
// uses for its single self-node, generalized to N nodes.
void nt_ldr_add_module(void *teb_v, void *image_base, void *entry, unsigned long image_size,
      const unsigned short *name_utf16, unsigned long name_chars) {
    u8 *teb = (u8 *)teb_v;
    u8 *peb = (u8 *)(u64b)rd64(teb + TEB_PEB_OFF);
    u8 *ldr = (u8 *)(u64b)rd64(peb + PEB_LDR_OFF);
    u8 *mod = (u8 *)nt_zalloc(MOD_SIZE);
    if (!ldr || !mod) return;

    // splice `mod` in before the list head (== insert at tail, since the
    // head's Blink is the current tail) for each of the 3 lists.
    for (int li = 0; li < 3; li++) {
        u64b off = li == 0 ? LDR_INLOADORDER_OFF : li == 1 ? LDR_INMEMORYORDER_OFF : LDR_INEINITORDER_OFF;
        u64b modoff = li == 0 ? MOD_INLOADORDER_OFF : li == 1 ? MOD_INMEMORYORDER_OFF : MOD_ININITORDER_OFF;
        u8 *head = ldr + off;
        u64b head_flink = rd64(head + 0);
        u64b old_tail = rd64(head + 8);   // head->Blink
        // new_node->Flink = head; new_node->Blink = old_tail
        wr64(mod + modoff + 0, (u64b)head);
        wr64(mod + modoff + 8, old_tail);
        // old_tail->Flink = new_node (old_tail's LIST_ENTRY is at old_tail
        // itself -- it's a raw LIST_ENTRY* into some node's modoff field)
        wr64((u8 *)old_tail + 0, (u64b)(mod + modoff));
        // head->Blink = new_node
        wr64(head + 8, (u64b)(mod + modoff));
        (void)head_flink;
    }

    wr64(mod + MOD_DLLBASE_OFF, (u64b)image_base);
    wr64(mod + MOD_ENTRYPOINT_OFF, (u64b)entry);
    wr32(mod + MOD_SIZEOFIMAGE_OFF, (u32b)image_size);
    if (name_utf16 && name_chars) {
        wr16(mod + MOD_BASEDLLNAME_OFF + US_LENGTH_OFF, (u16)(name_chars * 2));
        wr16(mod + MOD_BASEDLLNAME_OFF + US_MAXLENGTH_OFF, (u16)(name_chars * 2));
        wr64(mod + MOD_BASEDLLNAME_OFF + US_BUFFER_OFF, (u64b)name_utf16);
    }
}

// CHALICE C3c-2l Phase-A instrument: dump every node currently on
// PEB.Ldr.InLoadOrderModuleList (count + DllBase + SizeOfImage + raw
// BaseDllName char count) -- the direct receipt for "does the guest's own
// module list contain an ntdll.dll entry" before any fix is applied.
void nt_ldr_dump(void *teb_v) {
    u8 *teb = (u8 *)teb_v;
    u8 *peb = (u8 *)(u64b)rd64(teb + TEB_PEB_OFF);
    u8 *ldr = (u8 *)(u64b)rd64(peb + PEB_LDR_OFF);
    if (!ldr) { puts("CHALICE: ldr-dump: no Ldr\n"); return; }
    u8 *head = ldr + LDR_INLOADORDER_OFF;
    u64b cur = rd64(head + 0);
    int n = 0;
    puts("CHALICE: ldr-dump InLoadOrderModuleList:\n");
    while (cur != (u64b)head && n < 16) {
        u8 *mod = (u8 *)cur - MOD_INLOADORDER_OFF;
        u64b base = rd64(mod + MOD_DLLBASE_OFF);
        u64b sz   = rd64(mod + MOD_SIZEOFIMAGE_OFF) & 0xFFFFFFFFULL;
        u16 nlen = (u16)rd64(mod + MOD_BASEDLLNAME_OFF + US_LENGTH_OFF);
        u64b nbuf = rd64(mod + MOD_BASEDLLNAME_OFF + US_BUFFER_OFF);
        puts("  ["); nt_log_putdec((u64b)n); puts("] base="); puthex(base);
        puts(" size="); puthex(sz); puts(" namelen="); nt_log_putdec(nlen);
        if (nbuf && nlen) {
            puts(" name=");
            u16 *w = (u16 *)nbuf;
            for (int i = 0; i < nlen / 2 && i < 32; i++) putc((char)w[i]);
        }
        puts("\n");
        cur = rd64(mod + MOD_INLOADORDER_OFF + 0);
        n++;
    }
    puts("CHALICE: ldr-dump count="); nt_log_putdec((u64b)n); puts("\n");
}
