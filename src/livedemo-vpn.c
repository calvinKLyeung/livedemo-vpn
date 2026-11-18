#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"
#include <stdio.h>
#include <poll.h>
#include <sodium.h>
#include <time.h>
#include "replay.h"

/* DATA */
#define HDR (1 + 8 + 24)      /* type + counter + nonce = 33 (was 32) */
#define TAG crypto_aead_xchacha20poly1305_ietf_ABYTES
/* HANDSHAKE and SESSION */
#define TYPE_HELLO    1 
#define TYPE_RESPONSE 2
#define TYPE_DATA     3
#define PK            32   /* kx and sign public keys are both 32 bytes */
#define SIGNATURE     crypto_sign_BYTES          /* 64 */
#define HELLO_LEN     (1 + PK + PK + SIGNATURE)  /* 129 */
#define RESPONSE_LEN  (1 + PK + SIGNATURE)       /* 97 */
#define MAX_CLIENTS 16

// HELLO signs:     ["LiveDemoVPN hello v1"]   [client eph_pk]
// RESPONSE signs:  ["LiveDemoVPN response v1"][server eph_pk][client eph_pk]
/* preset Contexts */
static const char CTX_HELLO[]    = "LiveDemoVPN hello v1";
static const char CTX_RESPONSE[] = "LiveDemoVPN response v1";

struct session {
    uint8_t  rx[crypto_kx_SESSIONKEYBYTES];  // receive 
    uint8_t  tx[crypto_kx_SESSIONKEYBYTES];  // transmit 
    uint64_t send_ctr;
    struct window win;
    int      up;
};
static struct session sess; 

static uint8_t my_pk[PK], my_sk[crypto_sign_SECRETKEYBYTES];
static uint8_t server_pk[PK];           /* client only: the pinned server key */

/* track allowed clients */
static uint8_t allowed[MAX_CLIENTS][PK];
static int n_allowed = 0;

/* track bad packets or replays */
static unsigned long bad_packets = 0;       // failed decryption 
static unsigned long replays = 0;           // old or repeated counter

/* flags for argv */
static int tamper = 0, replay = 0;
static unsigned long last_printed_bad = 0;
static unsigned long last_printed_replays = 0;

/* Client Hello */
static uint8_t eph_pk[PK];
static uint8_t eph_sk[crypto_kx_SECRETKEYBYTES];
static uint8_t hello_pkt[HELLO_LEN];
static time_t  last_hello; 


void read_key(const char *path, uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "rb");
    if (!f) die(path);
    if (fread(buf, 1, len, f) != len) { 
        fprintf(stderr, "%s: wrong size\n", path);
        exit(1);
    }
    fclose(f);
}

void load_allowlist(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) die(path);

    char line[256]; 
    while (n_allowed < MAX_CLIENTS && fgets(line, sizeof line, f)) {
        size_t got = 0;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sodium_hex2bin(allowed[n_allowed], PK, line, strlen(line), " \r\n", &got, NULL) == 0 && got == PK)
            n_allowed++;
    }
    fclose(f);
    fprintf(stderr, "%d authorized client(s)\n", n_allowed);
}

int is_authorized(const uint8_t *pk) {
    for (int i = 0; i < n_allowed; i++)
        if (sodium_memcmp(allowed[i], pk, PK) == 0) return 1;
    return 0;
}

// WHO ARE YOU?      identity keys (my_pk/my_sk)  -> signature    -> "it's really the Mac"
// AGREE ON A SECRET ephemeral keys (eph_pk/sk)   -> Diffie–Hellman -> shared secret
// LOCK THE TRAFFIC  session keys (tx/rx)         -> XChaCha20    -> encrypted DATA

/* Client: build and send initial Hello */
void start_handshake(void) {
    crypto_kx_keypair(eph_pk, eph_sk);            /* pick a secret key and computes public key */
    /* [CTX_HELLO][eph_pk]  for Signing only, never send */
    uint8_t msg[sizeof CTX_HELLO + PK];           
    memcpy(msg, CTX_HELLO, sizeof CTX_HELLO);     // copy hello mssage
    memcpy(msg + sizeof CTX_HELLO, eph_pk, PK);   // copy public key
    /* [type][my_pk][eph_pk][signature]  for Sending */
    hello_pkt[0] = TYPE_HELLO;                    // set type hello flag
    memcpy(hello_pkt + 1, my_pk, PK);             // copy Identity public key 
    memcpy(hello_pkt + 1 + PK, eph_pk, PK);       // copy ephemeral Public  key 
    /* seal msg with the Mac's secret identity key; write the 64-byte signature into bytes 65–128 */
    crypto_sign_detached(hello_pkt + 1 + 2 * PK, NULL, msg, sizeof msg, my_sk);
    sess.up = 0;
    last_hello = 0;                                /* send on the next loop pass */
}

