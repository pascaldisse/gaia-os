// GAIA-OS Zone B — virtio-net + minimal IP stack: ARP, IPv4, ICMP, UDP, DNS.
// Successor to ZealOS Home/Net ambitions, EXODUS spirit: small, polled, alive.
// QEMU slirp world: we are 10.0.2.15/24, gw 10.0.2.2, dns 10.0.2.3.
#include "kernel.h"

#define VIRTIO_BASE   0x0a000000UL
#define VIRTIO_STRIDE 0x200
#define VIRTIO_SLOTS  32

#define R_MAGIC   0x00
#define R_VERSION 0x04
#define R_DEVID   0x08
#define R_GFEATSEL 0x24
#define R_GFEAT   0x20
#define R_QSEL    0x30
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
#define BUFSZ 1536                      // 12B vnet hdr + 1514 frame, rounded

typedef struct { u64 addr; u32 len; u16 flags; u16 next; } VqDesc;
#define F_NEXT 1
#define F_WRITE 2

typedef struct {
    VqDesc desc[QSZ];
    struct { u16 flags, idx; u16 ring[QSZ]; } avail;
    unsigned char pad[192 - 128 - 20];
    struct { u16 flags, idx; struct { u32 id, len; } ring[QSZ]; } used;
} __attribute__((aligned(4096))) Vq;

static Vq rxq, txq;
static u16 rx_last, tx_last;
static u64 net_base;

static unsigned char rxbuf[QSZ][BUFSZ] __attribute__((aligned(16)));
static unsigned char txbuf[BUFSZ] __attribute__((aligned(16)));

static unsigned char my_mac[6];
static u32 my_ip  = 0x0A00020F;         // 10.0.2.15
static u32 dns_ip = 0x0A000203;         // 10.0.2.3

#define REG(b, o) MMIO32((b) + (o))

// ------------------------------------------------------------ wire utils ----
static void put16(unsigned char *p, u32 v) { p[0] = v >> 8; p[1] = v; }
static void put32(unsigned char *p, u32 v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v; }
static u32 get16(const unsigned char *p) { return (u32)p[0] << 8 | p[1]; }
static u32 get32(const unsigned char *p) { return (u32)p[0] << 24 | (u32)p[1] << 16 | (u32)p[2] << 8 | p[3]; }

static u32 csum_add(const unsigned char *p, int len, u32 sum) {
    for (int i = 0; i + 1 < len; i += 2) sum += get16(p + i);
    if (len & 1) sum += (u32)p[len - 1] << 8;
    return sum;
}
static u32 csum_fin(u32 sum) {
    while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);
    return (~sum) & 0xFFFF;
}

static void print_ip(u32 ip) {
    for (int i = 3; i >= 0; i--) {
        u32 b = (ip >> (i * 8)) & 0xFF;
        char d[4]; int j = 3; d[3] = 0;
        do { d[--j] = '0' + b % 10; b /= 10; } while (b);
        puts(&d[j]);
        if (i) putc('.');
    }
}

// ------------------------------------------------------------- transport ----
static void setup_q(u64 b, int qi, Vq *q) {
    REG(b, R_QSEL) = qi;
    REG(b, R_QNUM) = QSZ;
    REG(b, R_QDESCLO) = (u32)(u64)&q->desc;  REG(b, R_QDESCHI) = (u32)((u64)&q->desc >> 32);
    REG(b, R_QAVLO)   = (u32)(u64)&q->avail; REG(b, R_QAVHI)   = (u32)((u64)&q->avail >> 32);
    REG(b, R_QUSEDLO) = (u32)(u64)&q->used;  REG(b, R_QUSEDHI) = (u32)((u64)&q->used >> 32);
    REG(b, R_QREADY) = 1;
    q->avail.flags = 1;                 // NO_INTERRUPT: we poll, cooperatively
}

static void rx_post(int i) {
    rxq.desc[i] = (VqDesc){ (u64)rxbuf[i], BUFSZ, F_WRITE, 0 };
    rxq.avail.ring[rxq.avail.idx % QSZ] = (u16)i;
    asm volatile("dsb sy");
    rxq.avail.idx++;
    asm volatile("dsb sy");
    REG(net_base, R_QNOTIFY) = 0;
}

