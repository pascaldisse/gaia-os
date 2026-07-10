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

// shell.c
void shell_task(void);

// virtio.c — virtio-mmio blk, polled
u64 BlkInit(void);
int BlkRW(u64 sector, void *buf, int write);

// hc.c — SeedC: HolyC-subset JIT compiler (Zone C seed)
void HcRun(const char *src);

// net.c — virtio-net + ARP/IPv4/ICMP/UDP/DNS, polled
int NetInit(void);
void NetRxTask(void);
void NetInfo(void);
void NetPing(u32 dst, int count);
int NetDns(const char *name, u32 *out);
void NetPrintIp(u32 ip);

// fs.c — RedSea-spirit contiguous FS
int FsMount(void);
int FsFormat(u64 disk_secs);
int FsWrite(const char *name, const void *buf, u64 len);
long FsRead(const char *name, void *buf, u64 maxlen);
void FsLs(void);
