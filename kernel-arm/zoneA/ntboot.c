// CHALICE C3b Atom 3B — boot protocol (fake-ntdll dress rehearsal).
// nt_boot_process: Phase-0 emulation per SLICE3-SPEC. Maps ntdll+exe blobs
// via the 3A machinery (PeMapImage/nt_territory_alloc/nt_image_register,
// ntfile.c), wires the unixlib passthrough into the mapped ntdll's data
// exports, builds a 64K thread stack + extended TEB/PEB (graduates
// ntsvc.c's nt_build_teb, patches in the extra fields Atom 3B needs),
// pushes a synthetic CONTEXT (Architecture verdict 2 exact shape) and
// enters ntdll's LdrInitializeThunk via pe_enter1 (ntdisp.S). The booted
// chain's own NtTerminateProcess unwinds back here through the same
// nt_setjmp/nt_longjmp channel PeRun's use_nt=1 path already uses (proven
// slice-2/3A) -- no new unwind mechanism needed.
// LLP64 note: this file is aarch64-none-elf (LP64, kernel side) -- every
// `unsigned long` here is 8 bytes. PE-side payloads (chalice/fake-ntdll.c,
// chalice/hello5.c) are aarch64-windows-gnu (LLP64) and must use explicit
// `unsigned long long` for any pointer-target integer, per the established
// SCARS law.
#include "kernel.h"
#include "pemod.h"
#include "ntsvc.h"
#include "ntfile.h"
#include "ntboot.h"
#include "../chalice/ntids.h"

typedef unsigned char  u8;
typedef unsigned long  u64b;   // aarch64-none-elf LP64: `unsigned long` = 8B
typedef unsigned int   u32b;
typedef unsigned short u16b;

static void wr16(u8 *p, u16b v) { p[0] = (u8)v; p[1] = (u8)(v >> 8); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static u64b rd64(const u8 *p) { u64b v = 0; for (int i = 0; i < 8; i++) v |= (u64b)p[i] << (8 * i); return v; }

static u64b strlen_local(const char *s) { u64b n = 0; while (s[n]) n++; return n; }

static void putdec_signed(long v) {
    if (v < 0) { putc('-'); v = -v; }
    u64b u = (u64b)v;
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + (char)(u % 10); u /= 10; } while (u);
    puts(&b[i]);
}

static long boot_err(const char *msg) {
    puts("CHALICE: boot: "); puts(msg); puts("\n");
    return -1;
}

// CHALICE C3c Atom 3C-1: was manifest-only (nt_blob_manifest[] direct
// walk); now routes through ntfile.c's nt_blob_lookup_or_fs so stock
// ntdll.dll (too big to embed, lives on the RedSea-spirit disk -- fs.c/
// mkdisk.py) resolves the same way NtOpenFile's loader-path already does.
// Single mechanism, no parallel lookup (SLICE3-SPEC 3A precedent).
static int boot_blob_lookup(const char *name, const unsigned char **out_bytes, u64b *out_len) {
    const char *unused_name;
    return nt_blob_lookup_or_fs(name, out_bytes, out_len, &unused_name);
}

// =====================================================================
// unixlib passthrough (SLICE3-SPEC Atom 3B / Architecture verdict 3).
// nt_unix_call_shim IS what kernel writes into ntdll's *__wine_syscall_
// dispatcher; nt_unix_call_funcs (this array's address) IS what kernel
// writes into ntdll's *__wine_unixlib_handle. fake-ntdll.c's exported
// __wine_unix_call_dispatcher(handle, code, args) forwards straight to
// nt_unix_call_shim(handle, code, args) via that first write, closing the
// loop -- no TEB-slot indirection needed here (unlike the regular Nt*
// dispatch), this is a direct cross-toolchain C function-pointer call
// (same trick slice-1's IAT resolution already proved works: basic
// integer/pointer AAPCS is compatible between aarch64-windows-gnu callers
// and aarch64-none-elf kernel callees for the first 8 register args).
// =====================================================================
typedef long (*NtUnixCallFn)(void *args);

static long nt_unix_stub(void *args) { (void)args; return STATUS_NOT_IMPLEMENTED; }

// Mirrors Wine's real dbg_write_params shape: {const char *str; size_t len;}.
// PE side (fake-ntdll.c) declares the matching struct with an explicit
// `unsigned long long len` (LLP64 SCARS law) -- both sides 8B, natural
// layout matches without needing raw-offset paranoia (precedent:
// hello3.c/ntsvc.c's shared IO_STATUS_BLOCK struct-not-offsets approach).
struct nt_unix_dbg_write_args { const char *str; u64b len; };

static long nt_unix_dbg_write(void *args) {
    struct nt_unix_dbg_write_args *a = (struct nt_unix_dbg_write_args *)args;
    if (!a || !a->str) return STATUS_INVALID_PARAMETER;
    for (u64b i = 0; i < a->len; i++) putc(a->str[i]);
    return STATUS_SUCCESS;
}

// KUSER_SHARED_DATA.InterruptTime: kernel "IS the unix side" (HANDOFF) --
// write it fresh right here rather than a periodic-update task (nothing
// else in this dress rehearsal needs a live clock). Derived from the
// existing `ticks` heartbeat (task.c), scaled toward 100ns units (NT
// FILETIME/interrupt-time convention) -- not calibrated against real wall
// time (no RTC read here), just monotonic and nonzero, matching "KUSER-
// derived" without claiming clock-accurate semantics.
static long nt_unix_system_time_precise(void *args) {
    if (!args) return STATUS_INVALID_PARAMETER;
    u64b v = (u64b)ticks * 2500000ULL;   // ~ticks (250ms/tick, task.c) -> 100ns units
    u8 *k = (u8 *)(u64b)NT_KUSER_SHARED_DATA_ADDR;
    wr32(k + NT_KUSER_INTERRUPTTIME_OFF, (u32b)v);
    wr32(k + NT_KUSER_INTERRUPTTIME_OFF + 4, (u32b)(v >> 32));
    *(u64b *)args = rd64(k + NT_KUSER_INTERRUPTTIME_OFF);
    return STATUS_SUCCESS;
}

static NtUnixCallFn nt_unix_call_funcs[NT_UNIXCALL_COUNT];
static int nt_unix_call_funcs_inited;

static void nt_unix_call_funcs_ensure_init(void) {
    if (nt_unix_call_funcs_inited) return;
    nt_unix_call_funcs_inited = 1;
    for (int i = 0; i < NT_UNIXCALL_COUNT; i++) nt_unix_call_funcs[i] = nt_unix_stub;
    nt_unix_call_funcs[NT_UNIXCALL_DBG_WRITE]           = nt_unix_dbg_write;
    nt_unix_call_funcs[NT_UNIXCALL_SYSTEM_TIME_PRECISE] = nt_unix_system_time_precise;
}

// Wine signature (handle, code, args) -> table[code](args). `handle` here
// IS the table base pointer (single unixlib in this kernel, so "handle" ==
// &nt_unix_call_funcs, written verbatim into *__wine_unixlib_handle at
// wire time below) -- matches SLICE3-SPEC bullet 3 literally.
static long nt_unix_call_shim(void *handle, unsigned int code, void *args) {
    NtUnixCallFn *table = (NtUnixCallFn *)handle;
    if (!table || code >= NT_UNIXCALL_COUNT) return STATUS_NOT_IMPLEMENTED;
    NtUnixCallFn fn = table[code];
    if (!fn) return STATUS_NOT_IMPLEMENTED;
    return fn(args);
}

