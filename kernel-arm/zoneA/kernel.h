// GAIA-OS Zone A — shared kernel decls
#pragma once
typedef unsigned long long u64;
typedef unsigned int u32;
typedef unsigned short u16;

#define MMIO32(a) (*(volatile u32 *)(u64)(a))

void putc(char c);
void puts(const char *s);
void puthex(u64 v);

// mem.c — Temple naming, ZealOS MAllocFree.ZC successor
void MemInit(void);
void *MAlloc(u64 size);
void Free(void *p);
u64 MemFreeBytes(void);

// task.c — cooperative round-robin, KTask.ZC/Sched.ZC successor
void TaskInit(void);
void Spawn(void (*fn)(void), const char *name);
void Yield(void);
extern volatile u64 ticks;

// kmain.c — CHALICE C3c Atom 3C-2b: IRQ PC-sampling profiler arm flag
extern volatile int profile_armed;

// kmain.c — CHALICE C4-9 (monad ruling #2): arena L1 table-descriptor
// install/read + standalone TLB invalidate, for ntfile.c's RESERVE/COMMIT
// arena to add L2 table mappings for its own 1GB slots without exposing
// mmu_on's private l1_table array. See kmain.c mmu_on for the encoding
// receipt.
void nt_l1_install_table(u64 l1_idx, u64 l2_phys);
u64  nt_l1_read(u64 l1_idx);
void nt_tlbi_vmalle1(void);

// shell.c
void shell_task(void);

// virtio.c — virtio-mmio blk, polled
u64 BlkInit(void);
int BlkRW(u64 sector, void *buf, int write);

// fw_cfg.c — QEMU fw_cfg (MMIO) + ramfb framebuffer
#define FB_WIDTH  640
#define FB_HEIGHT 480
#define FB_BPP    4        // XRGB8888
int  FbInit(void);         // configure ramfb via fw_cfg; 0 = ok
void FbDrawPattern(void);  // gradient + 3 color bars + white border
long FbBase(void);         // XRGB8888 surface base (C4 GaiaPlot import)
long FbWidth(void);        // pixels per row (C4 GaiaPlot import)

// hc.c — SeedC: HolyC-subset JIT compiler (Zone C seed)
void HcRun(const char *src);
void iflush(void *base, int bytes);

// net.c — virtio-net + ARP/IPv4/ICMP/UDP/DNS, polled
int NetInit(void);
void NetRxTask(void);
void NetInfo(void);
void NetPing(u32 dst, int count);
int NetDns(const char *name, u32 *out);
void NetPrintIp(u32 ip);

// tcp.c — minimal blocking single-connection TCP client
int NetHttpGet(u32 ip, const char *host, const char *path);

// fs.c — RedSea-spirit contiguous FS
int FsMount(void);
int FsFormat(u64 disk_secs);
int FsWrite(const char *name, const void *buf, u64 len);
long FsRead(const char *name, void *buf, u64 maxlen);
void FsLs(void);
