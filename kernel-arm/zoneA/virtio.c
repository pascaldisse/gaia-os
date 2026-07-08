// GAIA-OS Zone A/B — virtio-mmio (modern, v2) + virtio-blk, polled.
// Successor to ZealOS BlkDev/: one clean bus, no AHCI archaeology.
#include "kernel.h"

#define VIRTIO_BASE   0x0a000000UL
#define VIRTIO_STRIDE 0x200
#define VIRTIO_SLOTS  32

// mmio regs
#define R_MAGIC   0x00
#define R_VERSION 0x04
#define R_DEVID   0x08
#define R_DFEATSEL 0x14
#define R_DFEAT   0x10
#define R_GFEATSEL 0x24
#define R_GFEAT   0x20
#define R_QSEL    0x30
#define R_QMAX    0x34
#define R_QNUM    0x38
#define R_QREADY  0x44
#define R_QNOTIFY 0x50
#define R_STATUS  0x70
#define R_QDESCLO 0x80
#define R_QDESCHI 0x84
#define R_QAVLO   0x90
#define R_QAVHI   0x94
#define R_QUSEDLO 0xa0
#define R_QUSEDHI 0xa4
#define R_CONFIG  0x100

#define ST_ACK 1
#define ST_DRIVER 2
#define ST_DRIVER_OK 4
#define ST_FEATURES_OK 8

#define QSZ 8

typedef struct { u64 addr; u32 len; u16 flags; u16 next; } VqDesc;
#define F_NEXT 1
#define F_WRITE 2

static struct {
    VqDesc desc[QSZ];                          // 128 B
    struct { u16 flags, idx; u16 ring[QSZ]; } avail;
    unsigned char pad[192 - 128 - 20];
    struct { u16 flags, idx;
             struct { u32 id, len; } ring[QSZ]; } used;
} __attribute__((aligned(4096))) vq;

static u64 blk_base;
static u16 last_used;

static struct { u32 type, reserved; u64 sector; } __attribute__((aligned(16))) req_hdr;
static volatile unsigned char req_status;

#define REG(b, o) MMIO32((b) + (o))

u64 BlkInit(void) {                            // returns capacity in sectors, 0 if none
    for (int i = 0; i < VIRTIO_SLOTS; i++) {
        u64 b = VIRTIO_BASE + (u64)i * VIRTIO_STRIDE;
        if (REG(b, R_MAGIC) != 0x74726976 || REG(b, R_DEVID) != 2) continue;
        if (REG(b, R_VERSION) != 2) { puts("virtio-blk: legacy dev skipped (need force-legacy=false)\n"); continue; }
        blk_base = b;

        REG(b, R_STATUS) = 0;
        REG(b, R_STATUS) = ST_ACK;
        REG(b, R_STATUS) = ST_ACK | ST_DRIVER;
        REG(b, R_GFEATSEL) = 1;                // accept only VIRTIO_F_VERSION_1 (bit 32)
        REG(b, R_GFEAT) = 1;
        REG(b, R_GFEATSEL) = 0;
        REG(b, R_GFEAT) = 0;
        REG(b, R_STATUS) = ST_ACK | ST_DRIVER | ST_FEATURES_OK;
        if (!(REG(b, R_STATUS) & ST_FEATURES_OK)) { puts("virtio-blk: features rejected\n"); return 0; }

        REG(b, R_QSEL) = 0;
        REG(b, R_QNUM) = QSZ;
        REG(b, R_QDESCLO) = (u32)(u64)&vq.desc;   REG(b, R_QDESCHI) = (u32)((u64)&vq.desc >> 32);
        REG(b, R_QAVLO)   = (u32)(u64)&vq.avail;  REG(b, R_QAVHI)   = (u32)((u64)&vq.avail >> 32);
        REG(b, R_QUSEDLO) = (u32)(u64)&vq.used;   REG(b, R_QUSEDHI) = (u32)((u64)&vq.used >> 32);
        REG(b, R_QREADY) = 1;
        REG(b, R_STATUS) = ST_ACK | ST_DRIVER | ST_FEATURES_OK | ST_DRIVER_OK;

        vq.avail.flags = 1;                    // NO_INTERRUPT: we poll, cooperatively
        u64 cap = (u64)REG(b, R_CONFIG) | ((u64)REG(b, R_CONFIG + 4) << 32);
        return cap;
    }
    return 0;
}

// rw one 512-byte sector. write=0 read, write=1 write. returns 0 on OK.
int BlkRW(u64 sector, void *buf, int write) {
    if (!blk_base) return -1;
    req_hdr.type = write ? 1 : 0;
    req_hdr.reserved = 0;
    req_hdr.sector = sector;
    req_status = 0xFF;

    vq.desc[0] = (VqDesc){ (u64)&req_hdr, 16, F_NEXT, 1 };
    vq.desc[1] = (VqDesc){ (u64)buf, 512, (u16)(F_NEXT | (write ? 0 : F_WRITE)), 2 };
    vq.desc[2] = (VqDesc){ (u64)&req_status, 1, F_WRITE, 0 };

    vq.avail.ring[vq.avail.idx % QSZ] = 0;
    asm volatile("dsb sy");
    vq.avail.idx++;
    asm volatile("dsb sy");
    REG(blk_base, R_QNOTIFY) = 0;

    while (vq.used.idx == last_used) Yield();  // cooperative poll
    asm volatile("dsb sy");
    last_used = vq.used.idx;
    return req_status == 0 ? 0 : (int)req_status;
}
