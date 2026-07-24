// CHALICE C1 slice 1 — aarch64 PE loader.
// Parses a PE32+/ARM64 image entirely from its own headers (DOS -> COFF ->
// optional header -> data directories -> section table); no offset or size
// is assumed beyond what the PE/COFF spec fixes as the header layout itself.
// Mirrors zcmod.c's pattern: RWX copy, patch (relocs + imports), icache
// flush, call. Import resolution reuses zc_resolve() (KPutS -> zc_kputs),
// the same table zcmod.c's AOT module uses.
#include "kernel.h"
#include "zcmod.h"
#include "pemod.h"
#include "ntsvc.h"

typedef unsigned char  u8;
typedef unsigned short u16b;
typedef unsigned int   u32b;
typedef unsigned long long u64b;

// ---- little-endian field readers/writers (PE is always LE) ----
static u16b rd16(const u8 *p) { return (u16b)p[0] | ((u16b)p[1] << 8); }
static u32b rd32(const u8 *p) {
    return (u32b)p[0] | ((u32b)p[1] << 8) | ((u32b)p[2] << 16) | ((u32b)p[3] << 24);
}
static u64b rd64(const u8 *p) { return (u64b)rd32(p) | ((u64b)rd32(p + 4) << 32); }
static void wr64(u8 *p, u64b v) { for (int i = 0; i < 8; i++) p[i] = (u8)(v >> (8 * i)); }
static void wr32(u8 *p, u32b v) { for (int i = 0; i < 4; i++) p[i] = (u8)(v >> (8 * i)); }

// ---- PE/COFF header field offsets (fixed by the file format itself, not
// data-derived — same status as struct-member offsets in a C ABI) ----
#define DOS_E_LFANEW        0x3C
#define COFF_HDR_SIZE       20            // after the "PE\0\0" signature
#define COFF_MACHINE        0x00
#define COFF_NSECTIONS      0x02
#define COFF_OPTHDR_SIZE    0x10
#define COFF_CHARACTERISTICS 0x12         // CHALICE C3c-2i: FileHeader.Characteristics
#define OPT_MAGIC           0x00
#define OPT_SIZEOFCODE      0x04          // CHALICE C3c-2i: OptionalHeader.SizeOfCode
#define OPT_ENTRYPOINT_RVA  0x10
#define OPT_IMAGEBASE       0x18          // PE32+ only (magic 0x20B)
#define OPT_MAJOROSVER      0x28          // CHALICE C3c-2i
#define OPT_MINOROSVER      0x2A          // CHALICE C3c-2i
#define OPT_MAJORSUBSYSVER  0x30          // CHALICE C3c-2i
#define OPT_MINORSUBSYSVER  0x32          // CHALICE C3c-2i
#define OPT_SIZEOFIMAGE     0x38
#define OPT_SIZEOFHEADERS   0x3C
#define OPT_SUBSYSTEM       0x44          // CHALICE C3c-2i
#define OPT_DLLCHARACTERISTICS 0x46       // CHALICE C3c-2i
#define OPT_STACKRESERVE    0x48          // CHALICE C3c-2i
#define OPT_STACKCOMMIT     0x50          // CHALICE C3c-2i
#define OPT_NUMBER_OF_DIRS  0x6C
#define OPT_DATA_DIRS       0x70
#define IMAGE_DIR_COM_DESCRIPTOR 14        // CHALICE C3c-2i: CLR/COM+ header dir index
#define SEC_CHARACTERISTICS 36             // CHALICE C3c-2i: section header Characteristics
#define IMAGE_SCN_MEM_EXECUTE 0x20000000L  // CHALICE C3c-2i
#define SEC_ENTRY_SIZE      40
#define SEC_VIRTUAL_SIZE    8
#define SEC_VIRTUAL_ADDR    12
#define SEC_RAW_SIZE        16
#define SEC_RAW_PTR         20
#define IMPDESC_ENTRY_SIZE  20
#define IMPDESC_OFT         0             // OriginalFirstThunk
#define IMPDESC_NAME        12
#define IMPDESC_FT          16            // FirstThunk

