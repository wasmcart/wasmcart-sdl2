/* Unit-test the SDL_net shim's logic against a fake wc_peer_* layer, so the
 * queueing, packet lifecycle and SDL_net return conventions are verified without
 * a host in the loop. */
#include <stdio.h>
#include <string.h>
#include <assert.h>

static int fails = 0;
static void ck(const char *what, int ok) {
    printf("  %-4s  %s\n", ok ? "ok" : "FAIL", what);
    if (!ok) fails++;
}

/* Fake host side. */
static unsigned char sent_buf[4096];
static unsigned sent_len;
static int sent_count;
static int fake_peer_count = 1;

int  wc_peer_open(const char *a, unsigned l) { (void)a; (void)l; return 7; }
void wc_peer_close(int p) { (void)p; }
int  wc_peer_send(int p, const void *d, unsigned l) {
    (void)p; memcpy(sent_buf, d, l); sent_len = l; sent_count++; return (int)l;
}
int  wc_peer_broadcast(const void *d, unsigned l) { (void)d; return (int)l; }
int  wc_peer_state(int p) { (void)p; return 2; }
int  wc_peer_count(void) { return fake_peer_count; }
int  wc_peer_id(unsigned i) { (void)i; return 7; }
int  wc_peer_name(int p, char *d, unsigned m) { (void)p; (void)d; (void)m; return 0; }
int  wc_peer_transport(int p) { (void)p; return 0; }

/* Skip wasmcart.h: its wc_peer_* declarations are wasm imports, and this test
 * supplies its own definitions above. */
#define WC_NET_NO_WASMCART_H
#define WC_SDL_NET_IMPLEMENTATION
#include "wc_sdl_net.h"

int main(void) {
    printf("wc_sdl_net shim\n\n");

    ck("SDLNet_Init succeeds", SDLNet_Init() == 0);

    UDPsocket s = SDLNet_UDP_Open(1234);
    ck("UDP_Open returns a socket", s != NULL);

    IPaddress ip;
    ck("ResolveHost accepts a name", SDLNet_ResolveHost(&ip, "ws://host/relay/r", 1234) == 0);
    ck("UDP_Bind connects", SDLNet_UDP_Bind(s, 0, &ip) == 0);

    UDPpacket *p = SDLNet_AllocPacket(512);
    ck("AllocPacket gives maxlen", p != NULL && p->maxlen == 512);

    /* SDL_net's Write16/Read16 are big-endian on the wire. */
    SDLNet_Write16(0xBEEF, p->data);
    ck("Write16/Read16 round-trip", SDLNet_Read16(p->data) == 0xBEEF);
    ck("Write16 is big-endian", p->data[0] == 0xBE && p->data[1] == 0xEF);

    p->len = 8;
    memcpy(p->data, "ABCDEFGH", 8);
    ck("UDP_Send returns 1 on success", SDLNet_UDP_Send(s, 0, p) == 1);
    ck("the datagram reached the peer intact",
       sent_len == 8 && memcmp(sent_buf, "ABCDEFGH", 8) == 0);

    /* Nothing queued yet. */
    UDPpacket *in = SDLNet_AllocPacket(512);
    ck("UDP_Recv returns 0 when empty", SDLNet_UDP_Recv(s, in) == 0);

    /* Host delivers a message. */
    wc_peer_on_message(7, "hello wire", 10);
    ck("UDP_Recv returns 1 after delivery", SDLNet_UDP_Recv(s, in) == 1);
    ck("received bytes match", in->len == 10 && memcmp(in->data, "hello wire", 10) == 0);
    ck("queue empties again", SDLNet_UDP_Recv(s, in) == 0);

    /* Ordering across several datagrams: UDP does not guarantee it, but a queue
     * that reorders locally would be a bug in the shim, not the network. */
    wc_peer_on_message(7, "\x01", 1);
    wc_peer_on_message(7, "\x02", 1);
    wc_peer_on_message(7, "\x03", 1);
    int order_ok = 1;
    for (int i = 1; i <= 3; i++) {
        if (SDLNet_UDP_Recv(s, in) != 1 || in->data[0] != i) order_ok = 0;
    }
    ck("datagrams arrive in delivery order", order_ok);

    /* Overflow drops rather than corrupting. */
    for (int i = 0; i < WC_NET_QUEUE_DEPTH + 20; i++)
        wc_peer_on_message(7, "x", 1);
    int drained = 0;
    while (SDLNet_UDP_Recv(s, in) == 1) drained++;
    ck("a full queue drops instead of overrunning", drained <= WC_NET_QUEUE_DEPTH && drained > 0);

    /*
     * Oversize is clamped, not written past the slot.
     *
     * Checking the length Recv reports is NOT enough: Recv re-clamps against the
     * caller's maxlen, so a missing clamp in the delivery path still reports a
     * sane length while having already scribbled into the next queue slot. So
     * queue a known-good datagram behind the oversize one and verify it survives
     * -- that is the corruption the clamp exists to prevent.
     */
    static unsigned char big[WC_NET_PACKET_MAX * 2];
    memset(big, 0x5A, sizeof big);
    wc_peer_on_message(7, big, sizeof big);
    wc_peer_on_message(7, "CANARY", 6);

    UDPpacket *huge = SDLNet_AllocPacket(WC_NET_PACKET_MAX * 2);
    ck("oversize delivery is clamped to the packet max",
       SDLNet_UDP_Recv(s, huge) == 1 && huge->len == WC_NET_PACKET_MAX);
    ck("an oversize datagram does not corrupt the next queue slot",
       SDLNet_UDP_Recv(s, in) == 1 && in->len == 6 &&
       memcmp(in->data, "CANARY", 6) == 0);
    SDLNet_FreePacket(huge);

    /* A short read must not overrun a small caller buffer. */
    UDPpacket *small = SDLNet_AllocPacket(4);
    wc_peer_on_message(7, "0123456789", 10);
    ck("Recv truncates to the caller's maxlen",
       SDLNet_UDP_Recv(s, small) == 1 && small->len == 4);

    /* Send with no peer must fail, not pretend. */
    SDLNet_UDP_Unbind(s, 0);
    fake_peer_count = 0;
    p->len = 4;
    ck("Send with no peer returns 0", SDLNet_UDP_Send(s, 0, p) == 0);

    SDLNet_FreePacket(p);
    SDLNet_FreePacket(in);
    SDLNet_FreePacket(small);
    SDLNet_UDP_Close(s);
    SDLNet_Quit();
    ck("FreePacket/Close/Quit do not crash", 1);

    printf(fails ? "\nFAILED (%d)\n" : "\nall checks passed\n", fails);
    return fails ? 1 : 0;
}
