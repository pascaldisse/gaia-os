// GAIA-OS Zone A/B — RedSea-spirit filesystem: contiguous files, flat dir.
// Terry's design philosophy: allocation is a bump pointer, files are one
// unbroken run of sectors, the directory is an array. No inode Babylon.
#include "kernel.h"

#define SEC_SUPER 1
#define SEC_DIR   8
#define DIR_SECS  8            // 8 sectors * 8 entries = 64 files
#define SEC_DATA  16
#define FS_MAGIC  0x41455344455230ULL   // "0RESDEA" little-endian-ish stamp

typedef struct {               // 64 bytes
    u16 attr;                  // 0 empty, 1 file
    char name[38];
    u64 start, size, mtime;    // sector, bytes, tick-stamp
} CDirEnt;                     // 8 per sector

typedef struct {
    u64 magic, total_secs, next_free;
    unsigned char pad[512 - 24];
} CSuper;

static CSuper super;
static unsigned char secbuf[512];

static int fs_ok;

int FsMount(void) {
    if (BlkRW(SEC_SUPER, &super, 0)) return -1;
    fs_ok = (super.magic == FS_MAGIC);
    return fs_ok ? 0 : -1;
}

int FsFormat(u64 disk_secs) {
    super.magic = FS_MAGIC;
    super.total_secs = disk_secs;
    super.next_free = SEC_DATA;
    for (u64 i = 0; i < 512 - 24; i++) super.pad[i] = 0;
    if (BlkRW(SEC_SUPER, &super, 1)) return -1;
    for (u64 s = 0; s < DIR_SECS; s++) {
        for (int i = 0; i < 512; i++) secbuf[i] = 0;
        if (BlkRW(SEC_DIR + s, secbuf, 1)) return -1;
    }
    fs_ok = 1;
    return 0;
}

static int name_eq(const char *a, const char *b) {
    int i = 0;
    while (i < 38 && a[i] && a[i] == b[i]) i++;
    return (i == 38) || (a[i] == b[i]);
}

// find entry by name; if name==0, find first empty. returns dir slot or -1.
static int fs_find(const char *name, CDirEnt *out, u64 *sec, int *idx) {
    for (u64 s = 0; s < DIR_SECS; s++) {
        if (BlkRW(SEC_DIR + s, secbuf, 0)) return -1;
        CDirEnt *e = (CDirEnt *)secbuf;
        for (int i = 0; i < 8; i++) {
            if (name ? (e[i].attr == 1 && name_eq(e[i].name, name))
                     : (e[i].attr == 0)) {
                *out = e[i]; *sec = SEC_DIR + s; *idx = i;
                return 0;
            }
        }
    }
    return -1;
}

int FsWrite(const char *name, const void *buf, u64 len) {
    if (!fs_ok) return -1;
    CDirEnt e; u64 dsec; int di;
    int existed = fs_find(name, &e, &dsec, &di) == 0;
    if (!existed && fs_find(0, &e, &dsec, &di)) { puts("fs: dir full\n"); return -1; }
    u64 have = existed ? (e.size + 511) / 512 : 0;
    u64 need = (len + 511) / 512;
    if (need > have) {                                // (re)allocate at bump
        if (super.next_free + need > super.total_secs) { puts("fs: disk full\n"); return -1; }
        e.start = super.next_free;
        super.next_free += need;
        if (BlkRW(SEC_SUPER, &super, 1)) return -1;
    }
    e.attr = 1; e.size = len; e.mtime = ticks;
    for (int i = 0; i < 38; i++) { e.name[i] = *name; if (*name) name++; }

    const unsigned char *p = buf;
    for (u64 s = 0; s < need; s++) {
        for (int i = 0; i < 512; i++) {
            u64 off = s * 512 + i;
            secbuf[i] = off < len ? p[off] : 0;
        }
        if (BlkRW(e.start + s, secbuf, 1)) return -1;
    }
    if (BlkRW(dsec, secbuf, 0)) return -1;
    ((CDirEnt *)secbuf)[di] = e;
    return BlkRW(dsec, secbuf, 1);
}

long FsSize(const char *name) {
    if (!fs_ok) return -1;
    CDirEnt e; u64 dsec; int di;
    if (fs_find(name, &e, &dsec, &di)) return -1;
    return (long)e.size;
}

long FsRead(const char *name, void *buf, u64 maxlen) {
    if (!fs_ok) return -1;
    CDirEnt e; u64 dsec; int di;
    if (fs_find(name, &e, &dsec, &di)) return -1;
    u64 len = e.size < maxlen ? e.size : maxlen;
    unsigned char *p = buf;
    for (u64 s = 0; s * 512 < len; s++) {
        if (BlkRW(e.start + s, secbuf, 0)) return -1;
        for (int i = 0; i < 512 && s * 512 + (u64)i < len; i++)
            p[s * 512 + i] = secbuf[i];
    }
    return (long)len;
}

void FsLs(void) {
    if (!fs_ok) { puts("fs: not mounted (try 'format')\n"); return; }
    int n = 0;
    for (u64 s = 0; s < DIR_SECS; s++) {
        if (BlkRW(SEC_DIR + s, secbuf, 0)) return;
        CDirEnt *e = (CDirEnt *)secbuf;
        for (int i = 0; i < 8; i++) {
            if (e[i].attr != 1) continue;
            n++;
            puts("  ");
            for (int k = 0; k < 38 && e[i].name[k]; k++) putc(e[i].name[k]);
            puts("  ");
            char b[21]; int j = 20; b[j] = 0; u64 v = e[i].size;
            do { b[--j] = '0' + v % 10; v /= 10; } while (v);
            puts(&b[j]); puts(" bytes @sec ");
            j = 20; v = e[i].start;
            do { b[--j] = '0' + v % 10; v /= 10; } while (v);
            puts(&b[j]); puts("\n");
        }
    }
    if (!n) puts("  (empty)\n");
}