#define PE32PLUS_MAGIC          0x20B
// CHALICE C4-5: PE32 (i386 guest) support alongside PE32+ (ARM64 host).
// Donor: wine/include/winnt.h IMAGE_OPTIONAL_HEADER32 (cited at each use
// below) -- 32-bit optional header drops BaseOfData's PE32+ absence and
// narrows ImageBase/StackReserve/StackCommit to 4-byte fields, which
// shifts ONLY those three field offsets (SizeOfImage/SizeOfHeaders/
// Subsystem/etc. land at IDENTICAL offsets on both magics -- verified
// field-by-field against the donor struct, existing OPT_* macros above
// are reused unchanged for those).
#define PE32_MAGIC              0x10B
#define OPT_IMAGEBASE32         0x1C   // winnt.h:3062 (DWORD, after BaseOfData@0x18)
#define OPT_STACKRESERVE32      0x48   // winnt.h:3077 (DWORD, not QWORD)
#define OPT_STACKCOMMIT32       0x4C   // winnt.h:3078 (DWORD, not QWORD)
#define OPT_NUMBER_OF_DIRS32    0x5C   // winnt.h:3082 (NumberOfRvaAndSizes)
#define OPT_DATA_DIRS32         0x60   // winnt.h:3083 (DataDirectory[])
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define IMAGE_FILE_MACHINE_I386  0x14C
#define IMAGE_DIR_IMPORT         1
#define IMAGE_DIR_BASERELOC      5
#define IMAGE_REL_BASED_ABSOLUTE 0
#define IMAGE_REL_BASED_HIGHLOW  3
#define IMAGE_REL_BASED_DIR64    10

static long pe_err(const char *tag, const char *msg) {
    puts(tag); puts(": error: "); puts(msg); puts("\n");
    return -1;
}

static void putdec_signed(long v) {
    if (v < 0) { putc('-'); v = -v; }
    u64b u = (u64b)v;
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + (char)(u % 10); u /= 10; } while (u);
    puts(&b[i]);
}

// CHALICE C3a Atom 3A: PeRun's original heap allocator, graduated into a
// PeAllocFn so PeMapImage can be shared between PeRun (exe, this alloc) and
// NtMapViewOfSection (ntfile.c, DLL-territory bump alloc instead).
static unsigned char *pe_heap_alloc(unsigned long size_of_image) {
    u8 *alloc_raw = (u8 *)MAlloc((u64b)size_of_image + 4096);
    if (!alloc_raw) return 0;
    u8 *alloc = (u8 *)(((u64b)alloc_raw + 4095) & ~(u64b)4095);
    for (u64b i = 0; i < size_of_image; i++) alloc[i] = 0;
    return alloc;
}