/* Server: check Hello and reply to it */
int handle_hello(const uint8_t *in, size_t n, uint8_t *resp) {
    if (n != HELLO_LEN) return -1; 
    const uint8_t *cli_id = in + 1, *cli_eph = in + 1 + PK, *sig = in + 1 + 2 * PK;

    // if not authorized identity public key on the list
    if (!is_authorized(cli_id)) { fprintf(stderr, "unknown client\n"); return -1; }
    uint8_t msg[sizeof CTX_HELLO + PK];
    memcpy(msg, CTX_HELLO, sizeof CTX_HELLO);
    memcpy(msg + sizeof CTX_HELLO, cli_eph, PK);
    if (crypto_sign_verify_detached(sig, msg, sizeof msg, cli_id) != 0) {
        fprintf(stderr, "bad client signature\n");
        return -1; 
    }
    /* Server ephemeral keys */
    uint8_t s_pk[PK], s_sk[crypto_kx_SECRETKEYBYTES];
    crypto_kx_keypair(s_pk, s_sk);
    if (crypto_kx_server_session_keys(sess.rx, sess.tx, s_pk, s_sk, cli_eph) != 0) return -1; // check the cli_eph math is valid 
    sodium_memzero(s_sk, sizeof s_sk);   // replace with zeros for forward secrecy 
    sess.send_ctr = 0; 
    window_reset(&sess.win);
    sess.up = 1;

    /* Response message */
    uint8_t rmsg[sizeof CTX_RESPONSE + 2 * PK];
    memcpy(rmsg, CTX_RESPONSE, sizeof CTX_RESPONSE);
    memcpy(rmsg + sizeof CTX_RESPONSE, s_pk, PK);
    memcpy(rmsg + sizeof CTX_RESPONSE + PK, cli_eph, PK); // tie reply to hello 
    resp[0] = TYPE_RESPONSE; 
    memcpy(resp + 1, s_pk, PK);
    crypto_sign_detached(resp + 1 + PK, NULL, rmsg, sizeof rmsg, my_sk);
    return 0; 
}

/* Client: check Response and derive the keys */
int handle_response(const uint8_t *in, size_t n) {
   if (n != RESPONSE_LEN || sess.up) return -1; 
   const uint8_t *s_eph = in + 1, *sig = in + 1 + PK;

    uint8_t rmsg[sizeof CTX_RESPONSE + 2 * PK];
    memcpy(rmsg, CTX_RESPONSE, sizeof CTX_RESPONSE);
    memcpy(rmsg + sizeof CTX_RESPONSE, s_eph, PK);
    memcpy(rmsg + sizeof CTX_RESPONSE + PK, eph_pk, PK);
    if (crypto_sign_verify_detached(sig, rmsg, sizeof rmsg, server_pk) != 0) {
        fprintf(stderr, "bad server signature\n"); return -1;     /* probably a impostor lol */
    }
    if (crypto_kx_client_session_keys(sess.rx, sess.tx, eph_pk, eph_sk, s_eph) != 0) return -1;
    sodium_memzero(eph_sk, sizeof eph_sk);
    sess.send_ctr = 0; 
    window_reset(&sess.win);
    sess.up = 1;
    return 0;
}

// encrypt packet from in to out, and return updated byte size 
size_t seal_packet(struct session *s, const uint8_t *in, size_t n, uint8_t *out) {
    out[0] = TYPE_DATA;
    put_be64(out + 1, ++s->send_ctr);   
    randombytes_buf(out + 9, 24);    

    unsigned long long clen;    // ciphertext len 
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        out + HDR, &clen, 
        in, n,
        out, HDR,       // authenticated 
        NULL, out + 9, 
        s->tx);
    return HDR + clen;       
}

// decrypt from in to out, return plaintext len 
// or return -1 if packet is bad, like tampered, wrong key, len problem? 
ssize_t open_packet(struct session *s, const uint8_t *in, size_t n, uint8_t *out, uint64_t *ctr) {
    if (n < HDR + TAG) return -1; 

    unsigned long long mlen;    // message plaintext len 
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
        out, &mlen, NULL, 
        in + HDR, n - HDR,  // ciphertext + tag 
        in, HDR,            // same header as additional data 
        in + 9, s->rx       // nonce 
    ) != 0)                 // non 0 means failed 
        return -1;

    *ctr = get_be64(in + 1);   // read the counter from the packet, and hand it back to the caller
    return (ssize_t) mlen;
}