int NetInit(void) {
    for (int i = 0; i < VIRTIO_SLOTS; i++) {
        u64 b = VIRTIO_BASE + (u64)i * VIRTIO_STRIDE;
        if (REG(b, R_MAGIC) != 0x74726976 || REG(b, R_DEVID) != 1) continue;
        if (REG(b, R_VERSION) != 2) continue;
        net_base = b;

        REG(b, R_STATUS) = 0;
        REG(b, R_STATUS) = ST_ACK;
        REG(b, R_STATUS) = ST_ACK | ST_DRIVER;
        REG(b, R_GFEATSEL) = 0;         // VIRTIO_NET_F_MAC (bit 5)
        REG(b, R_GFEAT) = 1 << 5;
        REG(b, R_GFEATSEL) = 1;         // VIRTIO_F_VERSION_1 (bit 32)
        REG(b, R_GFEAT) = 1;
        REG(b, R_STATUS) = ST_ACK | ST_DRIVER | ST_FEATURES_OK;
        if (!(REG(b, R_STATUS) & ST_FEATURES_OK)) { puts("virtio-net: features rejected\n"); return -1; }

        u32 m0 = REG(b, R_CONFIG), m1 = REG(b, R_CONFIG + 4);
        my_mac[0] = m0; my_mac[1] = m0 >> 8; my_mac[2] = m0 >> 16; my_mac[3] = m0 >> 24;
        my_mac[4] = m1; my_mac[5] = m1 >> 8;

        setup_q(b, 0, &rxq);
        setup_q(b, 1, &txq);
        REG(b, R_STATUS) = ST_ACK | ST_DRIVER | ST_FEATURES_OK | ST_DRIVER_OK;

        for (int j = 0; j < QSZ; j++) rx_post(j);
        return 0;
    }
    return -1;
}

// send an ethernet frame already assembled at txbuf+12, flen bytes
static void tx_frame(int flen) {
    for (int i = 0; i < 12; i++) txbuf[i] = 0;      // vnet hdr: no offloads
    txq.desc[0] = (VqDesc){ (u64)txbuf, (u32)(12 + flen), 0, 0 };
    txq.avail.ring[txq.avail.idx % QSZ] = 0;
    asm volatile("dsb sy");
    txq.avail.idx++;
    asm volatile("dsb sy");
    REG(net_base, R_QNOTIFY) = 1;
    while (txq.used.idx == tx_last) Yield();
    tx_last = txq.used.idx;
}

static unsigned char *eth_hdr(const unsigned char *dst, u32 type) {
    unsigned char *f = txbuf + 12;
    for (int i = 0; i < 6; i++) f[i] = dst[i];
    for (int i = 0; i < 6; i++) f[6 + i] = my_mac[i];
    put16(f + 12, type);
    return f + 14;
}

// ------------------------------------------------------------------ ARP ----
static u32 arp_ip; static unsigned char arp_mac[6]; static int arp_ok;

static void arp_send(int op, u32 tpa, const unsigned char *tha, const unsigned char *dst) {
    unsigned char *p = eth_hdr(dst, 0x0806);
    put16(p, 1); put16(p + 2, 0x0800);
    p[4] = 6; p[5] = 4;
    put16(p + 6, op);
    for (int i = 0; i < 6; i++) p[8 + i] = my_mac[i];
    put32(p + 14, my_ip);
    for (int i = 0; i < 6; i++) p[18 + i] = tha ? tha[i] : 0;
    put32(p + 24, tpa);
    tx_frame(14 + 28);
}

static const unsigned char bcast[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };

static int arp_resolve(u32 ip, unsigned char *mac_out) {
    if (arp_ok && arp_ip == ip) {
        for (int i = 0; i < 6; i++) mac_out[i] = arp_mac[i];
        return 0;
    }
    arp_ok = 0; arp_ip = ip;
    arp_send(1, ip, 0, bcast);
    u64 deadline = ticks + 8;                       // 2s
    while (!arp_ok && ticks < deadline) Yield();
    if (!arp_ok) return -1;
    for (int i = 0; i < 6; i++) mac_out[i] = arp_mac[i];
    return 0;
}

// ----------------------------------------------------------------- IPv4 ----
static u32 ip_ident = 0x4749;                       // "GI"