// =====================================================================
// TEB.NtTib / RTL_USER_PROCESS_PARAMETERS extra offsets (Atom 3B growth
// over ntsvc.c's nt_build_teb). NT_TIB64 (winternl.h): ExceptionList@0,
// StackBase@8, StackLimit@0x10, ..., Self@0x30 (ntsvc.c's existing
// TEB_NTTIB_SELF_OFF receipt cross-checks this layout). RTL_USER_PROCESS_
// PARAMETERS (rtltypes.h, well-known public offsets, StandardOutput@0x28
// already receipted in ntsvc.c): StandardInput@0x20, StandardError@0x30,
// CurrentDirectory(CURDIR: UNICODE_STRING+Handle)@0x38..0x50, DllPath
// UNICODE_STRING@0x50, ImagePathName UNICODE_STRING@0x60, CommandLine
// UNICODE_STRING@0x70, Environment ptr@0x80 -- fits PARAMS_SIZE 0x90
// (ntsvc.c, bumped this atom). UNICODE_STRING: Length(u16)@0,
// MaximumLength(u16)@2, Buffer(ptr,8-aligned)@8 (matches ntfile.c's
// US_LENGTH_OFF/US_BUFFER_OFF receipt).
// =====================================================================
#define TEB_NTTIB_STACKBASE_OFF    0x08
#define TEB_NTTIB_STACKLIMIT_OFF   0x10
#define TEB_PEB_OFF                0x60   // ntsvc.c receipt, duplicated (file-local const convention)
#define PEB_PROCESSPARAMETERS_OFF  0x20   // ntsvc.c receipt, duplicated
// CHALICE C3c-2h: PEB.ApiSetMap. Donor receipt (wine/include/winternl.h,
// reactos/sdk/include/wine/winternl.h -- both agree): field comment
// "ApiSetMap; /* 038/068 */" -- the TWO numbers are the 32-bit/64-bit
// build offsets of the SAME field, not two different fields. This kernel
// is LP64 (aarch64-none-elf) building an LP64 (aarch64-windows-gnu) PEB,
// i.e. the 64-bit branch -> 0x68, NOT 0x38 (that's the x86/WoW64 offset --
// verified against the donor before use, not assumed from any well-known-
// public-offset table). Cross-check: ntsvc.c's own PEB_PROCESSPARAMETERS_OFF
// (0x20/RTL_USER_PROCESS_PARAMETERS* @ "010/020") and its
// TlsExpansionCounter C_ASSERT anchor ("03c/070" -> 0x70) both follow the
// exact same 32-bit/64-bit dual-offset comment convention in the same
// donor header, one field after ApiSetMap -- consistent.
#define PEB_APISETMAP_OFF          0x68
#define PARAMS_STANDARDINPUT_OFF   0x20
#define PARAMS_STANDARDERROR_OFF   0x30
#define PARAMS_IMAGEPATHNAME_OFF   0x60
#define PARAMS_COMMANDLINE_OFF     0x70
#define US_LENGTH_OFF              0x00
#define US_MAXLENGTH_OFF           0x02
#define US_BUFFER_OFF              0x08

// ---- ARM64 CONTEXT offsets (ke.h _CONTEXT, LP64/_STRUCT64 -- full receipt
// lives in ntdisp.S's NtContinue comment; duplicated here, file-local
// const convention, same as every other struct-offset table in this
// codebase). X[i] @ 0x08 + i*8 (X0..X28,Fp=X29,Lr=X30); Sp@0x100; Pc@0x108;
// total size 0x390.
#define CTX_SIZE     0x390
#define CTX_X0_OFF   0x08
#define CTX_X1_OFF   0x10
#define CTX_X18_OFF  0x98
#define CTX_SP_OFF   0x100
#define CTX_PC_OFF   0x108

static void widen_ascii(const char *s, u16b *out, u64b n) {
    for (u64b i = 0; i < n; i++) out[i] = (u16b)(unsigned char)s[i];
}

// CHALICE C3c-2h: PEB.ApiSetMap wiring -- replicates wine's own unix-side
// load_apiset_dll (wine/dlls/ntdll/unix/loader.c:1726, donor receipt in
// HANDOFF) on our boot path. ntdll's import-name resolution
// (build_import_name, loader.c:756) reads PEB.ApiSetMap on EVERY api-ms-
// win-*/api-set import; left null, every such import resolves to
// STATUS_DLL_NOT_FOUND before any file is even opened -- confirmed live
// this session: kernel32.dll's own NtOpenFile fires, but no dependency's
// ever does, and ntdll cleanly reports "could not load kernel32.dll,
// status c0000135" and exits. Gated to stock=1 only by the caller (Atom
// 3B's fake-ntdll.c dress rehearsal never reads this PEB field).
static long nt_wire_apiset(u8 *peb) {
    const unsigned char *schema_bytes; u64b schema_len;
    if (!boot_blob_lookup("apisetschema.dll", &schema_bytes, &schema_len))
        return boot_err("apisetschema.dll blob not found");

    // .apiset's raw on-disk range -- apisetschema.dll is a data-only DLL
    // wine maps SEC_COMMIT flat (not SEC_IMAGE), so PointerToRawData is
    // already the right offset into the very same file-bytes buffer
    // boot_blob_lookup just returned (pe_find_raw_section receipt,
    // pemod.c/h) -- no PeMapImage, no separate copy or allocator call.
    unsigned long sec_off = 0, sec_size = 0;
    long rc = pe_find_raw_section(schema_bytes, (unsigned long)schema_len, ".apiset", &sec_off, &sec_size);
    if (rc != 0) return boot_err("apisetschema.dll: .apiset section not found");
    if (sec_size < 28) return boot_err("apisetschema.dll: .apiset section too small for API_SET_NAMESPACE header");

    // schema_bytes itself is a STABLE, persistent address: for the FS-
    // fallback path (the one stock=1 always takes -- apisetschema.dll is
    // never in the embedded manifest) it's a DLL-territory bump allocation
    // (ntfile.c's nt_blob_lookup_fs), never freed/overwritten for the
    // kernel's lifetime -- exactly the guarantee PEB.ApiSetMap needs
    // (read on every import for the process's whole life). Point straight
    // at the section's bytes inside it; no extra copy required.
    const u8 *map = schema_bytes + sec_off;
    u32b version = (u32b)map[0] | ((u32b)map[1] << 8) | ((u32b)map[2] << 16) | ((u32b)map[3] << 24);
    u32b mapsize  = (u32b)map[4] | ((u32b)map[5] << 8) | ((u32b)map[6] << 16) | ((u32b)map[7] << 24);
    // API_SET_NAMESPACE (winternl.h): Version@0, Size@4 (donor validation
    // this atom replicates: "map->Version==6 && map->Size<=VirtualSize").
    if (version != 6) return boot_err("apisetschema.dll: .apiset Version != 6");
    if ((u64b)mapsize > sec_size) return boot_err("apisetschema.dll: .apiset Size exceeds section VirtualSize");

    wr64(peb + PEB_APISETMAP_OFF, (u64b)map);
    puts("CHALICE: ApiSetMap wired addr="); puthex((u64b)map);
    puts(" size="); putdec_signed((long)mapsize); puts("\n");
    return 0;
}