// ---- CHALICE C3a Atom 3A: shared PE-mapping core (header parse, section
// copy, DIR64 relocs; imports iff do_imports). Graduated out of slice-1's
// PeRun so NtMapViewOfSection (ntfile.c) can reuse the exact same,
// already-proven parse/reloc logic over a different allocator + skipping
// imports entirely (SLICE3-SPEC verdict 4: DLL imports are the PE-side
// loader's own job, never kernel-side). `allocfn` must return a ZEROED,
// page-aligned buffer of >= size_of_image bytes (see pe_heap_alloc above /
// ntfile.c's nt_territory_alloc for the two allocators in this codebase).
long PeMapImage(const unsigned char *img, unsigned long lenL, const char *tag, PeAllocFn allocfn,
                 int do_imports, int apply_relocs, unsigned long *out_base, unsigned long *out_size,
                 unsigned long *out_entry) {
    u64b len = (u64b)lenL;

    if (len < DOS_E_LFANEW + 4) return pe_err(tag, "file too small for DOS header");
    if (img[0] != 'M' || img[1] != 'Z') return pe_err(tag, "missing MZ signature");

    u32b e_lfanew = rd32(img + DOS_E_LFANEW);
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE > len) return pe_err(tag, "e_lfanew out of file bounds");

    const u8 *pe = img + e_lfanew;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0)
        return pe_err(tag, "missing PE\\0\\0 signature");

    const u8 *coff = pe + 4;
    u16b machine = rd16(coff + COFF_MACHINE);
    if (machine != IMAGE_FILE_MACHINE_ARM64 && machine != IMAGE_FILE_MACHINE_I386)
        return pe_err(tag, "COFF machine not ARM64 (0xAA64) or i386 (0x14C)");

    u16b nsections   = rd16(coff + COFF_NSECTIONS);
    u16b opthdr_size = rd16(coff + COFF_OPTHDR_SIZE);
    const u8 *opt = coff + COFF_HDR_SIZE;
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE + opthdr_size > len)
        return pe_err(tag, "optional header out of file bounds");
    if (opthdr_size < OPT_DATA_DIRS32) return pe_err(tag, "optional header too small (no data dirs)");

    // CHALICE C4-5: accept PE32 (0x10B, i386 guest) alongside PE32+ (0x20B,
    // ARM64 host) -- magic/machine must agree (donor: is_valid_binary-class
    // gate, both fields describe the SAME image, never mixed).
    u16b magic = rd16(opt + OPT_MAGIC);
    int is_pe32 = (magic == PE32_MAGIC);
    if (magic != PE32PLUS_MAGIC && !is_pe32)
        return pe_err(tag, "optional header magic not PE32+ (0x20B) or PE32 (0x10B)");
    if (is_pe32 && machine != IMAGE_FILE_MACHINE_I386)
        return pe_err(tag, "PE32 optional header on non-i386 machine");
    if (!is_pe32 && machine != IMAGE_FILE_MACHINE_ARM64)
        return pe_err(tag, "PE32+ optional header on non-ARM64 machine");
    if (!is_pe32 && opthdr_size < OPT_DATA_DIRS) return pe_err(tag, "optional header too small (no data dirs)");

    u32b entry_rva     = rd32(opt + OPT_ENTRYPOINT_RVA);
    u64b image_base    = is_pe32 ? (u64b)rd32(opt + OPT_IMAGEBASE32) : rd64(opt + OPT_IMAGEBASE);
    u32b size_of_image = rd32(opt + OPT_SIZEOFIMAGE);
    u32b size_of_hdrs  = rd32(opt + OPT_SIZEOFHEADERS);
    u32b num_dirs      = rd32(opt + (is_pe32 ? OPT_NUMBER_OF_DIRS32 : OPT_NUMBER_OF_DIRS));
    const u8 *dirs      = opt + (is_pe32 ? OPT_DATA_DIRS32 : OPT_DATA_DIRS);

    if (entry_rva >= size_of_image) return pe_err(tag, "entry point RVA outside image");

    // ---- allocate SizeOfImage region (zeroed, page-aligned -- allocfn's
    // contract), copy headers + sections ----
    u8 *alloc = allocfn(size_of_image);
    if (!alloc) return pe_err(tag, "image allocator failed");

    // CHALICE C4-5 atom: 32-bit (PE32) images MUST land below 4GB -- the
    // guest's own 32-bit pointers/CONTEXT can't address higher. Territory
    // allocator (nt_territory_alloc, ntfile.c: 0x45000000..0x7ff00000) is
    // already well under 4GB, but assert it explicitly rather than trust
    // that invariant silently -- legible CHALICE FAULT if it ever isn't.
    if (is_pe32 && ((u64b)(u64)alloc + (u64b)size_of_image) > 0x100000000ULL)
        return pe_err(tag, "CHALICE FAULT: PE32 image mapped at/above 4GB boundary (WOW64 requires <4GB)");

    u32b hcopy = size_of_hdrs < len ? size_of_hdrs : (u32b)len;
    if (hcopy > size_of_image) hcopy = size_of_image;
    for (u32b i = 0; i < hcopy; i++) alloc[i] = img[i];

    const u8 *sectab = opt + opthdr_size;
    if ((u64b)(sectab - img) + (u64b)nsections * SEC_ENTRY_SIZE > len)
        return pe_err(tag, "section table out of file bounds");

    for (u16b s = 0; s < nsections; s++) {
        const u8 *sh = sectab + (u32b)s * SEC_ENTRY_SIZE;
        u32b vsize   = rd32(sh + SEC_VIRTUAL_SIZE);
        u32b vaddr   = rd32(sh + SEC_VIRTUAL_ADDR);
        u32b rawsize = rd32(sh + SEC_RAW_SIZE);
        u32b rawptr  = rd32(sh + SEC_RAW_PTR);
        if ((u64b)vaddr + vsize > size_of_image) return pe_err(tag, "section VA+VSize exceeds SizeOfImage");
        u32b copy = rawsize < vsize ? rawsize : vsize;
        if (rawptr && copy) {
            if ((u64b)rawptr + copy > len) return pe_err(tag, "section raw data out of file bounds");
            for (u32b i = 0; i < copy; i++) alloc[vaddr + i] = img[rawptr + i];
        }
        for (u32b i = copy; i < vsize; i++) alloc[vaddr + i] = 0;  // VirtualSize > SizeOfRawData tail
    }

    long delta = (long)((u64b)(u64)alloc - image_base);

    // CHALICE C3c-2q: relocations are applied here ONLY when apply_relocs
    // is set. PROVEN (live PEMAP instrument, this atom): for DLLs mapped
    // via NtMapViewOfSection, kernel-side relocation here is CORRECT and
    // SINGLE at map time, but the stock Wine-built guest ntdll.dll's own
    // PE-side loader (dlls/ntdll/loader.c build_module -> perform_relocations,
    // donor source line ~2281) unconditionally re-relocates ANY module not
    // mapped at its own OptionalHeader.ImageBase (`if (module == base)
    // return STATUS_SUCCESS;` -- gated on the ADDRESS, not on our
    // NtMapViewOfSection's return status) -- real Wine's Unix-side
    // NtMapViewOfSection never relocates SEC_IMAGE maps at all (see
    // wine/dlls/ntdll/unix/virtual.c: virtual_relocate_module is called
    // only from the two special ntdll/apiset self-bootstrap paths in
    // unix/loader.c, load_ntdll/load_apiset_dll -- NOT from the normal
    // build_module path every other DLL takes), so applying relocations
    // kernel-side there duplicates the guest's own unconditional pass ->
    // root_key_names[]-class pointers end up at file+2*delta. Fix:
    // NtMapViewOfSection (ntfile.c) now passes apply_relocs=0, leaving
    // relocation entirely to the guest's perform_relocations, matching
    // real Wine architecture. PeRun (slice-1 standalone exes, no Wine
    // loader present) and ntboot.c's direct ntdll/exe Phase-0 maps (own
    // bootstrap, mirrors unix/loader.c's load_ntdll -- ntdll can't
    // relocate itself before it can run) keep apply_relocs=1.
    if (apply_relocs && num_dirs > IMAGE_DIR_BASERELOC) {
        u32b rrva = rd32(dirs + IMAGE_DIR_BASERELOC * 8);
        u32b rsz  = rd32(dirs + IMAGE_DIR_BASERELOC * 8 + 4);
        if (rrva && rsz) {
            if ((u64b)rrva + rsz > size_of_image) return pe_err(tag, ".reloc directory outside image");
            u32b off = 0;
            while (off < rsz) {
                const u8 *blk = alloc + rrva + off;
                u32b page_rva = rd32(blk);
                u32b block_sz = rd32(blk + 4);
                if (block_sz < 8 || off + block_sz > rsz) return pe_err(tag, "malformed reloc block");
                u32b nentries = (block_sz - 8) / 2;
                for (u32b i = 0; i < nentries; i++) {
                    u16b entry = rd16(blk + 8 + i * 2);
                    u16b type  = entry >> 12;
                    u16b pgoff = entry & 0xFFF;
                    if (type == IMAGE_REL_BASED_ABSOLUTE) continue;   // padding, skip
                    u8 *target = alloc + page_rva + pgoff;
                    if (type == IMAGE_REL_BASED_DIR64) {
                        u64b val = rd64(target);
                        wr64(target, val + (u64b)delta);
                    } else if (type == IMAGE_REL_BASED_HIGHLOW) {
                        // CHALICE C4-5: PE32 (i386) 32-bit base relocation
                        // (donor winnt.h IMAGE_REL_BASED_HIGHLOW=3) -- same
                        // apply_relocs semantics as DIR64 (HANDOFF scar #6:
                        // guest self-relocates SEC_IMAGE maps, kernel-side
                        // apply_relocs=1 is only for ntboot.c's own
                        // Phase-0 bootstrap/PeRun standalone-exe paths).
                        u32b val = rd32(target);
                        wr32(target, val + (u32b)delta);
                    } else {
                        return pe_err(tag, "unsupported base relocation type (only DIR64/HIGHLOW handled)");
                    }
                }
                off += block_sz;
            }
        }
    }

    // ---- imports (skipped entirely for do_imports=0 -- Atom 3A DLL path,
    // "NO import resolution", SLICE3-SPEC verdict 4) ----
    if (do_imports && num_dirs > IMAGE_DIR_IMPORT) {
        u32b irva = rd32(dirs + IMAGE_DIR_IMPORT * 8);
        u32b isz  = rd32(dirs + IMAGE_DIR_IMPORT * 8 + 4);
        if (irva && isz) {
            u8 *desc = alloc + irva;
            for (;;) {
                if ((u64b)(desc - alloc) + IMPDESC_ENTRY_SIZE > size_of_image)
                    return pe_err(tag, "import descriptor table runs past image");
                u32b orig_thunk  = rd32(desc + IMPDESC_OFT);
                u32b name_rva    = rd32(desc + IMPDESC_NAME);
                u32b first_thunk = rd32(desc + IMPDESC_FT);
                if (!orig_thunk && !name_rva && !first_thunk) break;   // null terminator

                u32b thunk_rva = orig_thunk ? orig_thunk : first_thunk;
                u8 *thunk = alloc + thunk_rva;
                u8 *iat   = alloc + first_thunk;
                for (u32b j = 0; ; j++) {
                    u64b tval = rd64(thunk + j * 8);
                    if (!tval) break;
                    if (tval & (1ULL << 63)) return pe_err(tag, "ordinal imports unsupported");
                    u32b name_off = (u32b)tval;                       // RVA of IMAGE_IMPORT_BY_NAME
                    const char *fname = (const char *)(alloc + name_off + 2);  // skip Hint(u16)
                    long addr = zc_resolve(fname);
                    if (!addr) {
                        puts(tag); puts(": error: unresolved import: "); puts(fname); puts("\n");
                        return -1;
                    }
                    wr64(iat + j * 8, (u64b)addr);
                }
                desc += IMPDESC_ENTRY_SIZE;
            }
        }
    }

    iflush(alloc, (int)size_of_image);

    *out_base  = (u64b)alloc;
    *out_size  = size_of_image;
    *out_entry = (u64b)alloc + entry_rva;
    return 0;
}

