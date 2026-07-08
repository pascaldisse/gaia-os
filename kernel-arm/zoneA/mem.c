// GAIA-OS Zone A — MAlloc/Free: first-fit free list with coalescing.
// Successor to ZealOS Memory/MAllocFree.ZC (asm parts -> plain C, Zone D style).
#include "kernel.h"

#define HEAP_BASE 0x41000000UL   // well above kernel load (0x40200000) + stack
#define HEAP_SIZE (64UL * 1024 * 1024)

typedef struct CHeapEnt {
    u64 size;                    // payload bytes
    struct CHeapEnt *next;       // next free block (free list only)
} CHeapEnt;

static CHeapEnt *free_list;

void MemInit(void) {
    free_list = (CHeapEnt *)HEAP_BASE;
    free_list->size = HEAP_SIZE - sizeof(CHeapEnt);
    free_list->next = 0;
}

void *MAlloc(u64 size) {
    size = (size + 15) & ~15ULL;
    CHeapEnt **pp = &free_list;
    for (CHeapEnt *e = free_list; e; pp = &e->next, e = e->next) {
        if (e->size < size) continue;
        if (e->size >= size + sizeof(CHeapEnt) + 16) {   // split
            CHeapEnt *rest = (CHeapEnt *)((u64)(e + 1) + size);
            rest->size = e->size - size - sizeof(CHeapEnt);
            rest->next = e->next;
            e->size = size;
            *pp = rest;
        } else {
            *pp = e->next;
        }
        return e + 1;
    }
    puts("MAlloc: OUT OF MEMORY\n");
    return 0;
}

void Free(void *p) {
    if (!p) return;
    CHeapEnt *e = (CHeapEnt *)p - 1;
    // insert sorted by address, coalesce neighbors
    CHeapEnt **pp = &free_list;
    while (*pp && *pp < e) pp = &(*pp)->next;
    e->next = *pp;
    *pp = e;
    if (e->next && (u64)(e + 1) + e->size == (u64)e->next) {   // merge fwd
        e->size += sizeof(CHeapEnt) + e->next->size;
        e->next = e->next->next;
    }
    if (pp != &free_list) {
        CHeapEnt *prev = (CHeapEnt *)((u64)pp - ((u64)&e->next - (u64)e));
        if ((u64)(prev + 1) + prev->size == (u64)e) {          // merge back
            prev->size += sizeof(CHeapEnt) + e->size;
            prev->next = e->next;
        }
    }
}

u64 MemFreeBytes(void) {
    u64 n = 0;
    for (CHeapEnt *e = free_list; e; e = e->next) n += e->size;
    return n;
}