/* Process Conf */
struct config {
    char server[256], tun_addr[256], tun_peer[256];    // 64, 32, 32 was enough, change to 256 to shut the warning from v
    char sign_key[256], server_key[256], clients[256];
    int  port;
};


int load_config(const char *path, struct config *c) {
    memset(c, 0, sizeof *c);
    c->port = 5555;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512], k[64], v[256];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || sscanf(line, " %63[^= ] = %255s", k, v) != 2) continue;  // accepts both "port=5555" and "port = 5555"

        if      (!strcmp(k, "server"))             snprintf(c->server, sizeof c->server, "%s", v);
        else if (!strcmp(k, "port"))               c->port = atoi(v);
        else if (!strcmp(k, "tun_addr"))           snprintf(c->tun_addr, sizeof c->tun_addr, "%s", v);
        else if (!strcmp(k, "tun_peer"))           snprintf(c->tun_peer, sizeof c->tun_peer, "%s", v);
        else if (!strcmp(k, "sign_key"))           snprintf(c->sign_key, sizeof c->sign_key, "%s", v);
        else if (!strcmp(k, "server_key"))         snprintf(c->server_key, sizeof c->server_key, "%s", v);
        else if (!strcmp(k, "authorized_clients")) snprintf(c->clients, sizeof c->clients, "%s", v);
        else fprintf(stderr, "config: unknown setting '%s'\n", k);
    }
    fclose(f);
    return 0;
}


/* Confiture tunnels */
static void run(const char *cmd) {
    fprintf(stderr, "+ %s\n", cmd);
    if (system(cmd) != 0) { fprintf(stderr, "failed: %s\n", cmd); exit(1); }
}

void configure_tun(const char *ifname, const struct config *c) {
    char cmd[512];
#ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "/sbin/ifconfig %s %s %s mtu 1400 up", ifname, c->tun_addr, c->tun_peer);
#else
    snprintf(cmd, sizeof cmd, "ip addr add %s dev %s && ip link set %s up mtu 1400",
             c->tun_addr, ifname, ifname);
#endif
    run(cmd);
}