#define NT_BOOT_STACK_SIZE 0x10000UL

long nt_boot_process(const char *exe_name, const char *ntdll_name, int stock, const char *label) {
    nt_blob_ensure_init();
    nt_territory_debug_stats(label);
    // CHALICE C-hello7: mark the bump pointer BEFORE this boot's own DLL
    // maps/heap negotiation/FS-blob fetches touch it -- released at the
    // bottom of this function (ntfile.c receipt: nt_territory_mark/
    // nt_territory_release_since), reclaiming this ENTIRE process's
    // private territory once it's run to completion (matches real Windows
    // process-exit teardown; this kernel has none otherwise, and 3 stock
    // boots back-to-back in one QEMU session was observed exhausting the
    // whole ~940MB territory on the 3rd boot without this).
    u64b nt_territory_boot_mark = nt_territory_mark();
    // CHALICE C4-8: HIGH pool gets the same per-boot wholesale-reset mark
    // (monad ruling #2 -- "per-boot wholesale reset covers BOTH pools").
    u64b nt_high_territory_boot_mark = nt_high_territory_mark();
    // CHALICE C4-9: RESERVE/COMMIT arena gets its own mark too (separate
    // VA space + tracking structures from LOW/HIGH -- ntfile.c receipt).
    u64b nt_arena_boot_mark = nt_arena_mark();

    const unsigned char *ntdll_bytes, *exe_bytes;
    u64b ntdll_len, exe_len;
    if (!boot_blob_lookup(ntdll_name, &ntdll_bytes, &ntdll_len)) return boot_err("ntdll blob not found");
    if (!boot_blob_lookup(exe_name, &exe_bytes, &exe_len))       return boot_err("exe blob not found");

    // ---- CHALICE C4-6: WOW64 signal, derived from the ALREADY-PARSED main
    // exe header (pe_query_image_info, pemod.c's is_pe32 path, C4-5) --
    // IRON law: no exe-name hardcode, this is the exact Machine field
    // pe32sec's own live proof already reads. Grounded gate (donor wine/
    // dlls/ntdll/loader.c:4487/4526, loader_init): "if (NtCurrentTeb()->
    // WowTebOffset) init_wow64(context);" is the ENTIRE native-ntdll-side
    // branch that decides whether a booted process takes the wow64cpu/
    // BTCpuSimulate path at all -- WowTebOffset is exactly the TEB64 field
    // nt_build_wow64_teb32 (ntsvc.c, C4-3) already knows how to wire (teb64+
    // 0x180c); this leg's whole job is plugging that helper into THIS real
    // boot TEB (below) instead of only the disconnected nt_boot_wow64_probe's
    // throwaway one.
    PeImageInfo exe_info;
    int wow64_mode = (pe_query_image_info(exe_bytes, exe_len, &exe_info) == 0 &&
                       exe_info.machine == 0x14C /* IMAGE_FILE_MACHINE_I386 */);

    // ---- Phase 0: map ntdll + exe via the 3A machinery (do_imports=0,
    // Architecture verdict 4: PE-side loader owns its own imports; there
    // is no Ldr walk here to resolve them against anyway). ----
    u64b ntdll_base = 0, ntdll_size = 0, ntdll_entry = 0;
    if (PeMapImage(ntdll_bytes, ntdll_len, "pe5-ntdll", nt_territory_alloc, 0, 1,
                    &ntdll_base, &ntdll_size, &ntdll_entry) != 0)
        return boot_err("ntdll map failed");
    nt_image_register(ntdll_name, ntdll_base, ntdll_size, ntdll_entry, 1);

    u64b exe_base = 0, exe_size = 0, exe_entry = 0;
    if (PeMapImage(exe_bytes, exe_len, "pe5-exe", nt_territory_alloc, 0, 1,
                    &exe_base, &exe_size, &exe_entry) != 0)
        return boot_err("exe map failed");
    nt_image_register(exe_name, exe_base, exe_size, exe_entry, 0);

    // ---- required ntdll exports (Architecture verdict 3 required-name
    // set; kernel only needs to LOOK UP the four it directly consumes --
    // the rest (KiUser*, DbgUiRemoteBreakin, __wine_ctrl_routine,
    // __wine_unix_call_dispatcher, LdrSystemDllInitBlock) just need to
    // EXIST in the mapped image so PE-side code that references them
    // resolves; nothing kernel-side calls them by address). Missing any
    // of the four -> clear serial error line, abort boot cleanly. ----
    u64b thunk_addr = 0, rtl_addr = 0, sdisp_addr = 0, uhandle_addr = 0, ucdisp_addr = 0;
    if (pe_find_named_export(ntdll_base, ntdll_size, "LdrInitializeThunk", &thunk_addr) != 0)
        return boot_err("ntdll missing export LdrInitializeThunk");
    if (pe_find_named_export(ntdll_base, ntdll_size, "RtlUserThreadStart", &rtl_addr) != 0)
        return boot_err("ntdll missing export RtlUserThreadStart");
    if (pe_find_named_export(ntdll_base, ntdll_size, "__wine_syscall_dispatcher", &sdisp_addr) != 0)
        return boot_err("ntdll missing export __wine_syscall_dispatcher");
    if (pe_find_named_export(ntdll_base, ntdll_size, "__wine_unixlib_handle", &uhandle_addr) != 0)
        return boot_err("ntdll missing export __wine_unixlib_handle");
    // CHALICE C3c-2k p2 diagnostic: cross-check our OWN kernel-side export
    // lookup against the exact export name the guest's find_named_export
    // (wine/dlls/ntdll/loader.c) is observed failing on ("ntdll.dll.
    // __wine_dbg_header", stub_entry_point abort). If this succeeds, the
    // export directory itself is intact in our mapped ntdll -- the guest-
    // side failure is a Wine loader-state bug, not a mapping/memory bug
    // on our end (disambiguates two very different next-gap fixes).
    if (stock) {
        u64b dbgh_addr = 0;
        long dbgh_rc = pe_find_named_export(ntdll_base, ntdll_size, "__wine_dbg_header", &dbgh_addr);
        puts("CHALICE: kernel-side __wine_dbg_header lookup rc="); putdec_signed(dbgh_rc);
        puts(" addr="); puthex(dbgh_addr); puts("\n");
    }
    // CHALICE C3c-2f p2: __wine_unix_call_dispatcher is a THIRD writable
    // data slot in STOCK ntdll (not a code export there -- confirmed via
    // `llvm-objdump -p stock/ntdll.dll`: its RVA 0x151750 sits directly
    // adjacent to __wine_syscall_dispatcher@0x151758 and
    // __wine_unixlib_handle@0x151760, a 3-pointer unixlib-wiring struct,
    // not 3 unrelated symbols) -- ONLY looked up/written when stock=1.
    // fake-ntdll.c (stock=0) exports this SAME NAME as a real CODE
    // function (its own hand-rolled forwarder, fake-ntdll.c:37) -- writing
    // through that address would overwrite live machine code, so the
    // lookup+write below is gated behind `stock` entirely, never touching
    // the stock=0 (pe5) path.
    if (stock && pe_find_named_export(ntdll_base, ntdll_size, "__wine_unix_call_dispatcher", &ucdisp_addr) != 0)
        return boot_err("ntdll missing export __wine_unix_call_dispatcher");

    // ---- unixlib wiring: pointer WRITES into the mapped ntdll's data
    // exports (Architecture verdict 3), no syscall. ----
    // CHALICE C3c-2d fix: *__wine_syscall_dispatcher is the REAL NT-service
    // dispatch slot (every Zw* stub's literal-pool target, verified against
    // stock/ntdll.dll -- ZwAllocateVirtualMemory's own literal resolves to
    // this exact export RVA) -- it must hold our x8-id table dispatcher
    // (nt_dispatch, ntdisp.S), NOT nt_unix_call_shim (a DIFFERENT donor
    // mechanism, (handle,code,args)-shaped, meant for *__wine_unixlib_handle
    // consumers only). Pre-fix this slot always got nt_unix_call_shim,
    // which happens to run harmlessly (STATUS_NOT_IMPLEMENTED, its own
    // ArgumentTable-less bounds check on a garbage "code") then `ret`s
    // straight back into the stub's own trailing `ret` -- the actual cause
    // of the ntdll.dll+0x64E24 self-loop (unrelated to the syscall-number/
    // x9 fix above, which never got a chance to run at all). fake-ntdll.c's
    // OWN hand-rolled __wine_unix_call_dispatcher (pe5/stock=0 dress
    // rehearsal only) still deliberately reads *__wine_syscall_dispatcher
    // as the unix-call shim by its own design (fake-ntdll.c comment), so
    // stock=0 keeps the old wiring unchanged -- only stock=1 (real Wine
    // ntdll, pe6) gets the fix.
    //
    // CHALICE C3c-2f p2: the C3c-2d fix above wired *__wine_syscall_dispatcher
    // correctly but left nt_unix_call_shim (built exactly for this
    // (handle,code,args) shape) with nowhere to plug in on the stock path
    // -- STOCK ntdll's own __wine_dbg_output (its always-on debug-channel
    // trace helper, called from inside RtlCreateAtomTable during heap init,
    // unconditional on any WINEDEBUG env var) calls straight through
    // *__wine_unix_call_dispatcher with x0=*__wine_unixlib_handle (our
    // nt_unix_call_funcs table, verified in the fault dump: x0=0x4021EA08
    // matches exactly), x1=code=2 (NT_UNIXCALL_DBG_WRITE), x2=argsptr --
    // this THIRD slot was never written, so it stayed zero (kernel BSS/
    // fresh page) and `blr x9` (x9=0) branched straight to address 0.
    // Fix: also write nt_unix_call_shim into *__wine_unix_call_dispatcher
    // when stock=1.
    nt_unix_call_funcs_ensure_init();
    wr64((u8 *)(u64b)sdisp_addr, stock ? (u64b)nt_dispatch : (u64b)nt_unix_call_shim);
    wr64((u8 *)(u64b)uhandle_addr, (u64b)nt_unix_call_funcs);
    if (stock) wr64((u8 *)(u64b)ucdisp_addr, (u64b)nt_unix_call_shim);
    if (stock) puts("CHALICE: stock ntdll mapped\n");

    // ---- 64K thread stack, DLL territory (same bump allocator as the DLL
    // images themselves -- SLICE3-SPEC: "alloc 64K thread stack in DLL
    // territory"). ----
    u64b stack_base = (u64b)nt_territory_alloc(NT_BOOT_STACK_SIZE);
    if (!stack_base) return boot_err("thread stack alloc failed");
    u64b stack_top = stack_base + NT_BOOT_STACK_SIZE;

    // ---- TEB/PEB floor: graduate ntsvc.c's nt_build_teb (today's
    // behavior: 4 trapdoors + PEB.ImageBaseAddress=image_base +
    // ProcessParameters.StandardOutput), then patch in Atom 3B's extras. ----
    u8 *teb = (u8 *)nt_build_teb((void *)exe_base, (void *)exe_entry, (unsigned long)exe_size);
    if (!teb) return boot_err("nt_build_teb failed");

    wr64(teb + TEB_NTTIB_STACKBASE_OFF, stack_top);     // StackBase = highest addr
    wr64(teb + TEB_NTTIB_STACKLIMIT_OFF, stack_base);   // StackLimit = lowest addr

    // ---- CHALICE C4-6: graft C4-3's WOW64 TEB32/PEB32/WOW64INFO/
    // CROSSPROCESSWORKLIST/CPURESERVED machinery onto THIS SAME real-boot
    // TEB (previously only reachable from nt_boot_wow64_probe's own
    // throwaway teb64) -- wires TEB64.WowTebOffset (0x180c) nonzero, the
    // exact signal loader_init's native ntdll reads to take the wow64
    // branch. native_machine=0xAA64 (this floor's host arch), emulated_
    // machine=0x14C (the only guest arch C4 targets) -- same pair the
    // probe already proved live. ----
    if (wow64_mode) {
        struct NtWow64Info wow64_wire;
        if (nt_build_wow64_teb32(teb, 0xAA64, 0x14C, &wow64_wire) != 0)
            return boot_err("wow64 teb32 wiring failed");
        nt_wow64_set_peb32(wow64_wire.peb32);
        puts("CHALICE: WOW64 signal wired teb64="); puthex((u64b)teb);
        puts(" WowTebOffset="); puthex((u64b)rd64(teb + 0x180c /* TEB64_WOWTEBOFFSET_OFF, ntsvc.c */));
        puts(" teb32="); puthex((u64b)wow64_wire.teb32);
        puts(" wow64info="); puthex((u64b)wow64_wire.wow64info);
        puts("\n");

        // CHALICE C4-9: x86 guest thread's initial Eip/Esp -- BTCpuGetContext
        // (wowbox64.dll+0x20e0) tail-calls NtQueryInformationThread(class 29)
        // for exactly this, inside wow64.dll's own thread_init (donor
        // syscall.c:1069) -- see ntsvc.c NtQueryInformationThread receipt for
        // the full fault analysis this fixes. A FRESH low-territory (<4GB,
        // 32-bit-ESP-addressable) stack, separate from stack_base/stack_top
        // above (that one is this boot's OWN ARM64 native thread stack,
        // still live on the call chain making THIS very syscall) -- Esp set
        // NT_BOOT_STACK_SIZE/4 below the block's top, generous headroom below
        // it for thread_init's own ctx_ptr write (sizeof(I386_CONTEXT)=0x2CC)
        // + 5 stack pushes (20 bytes), both far smaller than this margin.
        u64b x86_stack_base = (u64b)nt_territory_alloc(NT_BOOT_STACK_SIZE);
        if (!x86_stack_base) return boot_err("x86 guest stack alloc failed");
        u64b x86_stack_esp = x86_stack_base + NT_BOOT_STACK_SIZE - (NT_BOOT_STACK_SIZE / 4);
        nt_wow64_set_initial_context(exe_entry, x86_stack_esp);
        puts("CHALICE: x86 guest initial context eip="); puthex(exe_entry);
        puts(" esp="); puthex(x86_stack_esp); puts("\n");
    }

    // ---- CHALICE C3c-2l Phase A: nt_build_teb only ever registers ONE
    // self-entry (the exe) in PEB.Ldr -- ntdll itself (mapped directly by
    // this file's own C code above, bypassing the guest's own
    // NtCreateSection/NtMapViewOfSection dependency-load path kernel32/
    // kernelbase go through) never gets a module-list entry. Receipt dump
    // BEFORE the fix, then Phase B: register it, dump again after. ----
    if (stock) {
        puts("CHALICE: ldr state before ntdll self-registration:\n");
        nt_ldr_dump(teb);
        u64b ntdll_namelen = strlen_local(ntdll_name);
        u16b *ntdll_wname = (u16b *)MAlloc((ntdll_namelen + 1) * 2);
        if (!ntdll_wname) return boot_err("ntdll ldr name alloc failed");
        widen_ascii(ntdll_name, ntdll_wname, ntdll_namelen);
        nt_ldr_add_module(teb, (void *)ntdll_base, (void *)ntdll_entry, ntdll_size,
                          ntdll_wname, ntdll_namelen);
        puts("CHALICE: ldr state after ntdll self-registration:\n");
        nt_ldr_dump(teb);
    }

    u8 *peb    = (u8 *)(u64b)rd64(teb + TEB_PEB_OFF);
    u8 *params = (u8 *)(u64b)rd64(peb + PEB_PROCESSPARAMETERS_OFF);
    wr64(params + PARAMS_STANDARDINPUT_OFF, NT_STDOUT_HANDLE);
    wr64(params + PARAMS_STANDARDERROR_OFF, NT_STDOUT_HANDLE);

    u64b namelen = strlen_local(exe_name);
    u16b *wname = (u16b *)MAlloc((namelen + 1) * 2);
    if (!wname) return boot_err("command line alloc failed");
    widen_ascii(exe_name, wname, namelen);
    wr16(params + PARAMS_IMAGEPATHNAME_OFF + US_LENGTH_OFF, (u16b)(namelen * 2));
    wr16(params + PARAMS_IMAGEPATHNAME_OFF + US_MAXLENGTH_OFF, (u16b)(namelen * 2));
    wr64(params + PARAMS_IMAGEPATHNAME_OFF + US_BUFFER_OFF, (u64b)wname);
    wr16(params + PARAMS_COMMANDLINE_OFF + US_LENGTH_OFF, (u16b)(namelen * 2));
    wr16(params + PARAMS_COMMANDLINE_OFF + US_MAXLENGTH_OFF, (u16b)(namelen * 2));
    wr64(params + PARAMS_COMMANDLINE_OFF + US_BUFFER_OFF, (u64b)wname);

    // ---- CHALICE C4-6: ImagePathName needs a REAL DOS path (drive letter
    // + backslashes), wow64_mode only -- build_wow64_main_module (donor
    // loader.c ~4310) calls RtlDosPathNameToNtPathName_U_WithStatus on this
    // exact buffer; a bare relative name falls through to the
    // current-directory-relative branch (path.c RtlGetFullPathName_U),
    // which reads params->CurrentDirectory -- a field this floor never
    // populates (stays zeroed). An absolute "C:\..." path takes the
    // already-absolute branch instead (path.c: RtlGetFullPathName_U's own
    // early exit once a drive letter is present), never touching
    // CurrentDirectory -- same convention this file's own C4-4/C4-5
    // selftests already use for NtOpenFile ("\??\C:\windows\...").
    // CommandLine (just above) is left byte-identical -- pe8_gate.py
    // asserts its exact bare-name content, and nothing wow64-side reads
    // CommandLine this early. ----
    if (wow64_mode) {
        static const char dos_prefix[] = "C:\\windows\\";
        u64b prefixlen = strlen_local(dos_prefix);
        u64b fulllen = prefixlen + namelen;
        u16b *wfull = (u16b *)MAlloc((fulllen + 1) * 2);
        if (!wfull) return boot_err("wow64 ImagePathName alloc failed");
        widen_ascii(dos_prefix, wfull, prefixlen);
        widen_ascii(exe_name, wfull + prefixlen, namelen);
        wr16(params + PARAMS_IMAGEPATHNAME_OFF + US_LENGTH_OFF, (u16b)(fulllen * 2));
        wr16(params + PARAMS_IMAGEPATHNAME_OFF + US_MAXLENGTH_OFF, (u16b)(fulllen * 2));
        wr64(params + PARAMS_IMAGEPATHNAME_OFF + US_BUFFER_OFF, (u64b)wfull);
    }

    // ---- CHALICE C3c-2h: PEB.ApiSetMap, stock=1 only (Atom 3B's
    // fake-ntdll.c dress rehearsal never reads this field). Must land
    // before the thunk is entered -- ntdll's import-name resolution reads
    // it starting with kernel32.dll's own import walk, immediately after
    // "stock thunk enters". ----
    if (stock && nt_wire_apiset(peb) != 0) return -1;

    // ---- synthetic CONTEXT, pushed onto the new stack (16-aligned).
    // Architecture verdict 2 exact shape: X0=exe_entry, X1=arg(0), X18=teb,
    // Sp=stack_top, Pc=RtlUserThreadStart. ----
    u64b ctx_addr = (stack_top - CTX_SIZE) & ~0xFULL;
    u8 *ctx = (u8 *)ctx_addr;
    for (u64b i = 0; i < CTX_SIZE; i++) ctx[i] = 0;
    wr64(ctx + CTX_X0_OFF, exe_entry);
    wr64(ctx + CTX_X1_OFF, 0);
    wr64(ctx + CTX_X18_OFF, (u64b)teb);
    wr64(ctx + CTX_SP_OFF, stack_top);
    wr64(ctx + CTX_PC_OFF, rtl_addr);

    // ---- enter at LdrInitializeThunk (x0=&pushed_ctx, x18=teb). Runs on
    // the KERNEL's own C stack (pe_enter1, mirrors PeRun's pe_enter) until
    // LdrInitializeThunk itself calls NtContinue, which switches to
    // Context->Sp (the new 64K stack) and leaps to Context->Pc
    // (RtlUserThreadStart) -- never returns here on the success path.
    // NtTerminateProcess (called from deep inside the booted chain) longjmps
    // straight back to the nt_setjmp point below, same channel PeRun's
    // use_nt=1 path already proves (slice-2/3A). ----
    // ---- CHALICE C3c-2m: PROBE -- simulate the guest's OWN base-
    // discovery path (donor: wine/dlls/ntdll/loader.c build_ntdll_module,
    // quoted in HANDOFF) before entering the thunk. Wine's ntdll learns
    // ITS OWN module base by calling NtQueryVirtualMemory on an address it
    // already knows is inside itself (LdrInitializeThunk) and reading
    // meminfo.AllocationBase back -- every later find_named_export (incl.
    // KERNEL32.dll's import of ntdll.dll.__wine_dbg_header) keys off that
    // value. Reuse our OWN NtQueryVirtualMemory (ntfile.c) on thunk_addr,
    // the exact address the real loader queries, to see EXACTLY what the
    // guest sees -- no guessing, same tool, same input. ----
    if (stock) {
        unsigned char mbi[0x30];   // real Win64 sizeof(MEMORY_BASIC_INFORMATION), C3c-2m fix
        for (int i = 0; i < 0x30; i++) mbi[i] = 0;
        long qrc = NtQueryVirtualMemory(0, (void *)(u64b)thunk_addr, 0, mbi, sizeof(mbi), 0);
        u64b guest_base = rd64(mbi + 0x08 /* MBI_ALLOCATIONBASE_OFF */);
        int match = (guest_base == ntdll_base);
        u64b probe_addr = 0;
        long probe_rc = pe_find_named_export(guest_base, ntdll_size, "__wine_dbg_header", &probe_addr);
        puts("CHALICE PROBE: thunk="); puthex(thunk_addr);
        puts(" qrc="); putdec_signed(qrc);
        puts(" allocbase="); puthex(guest_base);
        puts(" ntdll_base="); puthex(ntdll_base);
        puts(" match="); putdec_signed(match);
        puts(" find_named_export_rc="); putdec_signed(probe_rc);
        puts(" addr="); puthex(probe_addr);
        puts("\n");
    }

    static unsigned long ntjmp[14];
    nt_exit_jmpbuf = ntjmp;
    long ret;
    if (nt_setjmp(ntjmp) == 0) {
        if (stock) puts("CHALICE: stock thunk enters\n");
        ret = pe_enter1((long (*)(void *))(u64b)thunk_addr, teb, (void *)ctx_addr);
    } else {
        ret = nt_exit_code;
    }
    nt_exit_jmpbuf = 0;

    nt_territory_release_since(nt_territory_boot_mark);
    nt_high_territory_release_since(nt_high_territory_boot_mark);   // CHALICE C4-8
    // CHALICE C-hello7 (continuation #3): drop this boot's OWN VM-arena
    // tracking-table slots too (ntsvc.c receipt) -- a separate fixed-size
    // bookkeeping array from the territory bump pointer that the bump reset
    // above does NOT touch; left unreset it fills up (64 slots) across a
    // few stock boots in one QEMU session even with hundreds of MB of raw
    // territory still free, since nt_vm_alloc fails on a full TRACKING
    // TABLE, not a full territory.
    nt_vm_regions_release_since((unsigned long)nt_territory_boot_mark);
    // CHALICE C4-9: RESERVE/COMMIT arena wholesale reset (own bump pointer,
    // reservation table, committed-window bitmap, L2-table pointer cache --
    // ntfile.c nt_arena_release_since receipt).
    nt_arena_release_since((unsigned long)nt_arena_boot_mark);
    nt_territory_debug_stats(label);
    puts(label); puts(" ret="); putdec_signed(ret); puts("\n");
    return ret;
}