static void ip_send(u32 dst, int proto, const unsigned char *payload, int plen) {
    unsigned char dmac[6];
    if (arp_resolve(dst, dmac)) { puts("net: arp timeout\n"); return; }
    unsigned char *p = eth_hdr(dmac, 0x0800);
    p[0] = 0x45; p[1] = 0;
    put16(p + 2, 20 + plen);
    put16(p + 4, ip_ident++);
    put16(p + 6, 0x4000);                           // DF
    p[8] = 64; p[9] = (unsigned char)proto;
    put16(p + 10, 0);
    put32(p + 12, my_ip);
    put32(p + 16, dst);
    put16(p + 10, csum_fin(csum_add(p, 20, 0)));
    for (int i = 0; i < plen; i++) p[20 + i] = payload[i];
    tx_frame(14 + 20 + plen);
}

// ----------------------------------------------------------------- ICMP ----
static volatile u32 ping_seq_rx;
static volatile u32 ping_from;

static void icmp_send(u32 dst, int type, int id, int seq, const unsigned char *data, int dlen) {
    static unsigned char b[64];
    b[0] = (unsigned char)type; b[1] = 0;
    put16(b + 2, 0);
    put16(b + 4, id); put16(b + 6, seq);
    for (int i = 0; i < dlen; i++) b[8 + i] = data[i];
    put16(b + 2, csum_fin(csum_add(b, 8 + dlen, 0)));
    ip_send(dst, 1, b, 8 + dlen);
}

// ------------------------------------------------------------------ UDP ----
static volatile int udp_pending_port = -1;
static unsigned char udp_rx[1024];
static volatile int udp_rx_len = -1;

static void udp_send(u32 dst, int dport, int sport, const unsigned char *data, int dlen) {
    static unsigned char b[1024];
    if (dlen > 1000) return;
    put16(b, sport); put16(b + 2, dport);
    put16(b + 4, 8 + dlen); put16(b + 6, 0);        // csum 0: legal for IPv4
    for (int i = 0; i < dlen; i++) b[8 + i] = data[i];
    ip_send(dst, 17, b, 8 + dlen);
}

// ------------------------------------------------------------ RX process ----
static void handle_frame(unsigned char *f, int flen) {
    if (flen < 14) return;
    u32 type = get16(f + 12);
    if (type == 0x0806 && flen >= 42) {             // ARP
        u32 op = get16(f + 20), spa = get32(f + 28), tpa = get32(f + 38);
        if (op == 1 && tpa == my_ip) arp_send(2, spa, f + 22, f + 22);
        else if (op == 2 && spa == arp_ip) {
            for (int i = 0; i < 6; i++) arp_mac[i] = f[22 + i];
            arp_ok = 1;
        }
        return;
    }
    if (type != 0x0800 || flen < 34) return;        // IPv4 only
    unsigned char *ip = f + 14;
    int ihl = (ip[0] & 0xF) * 4;
    if (get32(ip + 16) != my_ip) return;
    int proto = ip[9];
    unsigned char *pl = ip + ihl;
    int plen = (int)get16(ip + 2) - ihl;
    if (proto == 1 && plen >= 8) {                  // ICMP
        if (pl[0] == 8) {                           // echo request -> reply
            static unsigned char eb[576];
            if (plen > 576) plen = 576;
            for (int i = 0; i < plen; i++) eb[i] = pl[i];
            eb[0] = 0; put16(eb + 2, 0);
            put16(eb + 2, csum_fin(csum_add(eb, plen, 0)));
            ip_send(get32(ip + 12), 1, eb, plen);
        } else if (pl[0] == 0) {                    // echo reply
            ping_seq_rx = get16(pl + 6);
            ping_from = get32(ip + 12);
        }
        return;
    }
    if (proto == 17 && plen >= 8) {                 // UDP
        int dport = (int)get16(pl + 2);
        if (dport == udp_pending_port) {
            int dl = (int)get16(pl + 4) - 8;
            if (dl > 1024) dl = 1024;
            for (int i = 0; i < dl; i++) udp_rx[i] = pl[8 + i];
            udp_rx_len = dl;
        }
    }
}