// CHALICE C3a Atom 3A export parser (pemod growth). See pemod.h for the
// return-code contract. Standard PE/COFF IMAGE_EXPORT_DIRECTORY walk:
// AddressOfNames[i] -> name string; AddressOfNameOrdinals[i] -> biased
// ordinal (already a direct 0-based index into AddressOfFunctions, per
// spec -- no Base subtraction needed, verified against a real zig-cc-built
// export table during recon); AddressOfFunctions[ord] -> function RVA.
// Forwarder = a function RVA that lands inside the export directory itself
// (points at a string like "OTHERDLL.Func" instead of code) -- detected,
// not chased (SLICE3-SPEC: "return distinct error for now").
long pe_find_named_export(unsigned long baseL, unsigned long size_of_imageL,
                           const char *name, unsigned long *out_addr) {
    const u8 *base = (const u8 *)(u64b)baseL;
    (void)size_of_imageL;
    u32b e_lfanew = rd32(base + DOS_E_LFANEW);
    const u8 *pe = base + e_lfanew;
    const u8 *coff = pe + 4;
    const u8 *opt = coff + COFF_HDR_SIZE;
    // CHALICE C4-36: same is_pe32 dispatch PeMapImage already implements
    // (pemod.c:149-164) -- pe_find_named_export previously hardcoded the
    // PE32+ (0x20B) data-directory offsets unconditionally, misreading a
    // genuine PE32 (0x10B) image's NumberOfRvaAndSizes/DataDirectory[]
    // fields (real fields sit at 0x5C/0x60 for PE32, not 0x6C/0x70) --
    // for syswow64/ntdll.dll (PE32) this always read num_dirs=0 -> early
    // return -3 before the export table was ever consulted -> caller's
    // *out_addr (LdrInitializeThunk) stayed 0. See HANDOFF.md/FEX-MAP.md
    // C4-35/C4-36.
    u16b magic = rd16(opt + OPT_MAGIC);
    int is_pe32 = (magic == PE32_MAGIC);
    u32b num_dirs = rd32(opt + (is_pe32 ? OPT_NUMBER_OF_DIRS32 : OPT_NUMBER_OF_DIRS));
    if (num_dirs < 1) return -3;
    const u8 *dirs = opt + (is_pe32 ? OPT_DATA_DIRS32 : OPT_DATA_DIRS);
    u32b exp_rva  = rd32(dirs + 0);
    u32b exp_size = rd32(dirs + 4);
    if (!exp_rva) return -3;
    const u8 *exp = base + exp_rva;
    u32b nnames        = rd32(exp + 24);
    u32b addr_functions = rd32(exp + 28);
    u32b addr_names     = rd32(exp + 32);
    u32b addr_ordinals  = rd32(exp + 36);
    for (u32b i = 0; i < nnames; i++) {
        u32b name_rva = rd32(base + addr_names + i * 4);
        const char *cand = (const char *)(base + name_rva);
        const char *a = cand, *b = name;
        while (*a && *a == *b) { a++; b++; }
        if (*a != *b) continue;
        u16b ord = rd16(base + addr_ordinals + i * 2);
        u32b func_rva = rd32(base + addr_functions + (u32b)ord * 4);
        if (func_rva >= exp_rva && func_rva < exp_rva + exp_size) return -2;   // forwarder, unsupported
        *out_addr = (u64b)base + func_rva;
        return 0;
    }
    return -1;
}