// =====================================================================
// CHALICE C4-4: two-root FS live self-test helper (definitions; called
// from nt_boot_wow64_probe below). Builds a fresh OBJECT_ATTRIBUTES +
// UNICODE_STRING pair in DLL territory for one ASCII NT path, drives it
// through the REAL NtOpenFile -> NtCreateSection(SEC_IMAGE) ->
// NtQuerySection(SectionImageInformation) chain (ntfile.c, all
// already-proven services), and prints the resolved Machine field --
// 0xAA64 (ARM64, native/system32 root) vs 0x14C (i386, syswow64 root) is
// the concrete, unambiguous proof the redirect actually changed which
// on-disk tree got opened, not just that some string compare took a
// branch.
// =====================================================================
#define NT_SELFTEST_OA_OBJECTNAME_OFF 0x10
// CHALICE C4-4: raw Machine-field read (e_lfanew@0x3c -> +4 u16 LE), same
// tooling note as C4-SPEC's own recon ("llvm-objdump -f + a raw Machine-
// field read") -- NOT NtQuerySection/pe_query_image_info, which currently
// assumes PE32+/ARM64 only (same gate PeMapImage itself enforces,
// pemod.c) and would reject every i386 (PE32, Machine 0x14C) file with
// STATUS_INVALID_IMAGE_FORMAT regardless of which root correctly
// resolved it -- 32-bit PE header parsing is C4-5 territory, not yet
// implemented, so this proof reads the one field that's format-invariant
// (fixed byte offsets in the COFF header, same on PE32 and PE32+) via the
// already-proven NtReadFile, rather than depending on machinery that
// doesn't handle 32-bit PE yet.
#define NT_SELFTEST_HDR_LEN 0x200