int main(int argc, char **argv) {
    const char *conf_file = NULL;
    int is_server = 0, is_client = 0;

    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-s") && i + 1 < argc) { is_server = 1; conf_file = argv[++i]; }
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) { is_client = 1; conf_file = argv[++i]; }
        else if (!strcmp(argv[i], "--tamper"))           tamper = 1;
        else if (!strcmp(argv[i], "--replay"))           replay = 1;
        else { is_server = is_client = 0; break; }       // unknown flag
    }
    if (is_server == is_client) {                        // neither, or both
        fprintf(stderr, "usage: livedemo-vpn -s SERVER.conf | -c CLIENT.conf [--tamper] [--replay]\n");
        return 1;
    }
    if (!is_server && !is_client) { // -k is now required for handshake
        fprintf(stderr, "usage: livedemo-vpn -s -k KEY.sk -a ALLOWLIST | -c SERVER_IP -k KEY.sk -p SERVER.pk [--tamper] [--replay]\n");
        return 1;
    }


    /* Load and setup conf */
    struct config cfg; 
    if (load_config(conf_file, &cfg) != 0) die(conf_file);
    if (!cfg.sign_key[0] || !cfg.tun_addr[0] ||
        (is_server && !cfg.clients[0]) ||
        (is_client && (!cfg.server[0] || !cfg.server_key[0] || !cfg.tun_peer[0]))) {
        fprintf(stderr, "%s: missing settings for %s mode\n", conf_file, is_server ? "server" : "client");
        return 1;
    }


    // identity keys set up 
    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 1; }
    read_key(cfg.sign_key, my_sk, sizeof my_sk);
    crypto_sign_ed25519_sk_to_pk(my_pk, my_sk);        /* public key from the secret key */
    if (is_client) read_key(cfg.server_key, server_pk, sizeof server_pk);
    if (is_server) load_allowlist(cfg.clients);

    char ifname[IFNAMSIZ];
    int tun  = tun_open(ifname, sizeof ifname);
    printf("using %s\n", ifname);        // Mac: utun# ; server: tun0

    // Give the new tunnel interface its own IP address (auto add a route to it), and fire it up 
    configure_tun(ifname, &cfg);


    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) die("socket");

    struct sockaddr_in peer = {0};    // who we send to
    int have_peer = 0;                // flag to remember if we have peer client/server

    
    if (is_server) {                      // server: listen on 5555
        struct sockaddr_in me = {0};
        me.sin_family = AF_INET;
        me.sin_port = htons(cfg.port);
        me.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, (struct sockaddr *)&me, sizeof me) < 0) die("bind");
    } else {// is_client                      // client: we know the server
        peer.sin_family = AF_INET;
        peer.sin_port = htons(cfg.port);
        if (inet_pton(AF_INET, cfg.server, &peer.sin_addr) != 1) {
            fprintf(stderr, "bad IP\n"); return 1;
        }
        have_peer = 1;
    }

    /* poll loop*/
    struct pollfd fds[2] = {
        { .fd = tun,  .events = POLLIN },
        { .fd = sock, .events = POLLIN },
    };


    uint8_t buf[2048];
    time_t last_report = time(NULL); 

    /* initial start*/
    if (is_client) start_handshake();  /* build the first HELLO; the loop sends it */
    
    for (;;) {
        // wake up once a sec 
        if (poll(fds, 2, 1000) < 0) die("poll");  // poll wait for packet OR 1 sec at most 
        
        // every 3 secs, if bad or replays updated, print status 
        if (time(NULL) - last_report >= 3 &&
            (bad_packets != last_printed_bad || replays != last_printed_replays)) {
            fprintf(stderr, "stats: bad_packets=%lu replays=%lu\n", bad_packets, replays);
            last_printed_bad = bad_packets;
            last_printed_replays = replays;
            last_report = time(NULL);
        }
        time_t now = time(NULL);
        if (!is_server && !sess.up && now - last_hello >= 2) {
            sendto(sock, hello_pkt, HELLO_LEN, 0, (struct sockaddr *)&peer, sizeof peer);
            last_hello = now;
        }

        /* tunnel -> network */
        if (fds[0].revents & POLLIN) {            
            ssize_t n = tun_read(tun, buf, sizeof buf);
            if (n < 0) die("tun_read");
            if (n == 0 || !sess.up || !have_peer) continue;        // no session yet: drop it, TCP will resend
            
            // Encrypt and send 
            uint8_t sealed[sizeof buf + HDR + TAG];
            size_t sealed_len = seal_packet(&sess, buf, n, sealed);
            if (tamper && sess.send_ctr % 10 == 0){
                sealed[40] ^= 0x01;                 // flip one bit inside the ciphertext
            }
            sendto(sock, sealed, sealed_len, 0, (struct sockaddr *)&peer, sizeof peer);
            if (replay) // send the exact same bytes again, like a replay attack
                sendto(sock, sealed, sealed_len, 0, (struct sockaddr *)&peer, sizeof peer);
        }
    
        /* network -> tunnel */
        if (fds[1].revents & POLLIN) {                 
            // remember from address of last valid packet 
            // sendto() that address when tun0 gets packet 
            struct sockaddr_in from;                   // who sent this packet
            socklen_t len = sizeof from;
            uint8_t pkt[sizeof buf + HDR + TAG];       // raw packet from the network
            uint8_t *plain = buf;                      // decrypted DATA goes into buf
            
            ssize_t n = recvfrom(sock, pkt, sizeof pkt, 0, (struct sockaddr *)&from, &len);
            if (n < 0) die("recvfrom");                // Should try to keep this alive instead???
            if (n < 1) continue;

            switch (pkt[0]) {
            case TYPE_HELLO: {
                if (is_server) {
                    uint8_t resp[RESPONSE_LEN];
                    if (handle_hello(pkt, n, resp) == 0) {
                        peer = from; 
                        have_peer = 1;
                        sendto(sock, resp, RESPONSE_LEN, 0, (struct sockaddr *)&peer, sizeof peer);
                        fprintf(stderr, "session up\n");
                    }
                }
                break;
            }
            case TYPE_RESPONSE: {
                if (!is_server && handle_response(pkt, n) == 0) fprintf(stderr, "session up\n");
                break;
            }
            case TYPE_DATA: {
                uint64_t ctr;
                if (!sess.up) break;
                ssize_t m = open_packet(&sess, pkt, n, plain, &ctr);
                if (m < 0) { 
                    bad_packets++; 
                    fprintf(stderr, "dropped bad packet (%lu so far)\n", bad_packets); 
                    break; 
                }
                if (!replay_ok(&sess.win, ctr)) {                //Maybe should add WIndow feature???
                    replays++; 
                    fprintf(stderr, "dropped replay (%lu so far)\n", replays); 
                    break;
                }
                if (is_server) peer = from;              /* follow the client to a new address */
                if (m > 0) tun_write(tun, plain, m);     /* m == 0 is a keepalive, maybe should improve this ??? */ 
                break;
            }
            default:
                break;                                   /* ignore unknown type of data */
            }
        }
    }
}