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

#define HDR 32                                          // 8 bytes counter for order and 24 bytes nonce as required 
#define TAG crypto_aead_xchacha20poly1305_ietf_ABYTES   // 16 

static unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];

// tracking packets 
static uint64_t send_counter = 0;           // out going packet track
static unsigned long bad_packets = 0;       // failed decryption 
static unsigned long replays = 0;           // old or repeated counter
static uint64_t highest_seen = 0;           // highest counter accepted so far 

// flags for argv 
static int tamper = 0, replay = 0;
static unsigned long last_printed_bad = 0;
static unsigned long last_printed_replays = 0;


void load_key(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("open key file");
    if (fread(key, 1, sizeof key, f) != sizeof key) {
        fprintf(stderr, "key file must be exactly %zu bytes\n", sizeof key);
        exit(1);
    }
    fclose(f);
}

// encrypt packet from in to out, and return updated byte size 
size_t seal_packet(const uint8_t *in, size_t n, uint8_t *out) {
    put_be64(out, ++send_counter);   // 0-7 bytes, counter 
    randombytes_buf(out + 8, 24);    // 8-31 bytes, nonce 

    unsigned long long clen;    // ciphertext len 
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        out + HDR, &clen, 
        in, n,
        out, HDR,               // authentucated 
        NULL, out + 8, key
    );
    return HDR + clen;       // encrypted data len n + 16 bytes tag seal

}

// decrypt from in to out, return plaintext len 
// or return -1 if packet is bad, like tampered, wrong key, len problem? 
ssize_t open_packet(const uint8_t *in, size_t n, uint8_t *out) {
    if (n < HDR + TAG) return -1; 

    unsigned long long mlen;    // message plaintext len 
    if (crypto_aead_xchacha20poly1305_ietf_decrypt(
            out , &mlen, NULL,
            in + HDR, n - HDR,  // ciphertext + tag 
            in, HDR,            // same header as additional data 
            in + 8, key         // nonce 
        ) != 0)                 // non 0 means failed 
        return -1; 
    
    return (ssize_t) mlen;
}


int main(int argc, char **argv) {
    int is_server = 0;
    const char *server_ip = NULL;
    for (int i = 1; i < argc; i++) {
        if      (!strcmp(argv[i], "-s"))                 is_server = 1;
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) server_ip = argv[++i];
        else if (!strcmp(argv[i], "--tamper"))           tamper = 1;
        else if (!strcmp(argv[i], "--replay"))           replay = 1;
        else { 
            // unknown flags 
            server_ip = NULL; 
            is_server = 0; 
            break; 
        }  
    }
    int is_client = (server_ip != NULL);
    if (is_server && is_client) {
        fprintf(stderr, "choose -s or -c, not both\n");
        return 1;
    }
    if (!is_server && !is_client) {
        fprintf(stderr, "usage: livedemo-vpn -s | livedemo-vpn -c SERVER_IP [--tamper] [--replay]\n");
        return 1;
    }

    // vpnkey 
    if (sodium_init() < 0) { fprintf(stderr, "sodium_init failed\n"); return 1; }
    load_key("vpn.key"); 

    char ifname[IFNAMSIZ];
    int tun  = tun_open(ifname, sizeof ifname);
    printf("using %s\n", ifname);        // Mac: utun# ; server: tun0

    // Give the new tunnel interface its own IP address (auto add a route to it), and fire it up 
    char cmd[256];
    #ifdef __APPLE__
    snprintf(cmd, sizeof cmd, "ifconfig %s 10.8.0.2 10.8.0.1 mtu 1400 up", ifname);
    #else
    snprintf(cmd, sizeof cmd, "ip addr add 10.8.0.1/24 dev %s && ip link set %s up mtu 1400",ifname, ifname);
    #endif
    if (system(cmd) != 0) { fprintf(stderr, "failed: %s\n", cmd); exit(1); }


    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) die("socket");

    struct sockaddr_in peer = {0};    // who we send to
    int have_peer = 0;                // flag to remember if we have peer client/server

    
    if (is_server) {                      // server: listen on 5555
        struct sockaddr_in me = {0};
        me.sin_family = AF_INET;
        me.sin_port = htons(5555);
        me.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, (struct sockaddr *)&me, sizeof me) < 0) die("bind");
    } else {// is_client                      // client: we know the server
        peer.sin_family = AF_INET;
        peer.sin_port = htons(5555);
        if (inet_pton(AF_INET, server_ip, &peer.sin_addr) != 1) {
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

    
        if (fds[0].revents & POLLIN) {                 // tunnel -> network
            ssize_t n = tun_read(tun, buf, sizeof buf);
            if (n < 0) die("tun_read");
            if (n == 0 || !have_peer) continue;        // server: with no client yet
            
            // Encrypt and send 
            uint8_t sealed[sizeof buf + HDR + TAG];
            size_t sealed_len = seal_packet(buf, n, sealed);

            if (tamper && send_counter % 10 == 0){
                sealed[40] ^= 0x01;                 // flip one bit inside the ciphertext
            }
            sendto(sock, sealed, sealed_len, 0, (struct sockaddr *)&peer, sizeof peer);
            if (replay) // send the exact same bytes again, like a replay attack
                sendto(sock, sealed, sealed_len, 0, (struct sockaddr *)&peer, sizeof peer);
        }
    

        if (fds[1].revents & POLLIN) {                 // network -> tunnel
            // remember from address of last valid packet 
            // sendto() that address when tun0 gets packet 
            struct sockaddr_in from;  
            socklen_t len = sizeof from;

            // Receive and decrypt 
            uint8_t sealed[sizeof buf + HDR + TAG];
            ssize_t n = recvfrom(sock, sealed, sizeof sealed, 0, (struct sockaddr *)&from, &len);
            if (n < 0) die("recvfrom");

            ssize_t m = open_packet(sealed, n, buf);   // drop stuffs that fails 
            if (m < 0) {
                bad_packets++;
                fprintf(stderr, "dropped bad packet (%lu so far)\n", bad_packets);  
                continue;
            }

            // Reply checks 
            uint64_t counter = get_be64(sealed);
            if (counter <= highest_seen) {
                replays++;
                continue;   // seen or older, drop 
            }
            highest_seen = counter; 


            if (is_server) { // only trust and remember peer starting from here 
                peer = from; 
                have_peer = 1; 
            }
            if (tun_write(tun, buf, m) < 0) perror("tun_write"); // not exist on single bad packet 
            
        }
    }

}