// CHALICE C4-66: ordinal-form sibling of pe_find_named_export, above --
// same header dispatch (is_pe32 magic check, same data-dir offsets), but
// resolves via the export directory's own Base/NumberOfFunctions fields
// (AddressOfFunctions[ordinal - Base]) instead of the name table, for
// IAT thunks that import by ordinal (raw thunk value's bit31 set) rather
// than by name.
long pe_find_export_by_ordinal(unsigned long baseL, unsigned long size_of_imageL,
                                unsigned long ordinalL, unsigned long *out_addr) {
    const u8 *base = (const u8 *)(u64b)baseL;
    (void)size_of_imageL;
    u32b e_lfanew = rd32(base + DOS_E_LFANEW);
    const u8 *pe = base + e_lfanew;
    const u8 *coff = pe + 4;
    const u8 *opt = coff + COFF_HDR_SIZE;
    u16b magic = rd16(opt + OPT_MAGIC);
    int is_pe32 = (magic == PE32_MAGIC);
    u32b num_dirs = rd32(opt + (is_pe32 ? OPT_NUMBER_OF_DIRS32 : OPT_NUMBER_OF_DIRS));
    if (num_dirs < 1) return -3;
    const u8 *dirs = opt + (is_pe32 ? OPT_DATA_DIRS32 : OPT_DATA_DIRS);
    u32b exp_rva  = rd32(dirs + 0);
    u32b exp_size = rd32(dirs + 4);
    if (!exp_rva) return -3;
    const u8 *exp = base + exp_rva;
    u32b ord_base       = rd32(exp + 16);   // Base
    u32b nfuncs          = rd32(exp + 20);  // NumberOfFunctions
    u32b addr_functions = rd32(exp + 28);
    u32b ordinal = (u32b)ordinalL;
    if (ordinal < ord_base) return -1;
    u32b idx = ordinal - ord_base;
    if (idx >= nfuncs) return -1;
    u32b func_rva = rd32(base + addr_functions + idx * 4);
    if (!func_rva) return -1;
    if (func_rva >= exp_rva && func_rva < exp_rva + exp_size) return -2;   // forwarder, unsupported
    *out_addr = (u64b)base + func_rva;
    return 0;
}

