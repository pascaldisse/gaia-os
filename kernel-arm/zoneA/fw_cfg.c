// GAIA-OS Zone A — QEMU fw_cfg (MMIO) + ramfb framebuffer bring-up.
// Successor spirit to ZealOS VBE linear-FB draw, on QEMU virt aarch64.
// Guest allocates the framebuffer in its own RAM, then hands QEMU the
// surface geometry via the "etc/ramfb" fw_cfg file (all fields BIG-ENDIAN).
// Ref: qemu.org/docs/master/specs/fw_cfg.html  +  hw/display/ramfb.c
#include "kernel.h"

// ---- fw_cfg MMIO register file (DT: fw-cfg@9020000, reg len 0x18) ----
#define FWCFG_BASE 0x09020000UL
#define FWCFG_DATA (FWCFG_BASE + 0x00)   // 8B data port (unused here, DMA path)
#define FWCFG_SEL  (FWCFG_BASE + 0x08)   // 2B selector (BE)
#define FWCFG_DMA  (FWCFG_BASE + 0x10)   // 8B DMA address (BE)

#define FW_CFG_FILE_DIR 0x0019

// FWCfgDMAAccess.control bits
#define DMA_ERROR  0x01u
#define DMA_READ   0x02u
#define DMA_SKIP   0x04u
#define DMA_SELECT 0x08u
#define DMA_WRITE  0x10u

// DRM_FORMAT_XRGB8888, little-endian fourcc numeric value 'XR24'
#define RAMFB_FOURCC 0x34325258u

static void putdec(u64 v) {
    char b[21]; int i = 20; b[i] = 0;
    do { b[--i] = '0' + v % 10; v /= 10; } while (v);
    puts(&b[i]);
}

static inline u32 be32(u32 x) { return __builtin_bswap32(x); }
static inline u16 be16(u16 x) { return __builtin_bswap16(x); }
static inline u64 be64(u64 x) { return __builtin_bswap64(x); }

struct FWCfgDMAAccess {
    u32 control;   // BE
    u32 length;    // BE
    u64 address;   // BE
} __attribute__((packed));

struct FWCfgFile {
    u32 size;      // BE
    u16 select;    // BE
    u16 reserved;
    char name[56];
} __attribute__((packed));

struct QemuRamFBCfg {
    u64 addr;      // BE  guest-physical framebuffer base
    u32 fourcc;    // BE
    u32 flags;     // BE
    u32 width;     // BE
    u32 height;    // BE
    u32 stride;    // BE
} __attribute__((packed));

static volatile struct FWCfgDMAAccess g_dma __attribute__((aligned(16)));

// Run one fw_cfg DMA op and spin until QEMU clears the control word.
static void fw_dma(u32 control, u32 length, void *addr) {
    g_dma.control = be32(control);
    g_dma.length  = be32(length);
    g_dma.address = be64((u64)addr);
    asm volatile("dsb sy" ::: "memory");
    *(volatile u64 *)FWCFG_DMA = be64((u64)&g_dma);   // 8B BE write triggers
    asm volatile("dsb sy" ::: "memory");
    u32 c;
    do {
        asm volatile("dsb sy" ::: "memory");
        c = be32(g_dma.control);
    } while (c & ~DMA_ERROR);      // done when control==0 (or ERROR bit only)
}

// Locate a fw_cfg file by name; returns its selector key or 0 if absent.
static u16 fw_find(const char *want) {
    u32 count_be = 0;
    fw_dma((FW_CFG_FILE_DIR << 16) | DMA_SELECT | DMA_READ, 4, &count_be);
    u32 count = be32(count_be);
    for (u32 i = 0; i < count; i++) {
        struct FWCfgFile f;
        fw_dma(DMA_READ, sizeof f, &f);   // continues at current offset
        const char *a = f.name, *b = want;
        while (*a && *a == *b) { a++; b++; }
        if (*a == 0 && *b == 0) return be16(f.select);
    }
    return 0;
}

// ---- framebuffer (dims parameterized in kernel.h, no scattered magics) ----
static u32 *fb;
static const u32 fb_stride_px = FB_WIDTH;              // pixels per row

static void fill_rect(u32 x0, u32 y0, u32 w, u32 h, u32 rgb) {
    for (u32 y = y0; y < y0 + h && y < FB_HEIGHT; y++) {
        u32 *row = fb + (u64)y * fb_stride_px;
        for (u32 x = x0; x < x0 + w && x < FB_WIDTH; x++) row[x] = rgb;
    }
}

// gradient wash + 3 solid color bars + white 1px border (corner-accurate).
void FbDrawPattern(void) {
    if (!fb) return;
    for (u32 y = 0; y < FB_HEIGHT; y++) {
        u32 *row = fb + (u64)y * fb_stride_px;
        u32 g = y * 255u / (FB_HEIGHT - 1);
        for (u32 x = 0; x < FB_WIDTH; x++) {
            u32 r = x * 255u / (FB_WIDTH - 1);
            row[x] = (r << 16) | (g << 8);            // XRGB8888: 0x00RRGGBB
        }
    }
    fill_rect(50, 100, FB_WIDTH - 100, 40, 0x00FF0000);  // red bar  @ y=120
    fill_rect(50, 200, FB_WIDTH - 100, 40, 0x0000FF00);  // green bar @ y=220
    fill_rect(50, 300, FB_WIDTH - 100, 40, 0x000000FF);  // blue bar  @ y=320
    for (u32 x = 0; x < FB_WIDTH; x++) {
        fb[x] = 0x00FFFFFF;
        fb[(u64)(FB_HEIGHT - 1) * fb_stride_px + x] = 0x00FFFFFF;
    }
    for (u32 y = 0; y < FB_HEIGHT; y++) {
        u32 *row = fb + (u64)y * fb_stride_px;
        row[0] = 0x00FFFFFF;
        row[FB_WIDTH - 1] = 0x00FFFFFF;
    }
    asm volatile("dsb sy" ::: "memory");
}

// Exported to Shrine/zoneC HolyC modules by name (zcmod.c resolver). The
// GaiaPlot module imports these to draw directly on the ramfb surface.
long FbBase(void)  { return (long)fb; }
long FbWidth(void) { return FB_WIDTH; }

int FbInit(void) {
    u16 key = fw_find("etc/ramfb");
    if (!key) { puts("ramfb: etc/ramfb absent (need -device ramfb)\n"); return -1; }

    u64 bytes = (u64)FB_WIDTH * FB_HEIGHT * FB_BPP;
    fb = (u32 *)MAlloc(bytes);
    if (!fb) { puts("ramfb: framebuffer alloc failed\n"); return -1; }

    struct QemuRamFBCfg cfg;
    cfg.addr   = be64((u64)fb);
    cfg.fourcc = be32(RAMFB_FOURCC);
    cfg.flags  = be32(0);
    cfg.width  = be32(FB_WIDTH);
    cfg.height = be32(FB_HEIGHT);
    cfg.stride = be32(FB_WIDTH * FB_BPP);
    fw_dma(((u32)key << 16) | DMA_SELECT | DMA_WRITE, sizeof cfg, &cfg);

    // "ramfb: 640x480 @ <addr>"  (dims parameterized, no scattered magics)
    puts("ramfb: "); putdec(FB_WIDTH); puts("x"); putdec(FB_HEIGHT);
    puts(" @ "); puthex((u64)fb); puts("\n");
    return 0;
}