static void nt_wow64_fs_selftest_one(const char *ascii_path, int wow64_active, const char *tag) {
    nt_wow64_set_active(wow64_active);

    u64b plen = strlen_local(ascii_path);
    u16b *wpath = (u16b *)nt_territory_alloc((plen + 1) * 2);
    widen_ascii(ascii_path, wpath, plen);

    u8 *us = nt_territory_alloc(0x10);
    wr16(us + US_LENGTH_OFF, (u16b)(plen * 2));
    wr16(us + US_MAXLENGTH_OFF, (u16b)(plen * 2));
    wr64(us + US_BUFFER_OFF, (u64b)wpath);

    u8 *oa = nt_territory_alloc(0x30);
    wr64(oa + NT_SELFTEST_OA_OBJECTNAME_OFF, (u64b)us);

    void *fh = 0;
    long orc = NtOpenFile(&fh, 0, oa, 0, 0, 0);
    puts("CHALICE: fstest "); puts(tag); puts(" wow64active="); putdec_signed(wow64_active);
    puts(" path="); puts(ascii_path); puts(" open_rc="); putdec_signed(orc);
    if (orc != 0) { puts("\n"); nt_wow64_set_active(0); return; }

    u8 *hdr = nt_territory_alloc(NT_SELFTEST_HDR_LEN);
    long rrc = NtReadFile(fh, 0, 0, 0, 0, hdr, NT_SELFTEST_HDR_LEN, 0, 0);
    if (rrc != 0) { puts(" read_rc="); putdec_signed(rrc); puts("\n"); nt_wow64_set_active(0); return; }

    u32b e_lfanew = (u32b)hdr[0x3c] | ((u32b)hdr[0x3d] << 8) | ((u32b)hdr[0x3e] << 16) | ((u32b)hdr[0x3f] << 24);
    u16b machine = 0;
    if (e_lfanew + 6 < NT_SELFTEST_HDR_LEN)
        machine = (u16b)((u32b)hdr[e_lfanew + 4] | ((u32b)hdr[e_lfanew + 5] << 8));
    puts(" machine="); puthex(machine); puts("\n");

    nt_wow64_set_active(0);
}