// CHALICE C3c-2h: raw (unmapped) PE section finder -- locates a section by
// its 8-byte on-disk Name field directly in the FILE bytes (no PeMapImage,
// no relocation, no allocator). Matches wine's own load_apiset_dll
// (wine/dlls/ntdll/unix/loader.c:1726) receipt exactly: apisetschema.dll is
// mapped SEC_COMMIT flat there (not SEC_IMAGE), so a section's on-disk
// PointerToRawData IS its offset in that flat mapping too -- no VA/RVA
// translation needed, we can read straight out of the same file-bytes
// buffer ntfile.c's blob lookup already returned. *out_off/*out_size give
// the section's raw byte range, capped to both SizeOfRawData and
// VirtualSize and to the bytes actually available in the file (mirrors
// the donor's own `size - PointerToRawData >= VirtualSize` guard, done as
// a clamp instead of a hard reject so a short/truncated tail can't read
// past the buffer). Returns: 0 = found (out_off/out_size valid), -1 = not
// found or malformed header, -2 = PointerToRawData outside the file.
long pe_find_raw_section(const unsigned char *img, unsigned long lenL, const char *name8,
                          unsigned long *out_off, unsigned long *out_size) {
    u64b len = (u64b)lenL;
    if (len < DOS_E_LFANEW + 4) return -1;
    if (img[0] != 'M' || img[1] != 'Z') return -1;
    u32b e_lfanew = rd32(img + DOS_E_LFANEW);
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE > len) return -1;
    const u8 *pe = img + e_lfanew;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) return -1;
    const u8 *coff = pe + 4;
    u16b nsections   = rd16(coff + COFF_NSECTIONS);
    u16b opthdr_size = rd16(coff + COFF_OPTHDR_SIZE);
    const u8 *opt = coff + COFF_HDR_SIZE;
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE + opthdr_size > len) return -1;
    const u8 *sectab = opt + opthdr_size;
    if ((u64b)(sectab - img) + (u64b)nsections * SEC_ENTRY_SIZE > len) return -1;

    for (u16b s = 0; s < nsections; s++) {
        const u8 *sh = sectab + (u32b)s * SEC_ENTRY_SIZE;
        int match = 1;
        for (int i = 0; i < 8; i++) if (sh[i] != (u8)name8[i]) { match = 0; break; }
        if (!match) continue;

        u32b vsize   = rd32(sh + SEC_VIRTUAL_SIZE);
        u32b rawsize = rd32(sh + SEC_RAW_SIZE);
        u32b rawptr  = rd32(sh + SEC_RAW_PTR);
        if (!rawptr || (u64b)rawptr >= len) return -2;

        u64b avail = len - rawptr;
        u32b size = rawsize < vsize ? rawsize : vsize;
        if ((u64b)size > avail) size = (u32b)avail;
        *out_off  = rawptr;
        *out_size = size;
        return 0;
    }
    return -1;
}

