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

// ---- PE/COFF header field offsets (fixed by the file format itself, not
// data-derived — same status as struct-member offsets in a C ABI) ----
#define DOS_E_LFANEW        0x3C
#define COFF_HDR_SIZE       20            // after the "PE\0\0" signature
#define COFF_MACHINE        0x00
#define COFF_NSECTIONS      0x02
#define COFF_OPTHDR_SIZE    0x10
#define OPT_MAGIC           0x00
#define OPT_ENTRYPOINT_RVA  0x10
#define OPT_IMAGEBASE       0x18          // PE32+ only (magic 0x20B)
#define OPT_SIZEOFIMAGE     0x38
#define OPT_SIZEOFHEADERS   0x3C
#define OPT_NUMBER_OF_DIRS  0x6C
#define OPT_DATA_DIRS       0x70
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
#define IMAGE_FILE_MACHINE_ARM64 0xAA64
#define IMAGE_DIR_IMPORT         1
#define IMAGE_DIR_BASERELOC      5
#define IMAGE_REL_BASED_ABSOLUTE 0
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

long PeRun(const unsigned char *img, unsigned long lenL, const char *tag) {
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
    if (machine != IMAGE_FILE_MACHINE_ARM64) return pe_err(tag, "COFF machine is not ARM64 (0xAA64)");

    u16b nsections   = rd16(coff + COFF_NSECTIONS);
    u16b opthdr_size = rd16(coff + COFF_OPTHDR_SIZE);
    const u8 *opt = coff + COFF_HDR_SIZE;
    if ((u64b)e_lfanew + 4 + COFF_HDR_SIZE + opthdr_size > len)
        return pe_err(tag, "optional header out of file bounds");
    if (opthdr_size < OPT_DATA_DIRS) return pe_err(tag, "optional header too small (no data dirs)");

    u16b magic = rd16(opt + OPT_MAGIC);
    if (magic != PE32PLUS_MAGIC) return pe_err(tag, "not PE32+ (expected 64-bit optional header)");

    u32b entry_rva     = rd32(opt + OPT_ENTRYPOINT_RVA);
    u64b image_base    = rd64(opt + OPT_IMAGEBASE);
    u32b size_of_image = rd32(opt + OPT_SIZEOFIMAGE);
    u32b size_of_hdrs  = rd32(opt + OPT_SIZEOFHEADERS);
    u32b num_dirs      = rd32(opt + OPT_NUMBER_OF_DIRS);
    const u8 *dirs      = opt + OPT_DATA_DIRS;

    if (entry_rva >= size_of_image) return pe_err(tag, "entry point RVA outside image");

    // ---- allocate SizeOfImage RWX region, copy headers + sections ----
    // MAlloc has no alignment guarantee (its header is 16 bytes, so returned
    // pointers commonly land at base+0x10, not page-aligned). ARM64 PE code
    // uses adrp (page-relative) addressing for data/import references, which
    // is only load-address-invariant when the whole image starts on a 4096
    // byte boundary (adrp computes Align(PC,4096)+imm*4096 against the true
    // runtime PC, not an alloc-relative one). Over-allocate by one page and
    // round up so the image itself is page-aligned.
    u8 *alloc_raw = (u8 *)MAlloc((u64b)size_of_image + 4096);
    if (!alloc_raw) return pe_err(tag, "MAlloc failed");
    u8 *alloc = (u8 *)(((u64b)alloc_raw + 4095) & ~(u64b)4095);
    for (u32b i = 0; i < size_of_image; i++) alloc[i] = 0;

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

    // ---- base relocations ----
    if (num_dirs > IMAGE_DIR_BASERELOC) {
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
                    if (type != IMAGE_REL_BASED_DIR64)
                        return pe_err(tag, "unsupported base relocation type (only DIR64 handled)");
                    u8 *target = alloc + page_rva + pgoff;
                    u64b val = rd64(target);
                    wr64(target, val + (u64b)delta);
                }
                off += block_sz;
            }
        }
    }

    // ---- imports ----
    if (num_dirs > IMAGE_DIR_IMPORT) {
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

    long (*entry)(void) = (long (*)(void))(alloc + entry_rva);
    long ret = entry();

    puts(tag); puts(" ret="); putdec_signed(ret); puts("\n");
    return ret;
}