static void nt_wow64_fs_selftest(void) {
    nt_wow64_fs_selftest_one("\\??\\C:\\windows\\system32\\kernel32.dll", 0, "native-default");
    nt_wow64_fs_selftest_one("\\??\\C:\\windows\\system32\\kernel32.dll", 1, "wow64-redirect");
    nt_wow64_fs_selftest_one("\\??\\C:\\windows\\sysnative\\ntdll.dll", 1, "sysnative-escape");
    nt_wow64_fs_selftest_one("\\??\\C:\\windows\\syswow64\\kernelbase.dll", 0, "explicit-syswow64");
}

// =====================================================================
// CHALICE C4-5: PE32 section-path live proof -- the real gate this leg's
// pemod.c deltas exist for. Drives a genuine i386 (Machine 0x14C) blob
// through NtOpenFile -> NtCreateSection(SEC_IMAGE) -> NtQuerySection
// (SectionImageInformation, pe_query_image_info's is_pe32 path) ->
// NtMapViewOfSection (PeMapImage's is_pe32 path, incl. the new <4GB
// assert) -- all three already-proven kernel services, unmodified this
// leg; only pemod.c's header parser gained the is_pe32 branch. Prints
// Machine (from NtQuerySection, format-CORRECT now, not the raw-byte-read
// nt_wow64_fs_selftest workaround above) + mapped base/size/entry (from
// NtMapViewOfSection) -- concrete proof the whole SEC_IMAGE chain now
// round-trips a 32-bit image end to end.
// =====================================================================
static void nt_pe32_section_selftest_one(const char *ascii_path, int wow64_active, const char *tag) {
    nt_wow64_set_active(wow64_active);

    u64b plen = strlen_local(ascii_path);
    u16b *wpath = (u16b *)nt_territory_alloc((plen + 1) * 2);
    widen_ascii(ascii_path, wpath, plen);

    u8 *us = nt_territory_alloc(0x10);
    wr16(us + US_LENGTH_OFF, (u16b)(plen * 2));
    wr16(us + US_MAXLENGTH_OFF, (u16b)(plen * 2));
    wr64(us + US_BUFFER_OFF, (u64b)wpath);

    u8 *oa = nt_territory_alloc(0x30);
    wr64(oa + NT_SELFTEST_OA_OBJECTNAME_OFF, (u64b)us);

    void *fh = 0;
    long orc = NtOpenFile(&fh, 0, oa, 0, 0, 0);
    puts("CHALICE: pe32sec "); puts(tag); puts(" path="); puts(ascii_path);
    puts(" open_rc="); putdec_signed(orc);
    if (orc != 0) { puts("\n"); nt_wow64_set_active(0); return; }

    void *sh = 0;
    long crc = NtCreateSection(&sh, 0, 0, 0, 0, SEC_IMAGE, fh);
    puts(" create_rc="); putdec_signed(crc);
    if (crc != 0) { puts("\n"); nt_wow64_set_active(0); return; }

    u8 sii[0x40];
    unsigned long retlen = 0;
    long qrc = NtQuerySection(sh, 1 /* SectionImageInformation */, sii, sizeof(sii), &retlen);
    puts(" query_rc="); putdec_signed(qrc);
    if (qrc == 0) {
        u16b qmachine = (u16b)((u32b)sii[0x30] | ((u32b)sii[0x31] << 8));
        u64b transfer = rd64(sii + 0x00);
        puts(" Machine="); puthex(qmachine);
        puts(" TransferAddress="); puthex(transfer);
    }

    void *mapbase = 0;
    unsigned long viewsize = 0;
    long mrc = NtMapViewOfSection(sh, 0, &mapbase, 0, 0, 0, &viewsize, 0, 0, 0);
    puts(" map_rc="); putdec_signed(mrc);
    if (mrc == 0) {
        puts(" base="); puthex((u64b)mapbase);
        puts(" size="); putdec_signed((long)viewsize);
    }
    puts("\n");

    nt_wow64_set_active(0);
}