// CHALICE C3c-2i: NtQuerySection(SectionImageInformation)'s backing parser.
// See pemod.h for the field-semantics receipt (donor cross-refs). Zeros
// *out first so any field this PE lacks room for (short optional header,
// etc.) reads as 0 rather than stale caller memory.
long pe_query_image_info(const unsigned char *img, unsigned long lenL, PeImageInfo *out) {
    u64b len = (u64b)lenL;
    if (!out) return -1;
    for (u64b i = 0; i < sizeof(*out); i++) ((u8 *)out)[i] = 0;

    if (len < DOS_E_LFANEW + 4) return -1;
    if (img[0] != 'M' || img[1] != 'Z') return -1;

    u32b e_lfanew = rd32(img + DOS_E_LFANEW);
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE > len) return -1;

    const u8 *pe = img + e_lfanew;
    if (pe[0] != 'P' || pe[1] != 'E' || pe[2] != 0 || pe[3] != 0) return -1;

    const u8 *coff = pe + 4;
    u16b machine        = rd16(coff + COFF_MACHINE);
    u16b nsections      = rd16(coff + COFF_NSECTIONS);
    u16b opthdr_size    = rd16(coff + COFF_OPTHDR_SIZE);
    u16b characteristics = rd16(coff + COFF_CHARACTERISTICS);
    const u8 *opt = coff + COFF_HDR_SIZE;
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE + opthdr_size > len) return -1;
    if (opthdr_size < OPT_DATA_DIRS32) return -1;

    // CHALICE C4-5: PE32 (i386) alongside PE32+ (ARM64) -- same is_pe32
    // gate/field-width split as PeMapImage above.
    u16b magic = rd16(opt + OPT_MAGIC);
    int is_pe32 = (magic == PE32_MAGIC);
    if (magic != PE32PLUS_MAGIC && !is_pe32) return -1;
    if (is_pe32 && machine != IMAGE_FILE_MACHINE_I386) return -1;
    if (!is_pe32 && machine != IMAGE_FILE_MACHINE_ARM64) return -1;
    if (!is_pe32 && opthdr_size < OPT_DATA_DIRS) return -1;

    u32b size_of_code  = rd32(opt + OPT_SIZEOFCODE);
    u32b entry_rva     = rd32(opt + OPT_ENTRYPOINT_RVA);
    u64b image_base    = is_pe32 ? (u64b)rd32(opt + OPT_IMAGEBASE32) : rd64(opt + OPT_IMAGEBASE);
    u16b os_major       = rd16(opt + OPT_MAJOROSVER);
    u16b os_minor       = rd16(opt + OPT_MINOROSVER);
    u16b subsys_major   = rd16(opt + OPT_MAJORSUBSYSVER);
    u16b subsys_minor   = rd16(opt + OPT_MINORSUBSYSVER);
    u16b subsystem      = rd16(opt + OPT_SUBSYSTEM);
    u16b dll_char       = rd16(opt + OPT_DLLCHARACTERISTICS);
    u64b stack_reserve  = is_pe32 ? (u64b)rd32(opt + OPT_STACKRESERVE32) : rd64(opt + OPT_STACKRESERVE);
    u64b stack_commit   = is_pe32 ? (u64b)rd32(opt + OPT_STACKCOMMIT32) : rd64(opt + OPT_STACKCOMMIT);
    u32b num_dirs       = rd32(opt + (is_pe32 ? OPT_NUMBER_OF_DIRS32 : OPT_NUMBER_OF_DIRS));
    const u8 *dirs       = opt + (is_pe32 ? OPT_DATA_DIRS32 : OPT_DATA_DIRS);

    // ---- loader_flags: 1 iff a non-empty COM_DESCRIPTOR (CLR) data dir is
    // present (server/mapping.c: `mapping->image.loader_flags = !!clr_dir`) ----
    int has_clr = 0;
    if (num_dirs > IMAGE_DIR_COM_DESCRIPTOR) {
        const u8 *d = dirs + (u64b)IMAGE_DIR_COM_DESCRIPTOR * 8;
        if ((u64b)(d - img) + 8 <= len) {
            u32b vaddr = rd32(d);
            u32b vsize = rd32(d + 4);
            has_clr = (vaddr && vsize);
        }
    }

    // ---- contains_code: SizeOfCode!=0 OR any section IMAGE_SCN_MEM_EXECUTE
    // (server/mapping.c's own two-part test) ----
    int contains_code = size_of_code != 0;
    if (!contains_code) {
        const u8 *sectab = opt + opthdr_size;
        if ((u64b)(sectab - img) + (u64b)nsections * SEC_ENTRY_SIZE <= len) {
            for (u16b s = 0; s < nsections && !contains_code; s++) {
                const u8 *sh = sectab + (u32b)s * SEC_ENTRY_SIZE;
                u32b sec_char = rd32(sh + SEC_CHARACTERISTICS);
                if (sec_char & IMAGE_SCN_MEM_EXECUTE) contains_code = 1;
            }
        }
    }

    out->machine               = machine;
    out->subsystem              = subsystem;
    out->subsystem_major        = subsys_major;
    out->subsystem_minor        = subsys_minor;
    out->os_major                = os_major;
    out->os_minor                = os_minor;
    out->image_characteristics  = characteristics;
    out->dll_characteristics    = dll_char;
    out->image_base              = (unsigned long)image_base;
    out->entry_point_rva         = (unsigned long)entry_rva;
    out->stack_reserve           = (unsigned long)stack_reserve;
    out->stack_commit            = (unsigned long)stack_commit;
    out->loader_flags            = has_clr ? 1 : 0;
    out->file_size                = (unsigned long)len;
    out->contains_code           = (unsigned char)(contains_code ? 1 : 0);
    return 0;
}

