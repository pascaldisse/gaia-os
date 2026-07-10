// GAIA-OS Zone A — minimal TCP client: single global connection, blocking,
// no retransmission (slirp is lossless), no window management (advertise 8192).
// Successor spirit: small, polled, alive. Rides on net.c's virtio-net/IPv4.
#include "kernel.h"

// -------------------------------------------------------- borrowed from net.c
extern void ip_send(u32 dst, int proto, const unsigned char *payload, int plen);
extern u32 my_ip;

// ------------------------------------------------------------ wire utils ----
// (local copies — net.c's are static; same accumulation style)
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

static void print_dec(u64 v) {
    char d[21]; int j = 20; d[20] = 0;
    do { d[--j] = '0' + v % 10; v /= 10; } while (v);
    puts(&d[j]);
}

// ------------------------------------------------------------------- flags --
#define TF_FIN 0x01
#define TF_SYN 0x02
#define TF_RST 0x04
#define TF_PSH 0x08
#define TF_ACK 0x10

// ------------------------------------------------------------------- state --
typedef enum { TCP_CLOSED, TCP_SYN_SENT, TCP_ESTAB, TCP_DONE } TcpState;

static TcpState tcp_state = TCP_CLOSED;
static u16 tcp_lport;
static u32 tcp_iss;
static u32 tcp_snd_nxt, tcp_rcv_nxt;
static u32 tcp_rip;
static u16 tcp_rport;

static unsigned char tcp_buf[16384 + 1];   // +1: room for the NUL we print with
static int tcp_buf_len;

// ------------------------------------------------------------------ segment --
static void tcp_send(u32 dst, u16 dport, u32 seq, u32 ack, int flags,
                      const unsigned char *data, int dlen, int with_mss) {
    static unsigned char b[24 + 512];
    int hlen = with_mss ? 24 : 20;
    if (dlen > 512) dlen = 512;

    put16(b, tcp_lport);
    put16(b + 2, dport);
    put32(b + 4, seq);
    put32(b + 8, ack);
    b[12] = (unsigned char)((hlen / 4) << 4);
    b[13] = (unsigned char)flags;
    put16(b + 14, 8192);                    // window: always advertise 8192
    put16(b + 16, 0);                       // checksum, filled below
    put16(b + 18, 0);                       // urgent ptr
    if (with_mss) {
        b[20] = 2; b[21] = 4; put16(b + 22, 1460);
    }
    for (int i = 0; i < dlen; i++) b[hlen + i] = data[i];
    int tlen = hlen + dlen;

    unsigned char ph[12];                   // pseudo-header
    put32(ph, my_ip);
    put32(ph + 4, dst);
    ph[8] = 0; ph[9] = 6;
    put16(ph + 10, tlen);
    u32 sum = csum_add(ph, 12, 0);
    sum = csum_add(b, tlen, sum);
    put16(b + 16, csum_fin(sum));

    ip_send(dst, 6, b, tlen);
}

// ---------------------------------------------------------------------- rx --
void TcpRx(const unsigned char *ipkt, int iplen) {
    int ihl = (ipkt[0] & 0xF) * 4;
    if (iplen < ihl + 20) return;
    const unsigned char *tcp = ipkt + ihl;
    int tlen = iplen - ihl;

    u32 sport = get16(tcp);
    u32 dport = get16(tcp + 2);
    if (tcp_state == TCP_CLOSED || dport != tcp_lport) return;

    u32 seq = get32(tcp + 4);
    int hlen = (tcp[12] >> 4) * 4;
    int flags = tcp[13];
    int dlen = tlen - hlen;
    if (dlen < 0) return;
    const unsigned char *payload = tcp + hlen;
    u32 srcip = get32(ipkt + 12);

    if (tcp_state == TCP_SYN_SENT) {
        if ((flags & TF_SYN) && (flags & TF_ACK)) {
            tcp_rip = srcip; tcp_rport = (u16)sport;
            tcp_rcv_nxt = seq + 1;
            tcp_snd_nxt = tcp_iss + 1;
            tcp_send(tcp_rip, tcp_rport, tcp_snd_nxt, tcp_rcv_nxt, TF_ACK, 0, 0, 0);
            tcp_state = TCP_ESTAB;
        }
        return;
    }

    if (tcp_state == TCP_ESTAB) {
        if (srcip != tcp_rip || sport != tcp_rport) return;

        if (dlen > 0 && seq == tcp_rcv_nxt) {
            for (int i = 0; i < dlen; i++) {
                if (tcp_buf_len < (int)sizeof(tcp_buf) - 1) tcp_buf[tcp_buf_len++] = payload[i];
            }
            tcp_rcv_nxt += (u32)dlen;
            tcp_send(tcp_rip, tcp_rport, tcp_snd_nxt, tcp_rcv_nxt, TF_ACK, 0, 0, 0);
        }
        if (flags & TF_FIN) {
            tcp_rcv_nxt += 1;
            tcp_send(tcp_rip, tcp_rport, tcp_snd_nxt, tcp_rcv_nxt, TF_FIN | TF_ACK, 0, 0, 0);
            tcp_state = TCP_DONE;
        }
    }
}

// ------------------------------------------------------------- shell service --
int NetHttpGet(u32 ip, const char *host, const char *path) {
    tcp_lport = (u16)(0xC000 | (ticks & 0xFFF));
    tcp_iss = (u32)(ticks << 16);
    tcp_snd_nxt = tcp_iss;
    tcp_rcv_nxt = 0;
    tcp_rip = ip; tcp_rport = 80;
    tcp_buf_len = 0;
    tcp_state = TCP_SYN_SENT;

    tcp_send(ip, 80, tcp_iss, 0, TF_SYN, 0, 0, 1);   // SYN carries MSS option

    u64 deadline = ticks + 12;
    while (tcp_state == TCP_SYN_SENT && ticks < deadline) Yield();
    if (tcp_state != TCP_ESTAB) {
        puts("tcp: connect timeout\n");
        tcp_state = TCP_CLOSED;
        return -1;
    }

    static char req[600];
    int n = 0;
    const char *s;
    s = "GET "; while (*s) req[n++] = *s++;
    s = path;   while (*s && n < 500) req[n++] = *s++;
    s = " HTTP/1.0\r\nHost: "; while (*s) req[n++] = *s++;
    s = host;   while (*s && n < 560) req[n++] = *s++;
    s = "\r\nConnection: close\r\n\r\n"; while (*s) req[n++] = *s++;

    tcp_send(tcp_rip, tcp_rport, tcp_snd_nxt, tcp_rcv_nxt, TF_PSH | TF_ACK,
              (const unsigned char *)req, n, 0);
    tcp_snd_nxt += (u32)n;

    deadline = ticks + 40;
    while (tcp_state != TCP_DONE && ticks < deadline) Yield();
    tcp_state = TCP_CLOSED;

    if (tcp_buf_len <= 0) { puts("tcp: no data\n"); return -1; }

    int plen = tcp_buf_len;
    if (plen > 2048) {
        unsigned char save = tcp_buf[2048];
        tcp_buf[2048] = 0;
        puts((const char *)tcp_buf);
        tcp_buf[2048] = save;
        puts("...[");
        print_dec((u64)tcp_buf_len);
        puts(" bytes total]\n");
    } else {
        tcp_buf[plen] = 0;
        puts((const char *)tcp_buf);
    }
    return 0;
}