static void nt_pe32_section_selftest(void) {
    nt_pe32_section_selftest_one("\\??\\C:\\windows\\syswow64\\ntdll.dll", 0, "syswow64-ntdll");
    nt_pe32_section_selftest_one("\\??\\C:\\windows\\hello8_x86.exe", 0, "hello8-x86-exe");
}

// =====================================================================
// CHALICE C4-2 (C4-SPEC.md atom C4-2): native WOW64 image trio maps.
// Reuses boot_blob_lookup (nt_blob_lookup_or_fs, above) + PeMapImage +
// nt_territory_alloc + nt_image_register -- the exact same 3A machinery
// nt_boot_process's own ntdll/exe maps use, per HANDOFF ruling #2 ("floor
// deltas only, no parallel mechanism"). apply_relocs=1: like ntdll's own
// bootstrap map above, there is no Wine PE-side loader present yet to
// self-relocate these images (that's what wow64.dll itself will do to
// ITS OWN dependents once it runs -- these two images are the ones being
// bootstrapped here, symmetrical to how this file bootstraps ntdll+exe).
// do_imports=0: same Architecture verdict 4 as ntdll/exe -- imports are
// resolved by Wine's own loader machinery once code actually runs, not by
// this kernel-side probe (no guest code runs in this atom at all).
// =====================================================================
long nt_boot_wow64_probe(const char *cpu_backend_name, int call_process_init, const char *label) {
    nt_blob_ensure_init();
    u64b mark = nt_territory_mark();
    u64b high_mark = nt_high_territory_mark();   // CHALICE C4-8: same wholesale reset, both pools
    u64b arena_mark = nt_arena_mark();           // CHALICE C4-9: arena, same doctrine

    const unsigned char *wow64_bytes, *backend_bytes;
    u64b wow64_len, backend_len;
    if (!boot_blob_lookup("wow64.dll", &wow64_bytes, &wow64_len))
        return boot_err("wow64.dll blob not found (run fetch-stock-dlls.sh)");
    if (!boot_blob_lookup(cpu_backend_name, &backend_bytes, &backend_len))
        return boot_err("CPU backend blob not found (run fetch-stock-dlls.sh)");

    u64b wow64_base = 0, wow64_size = 0, wow64_entry = 0;
    if (PeMapImage(wow64_bytes, wow64_len, "pe9-wow64", nt_territory_alloc, 0, 1,
                    &wow64_base, &wow64_size, &wow64_entry) != 0)
        return boot_err("wow64.dll map failed");
    nt_image_register("wow64.dll", wow64_base, wow64_size, wow64_entry, 1);
    puts("CHALICE: wow64.dll mapped base="); puthex(wow64_base);
    puts(" size="); putdec_signed((long)wow64_size);
    puts(" entry="); puthex(wow64_entry); puts("\n");

    u64b backend_base = 0, backend_size = 0, backend_entry = 0;
    if (PeMapImage(backend_bytes, backend_len, "pe9-cpu", nt_territory_alloc, 0, 1,
                    &backend_base, &backend_size, &backend_entry) != 0)
        return boot_err("CPU backend map failed");
    nt_image_register(cpu_backend_name, backend_base, backend_size, backend_entry, 1);
    puts("CHALICE: CPU backend mapped name="); puts(cpu_backend_name);
    puts(" base="); puthex(backend_base);
    puts(" size="); putdec_signed((long)backend_size);
    puts(" entry="); puthex(backend_entry); puts("\n");

    // =================================================================
    // CHALICE C4-4 (C4-SPEC.md atom C4-4, first slice): BTCpu* export
    // resolution from the mapped CPU backend. wow64.dll's own process_init
    // (donor: dlls/wow64/syscall.c:1011-1032) resolves this EXACT list via
    // GetProcAddress-equivalent (RtlFindExportedRoutineByName) against the
    // loaded backend module, then calls BTCpuProcessInit() directly -- our
    // kernel does NOT call it (no guest code runs this atom, C4-2/3
    // doctrine unchanged); this slice proves the SAME resolution mechanism
    // (pe_find_named_export, already proven by the unixlib wiring in
    // nt_boot_process above) succeeds against the backend image we just
    // mapped, name-for-name, both backends. ----
    static const char *const btcpu_exports[] = {
        "BTCpuGetBopCode", "BTCpuGetContext", "BTCpuIsProcessorFeaturePresent",
        "BTCpuProcessInit" /* [3] */, "BTCpuThreadInit", "BTCpuResetToConsistentState",
        "BTCpuSetContext", "BTCpuSimulate" /* [7] */, "BTCpuFlushInstructionCache2",
        "BTCpuFlushInstructionCacheHeavy", "BTCpuNotifyMapViewOfSection",
        "BTCpuNotifyMemoryAlloc", "BTCpuNotifyMemoryDirty", "BTCpuNotifyMemoryFree",
        "BTCpuNotifyMemoryProtect", "BTCpuNotifyProcessExecuteFlagsChange",
        "BTCpuNotifyReadFile", "BTCpuNotifyUnmapViewOfSection",
        "BTCpuUpdateProcessorInformation", "BTCpuProcessTerm", "BTCpuThreadTerm",
        "__wine_get_unix_opcode",
    };
    #define BTCPU_EXPORT_COUNT (sizeof(btcpu_exports) / sizeof(btcpu_exports[0]))
    int btcpu_resolved = 0;
    u64b btcpu_process_init_addr = 0, btcpu_thread_init_addr = 0, btcpu_simulate_addr = 0;
    for (unsigned int i = 0; i < BTCPU_EXPORT_COUNT; i++) {
        u64b addr = 0;
        long rc = pe_find_named_export(backend_base, backend_size, btcpu_exports[i], &addr);
        if (rc == 0) {
            btcpu_resolved++;
            if (i == 3) btcpu_process_init_addr = addr;   // BTCpuProcessInit
            if (i == 4) btcpu_thread_init_addr = addr;    // BTCpuThreadInit
            if (i == 7) btcpu_simulate_addr = addr;       // BTCpuSimulate
        }
    }
    puts("CHALICE: BTCpu export scan name="); puts(cpu_backend_name);
    puts(" resolved="); putdec_signed(btcpu_resolved);
    puts("/"); putdec_signed((long)BTCPU_EXPORT_COUNT);
    puts(" ProcessInit="); puthex(btcpu_process_init_addr);
    puts(" Simulate="); puthex(btcpu_simulate_addr);
    puts("\n");
    if (btcpu_process_init_addr && btcpu_simulate_addr) {
        puts("CHALICE: BTCpu wired\n");
    } else {
        puts("CHALICE: BTCpu wiring FAULT -- ProcessInit or Simulate missing from ");
        puts(cpu_backend_name); puts("\n");
    }

    // =================================================================
    // CHALICE C4-4: two-root FS live self-test -- exercises the REAL
    // NtOpenFile dirname-redirect (nt_wow64_process_active flag, ntfile.c)
    // + NtCreateSection(SEC_IMAGE)/NtQuerySection(SectionImageInformation)
    // (both already-proven kernel services) to PROVE, not assert, that a
    // wow64-active "...\system32\X" open resolves the i386-windows
    // (Machine 0x14C) tree while "...\sysnative\X" always resolves native
    // ARM64 (Machine 0xAA64) regardless of the flag, and a non-wow64
    // "...\system32\X" open is UNCHANGED (still 0xAA64) -- concrete,
    // measured proof (HANDOFF "measure, don't guess"), no raw native-code
    // execution involved (unlike the opt-in BTCpuProcessInit call below).
    // =================================================================
    nt_wow64_fs_selftest();

    // CHALICE C4-5: PE32 (i386) section-path live proof -- see helper
    // comment above for the exact chain this exercises.
    nt_pe32_section_selftest();

    // =================================================================
    // CHALICE C4-3 (C4-SPEC.md atom C4-3): 32-bit TEB32/PEB32 floor.
    // Still no guest code runs (C4-2's own doctrine, unchanged this atom) --
    // build a throwaway 64-bit TEB/PEB (nt_build_teb, image args zeroed:
    // nothing reads PEB.ImageBaseAddress/the Ldr self-entry in a probe with
    // no entered guest), then graduate it with the WOW64 view
    // (nt_build_wow64_teb32, ntsvc.c) and dump every structure's address +
    // key field, becoming this leg's pe9_gate.py additions.
    // =================================================================
    void *teb64 = nt_build_teb(0, 0, 0);
    if (!teb64) return boot_err("wow64 probe: nt_build_teb failed");

    struct NtWow64Info wi;
    long wow64_teb_rc = nt_build_wow64_teb32(teb64, 0xAA64 /* IMAGE_FILE_MACHINE_ARM64, native */,
                                              0x14C /* IMAGE_FILE_MACHINE_I386, emulated */, &wi);
    if (wow64_teb_rc != 0) return boot_err("wow64 probe: nt_build_wow64_teb32 failed");

    puts("CHALICE: WOW64 teb32/peb32 built teb64="); puthex((u64b)teb64);
    puts(" teb32="); puthex((u64b)wi.teb32);
    puts(" peb32="); puthex((u64b)wi.peb32);
    puts(" wow64info="); puthex((u64b)wi.wow64info);
    puts("\n");
    puts("CHALICE: WOW64 crossproclist addr="); puthex((u64b)wi.crossproclist);
    puts(" size="); putdec_signed((long)wi.crossproclist_size);
    puts(" entries="); putdec_signed((long)wi.crossproclist_entries);
    puts(" cpureserved="); puthex((u64b)wi.cpureserved);
    puts("\n");

    // =================================================================
    // CHALICE C4-4 (second slice, this ruling): actually CALL
    // BTCpuProcessInit (+ThreadInit, donor init order -- dlls/wow64/
    // syscall.c process_init:1039 calls pBTCpuProcessInit() once per
    // process right after resolving every BTCpu*/wow64win export;
    // thread_init:1073 calls pBTCpuThreadInit() once per thread,
    // AFTER process_init has already run). Opt-in only (call_process_init
    // flag, new `pe9 <backend> call` shell arg) -- NEVER on the default
    // `pe9`/`pe9 fex` path the canonical pe9_gate.py drives, because this
    // is REAL ARM64 machine code from the mapped backend DLL, mapped with
    // do_imports=0 (Architecture verdict 4 -- imports are the PE-side
    // loader's job, and no such loader has run for this native-side image,
    // unlike ntdll/kernel32/kernelbase which self-relocate+self-import
    // once their OWN guest code starts executing): any import the backend
    // calls through resolves to an untouched raw thunk/zero, a real fault
    // is the expected likely outcome, not a bug in OUR mapping -- and this
    // kernel's sync_handler (kmain.c) has no recovery path for a raw CPU
    // exception (dumps then `for(;;) wfe` -- unlike guest-PE NT services,
    // there is no NtTerminateProcess/nt_longjmp unwind for kernel-side
    // native calls), so a fault here ends the QEMU session. Isolated to
    // its own opt-in gate run for exactly that reason -- HANDOFF measure-
    // don't-guess doctrine over hypothesizing the outcome.
    // =================================================================
    if (call_process_init) {
        if (!btcpu_process_init_addr) {
            puts("CHALICE: BTCpuProcessInit call SKIPPED -- address unresolved\n");
        } else {
            typedef void (*BTCpuVoidFn)(void);
            puts("CHALICE: calling BTCpuProcessInit addr="); puthex(btcpu_process_init_addr); puts("\n");
            ((BTCpuVoidFn)(u64b)btcpu_process_init_addr)();
            puts("CHALICE: BTCpuProcessInit returned -- no fault\n");
            if (btcpu_thread_init_addr) {
                puts("CHALICE: calling BTCpuThreadInit addr="); puthex(btcpu_thread_init_addr); puts("\n");
                ((BTCpuVoidFn)(u64b)btcpu_thread_init_addr)();
                puts("CHALICE: BTCpuThreadInit returned -- no fault\n");
            } else {
                puts("CHALICE: BTCpuThreadInit call SKIPPED -- address unresolved\n");
            }
        }
    }

    nt_territory_release_since(mark);
    nt_high_territory_release_since(high_mark);   // CHALICE C4-8
    nt_vm_regions_release_since((unsigned long)mark);
    nt_arena_release_since((unsigned long)arena_mark);   // CHALICE C4-9
    puts(label); puts(" ret=0\n");
    return 0;
}