long PeRun(const unsigned char *img, unsigned long lenL, const char *tag, int use_nt) {
    // NOTE: `unsigned long` here, not this file's own `u64b` (=`unsigned
    // long long` in pemod.c's local typedefs) -- PeMapImage's out-params are
    // `unsigned long *` (matches the kernel-wide LP64 convention ntsvc.c/
    // ntfile.c use; both are 8 bytes here regardless, but C treats them as
    // distinct pointer types).
    unsigned long base = 0, size = 0, entry_addr = 0;
    if (PeMapImage(img, lenL, tag, pe_heap_alloc, 1, 1, &base, &size, &entry_addr) != 0) return -1;

    long (*entry)(void) = (long (*)(void))(u64b)entry_addr;
    long ret;
    if (use_nt) {
        // CHALICE C2a Atom A: build the TEB/PEB floor (ntsvc.c, receipts
        // there) and enter via pe_enter (x18=TEB) instead of a bare call --
        // NtTerminateProcess can unwind straight back here through
        // nt_setjmp/nt_longjmp, mirroring how a normal `return` from entry
        // already flows into `ret` below (SLICE2-SPEC A3 receipt).
        void *teb = nt_build_teb((void *)(u64b)base, (void *)entry, (unsigned long)size);
        if (!teb) return pe_err(tag, "nt_build_teb: MAlloc failed");
        static unsigned long ntjmp[14];
        nt_exit_jmpbuf = ntjmp;
        if (nt_setjmp(ntjmp) == 0) {
            ret = pe_enter(entry, teb);
        } else {
            ret = nt_exit_code;   // NtTerminateProcess longjmp'd here
        }
        nt_exit_jmpbuf = 0;
    } else {
        ret = entry();
    }

    puts(tag); puts(" ret="); putdec_signed(ret); puts("\n");
    return ret;
}