void NetRxTask(void) {
    for (;;) {
        while (rxq.used.idx != rx_last) {
            asm volatile("dsb sy");
            u32 id = rxq.used.ring[rx_last % QSZ].id;
            u32 len = rxq.used.ring[rx_last % QSZ].len;
            rx_last++;
            if (len > 12) handle_frame(rxbuf[id] + 12, (int)len - 12);
            rx_post((int)id);
        }
        Yield();
    }
}

// -------------------------------------------------------- shell services ----
void NetInfo(void) {
    if (!net_base) { puts("net: no device\n"); return; }
    puts("mac ");
    for (int i = 0; i < 6; i++) {
        putc("0123456789ab cdef"[my_mac[i] >> 4 & 15]); putc("0123456789abcdef"[my_mac[i] & 15]);
        if (i < 5) putc(':');
    }
    puts("  ip "); print_ip(my_ip);
    puts("  gw "); print_ip(0x0A000202);
    puts("  dns "); print_ip(dns_ip);
    puts("\n");
}

void NetPing(u32 dst, int count) {
    if (!net_base) { puts("net: no device\n"); return; }
    static const unsigned char pay[] = "GAIA-OS love=1.000 ping ";
    for (int seq = 1; seq <= count; seq++) {
        ping_seq_rx = 0;
        u64 t0 = ticks;
        icmp_send(dst, 8, 0x4749, seq, pay, sizeof(pay) - 1);
        u64 deadline = ticks + 8;
        while (ping_seq_rx != (u32)seq && ticks < deadline) Yield();
        if (ping_seq_rx == (u32)seq) {
            puts("reply from "); print_ip(ping_from);
            puts(" seq="); putc('0' + seq);
            puts(" time<");
            u64 dt = (ticks - t0 + 1) * 250;
            char d[8]; int j = 7; d[7] = 0;
            do { d[--j] = '0' + dt % 10; dt /= 10; } while (dt);
            puts(&d[j]); puts("ms\n");
        } else puts("timeout\n");
    }
}

static int dns_name(unsigned char *q, const char *name) {   // encode labels
    int qi = 0, li = qi++;
    for (const char *c = name; ; c++) {
        if (*c == '.' || !*c) {
            q[li] = (unsigned char)(qi - li - 1);
            if (!*c) break;
            li = qi++;
        } else q[qi++] = (unsigned char)*c;
        if (qi > 250) return -1;
    }
    q[qi++] = 0;
    return qi;
}

int NetDns(const char *name, u32 *out) {
    if (!net_base) { puts("net: no device\n"); return -1; }
    static unsigned char q[300];
    u32 id = (u32)(ticks & 0xFFFF) | 1;
    put16(q, id); put16(q + 2, 0x0100);             // RD
    put16(q + 4, 1); put16(q + 6, 0); put16(q + 8, 0); put16(q + 10, 0);
    int n = dns_name(q + 12, name);
    if (n < 0) return -1;
    n += 12;
    put16(q + n, 1); put16(q + n + 2, 1);           // type A, class IN
    n += 4;

    int sport = 0xC000 | (int)(ticks & 0xFFF);
    udp_pending_port = sport; udp_rx_len = -1;
    udp_send(dns_ip, 53, sport, q, n);
    u64 deadline = ticks + 12;                      // 3s
    while (udp_rx_len < 0 && ticks < deadline) Yield();
    udp_pending_port = -1;
    if (udp_rx_len < 12 || get16(udp_rx) != id) { puts("dns: timeout\n"); return -1; }

    int an = (int)get16(udp_rx + 6);
    int p = 12;
    while (p < udp_rx_len && udp_rx[p]) p += udp_rx[p] + 1;   // skip question name
    p += 5;                                                    // 0-byte + type + class
    for (int a = 0; a < an && p + 12 <= udp_rx_len; a++) {
        if ((udp_rx[p] & 0xC0) == 0xC0) p += 2;               // compressed name
        else { while (p < udp_rx_len && udp_rx[p]) p += udp_rx[p] + 1; p++; }
        u32 t = get16(udp_rx + p);
        int rdlen = (int)get16(udp_rx + p + 8);
        p += 10;
        if (t == 1 && rdlen == 4) { *out = get32(udp_rx + p); return 0; }
        p += rdlen;
    }
    puts("dns: no A record\n");
    return -1;
}

void NetPrintIp(u32 ip) { print_ip(ip); }
