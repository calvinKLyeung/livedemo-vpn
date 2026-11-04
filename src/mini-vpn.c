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

static unsigned char key[crypto_aead_xchacha20poly1305_ietf_KEYBYTES];

void load_key(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) die("open key file");
    if (fread(key, 1, sizeof key, f) != sizeof key) {
        fprintf(stderr, "key file must be exactly %zu bytes\n", sizeof key);
        exit(1);
    }
    fclose(f);
}

#define HDR 32                                          // 8 bytes counter for order and 24 bytes nonce as required 
#define TAG crypto_aead_xchacha20poly1305_ietf_ABYTES   // 16 

static uint64_t send_counter = 0; 

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




int main(int argc, char **argv) {
    int is_server = (argc == 2 && strcmp(argv[1], "-s") == 0);
    int is_client = (argc == 3 && strcmp(argv[1], "-c") == 0);
    if (!is_server && !is_client) {
        fprintf(stderr, "usage: myvpn -s | myvpn -c SERVER_IP\n");
        return 1;
    }

    char path[10];
    // vpnkey 
    load_key(path);  // path as placeholder for now 

    char ifname[IFNAMSIZ];
    int tun  = tun_open(ifname, sizeof ifname);
    printf("using %s\n", ifname);        // Mac: utun# ; server: tun0

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) die("socket");

    struct sockaddr_in peer = {0};    // who we send to
    int have_peer = 0;                // flag to remember if we have peer client/server

    if (is_server) {                  // server: listen on 5555
        struct sockaddr_in me = {0};
        me.sin_family = AF_INET;
        me.sin_port = htons(5555);
        me.sin_addr.s_addr = htonl(INADDR_ANY);
        if (bind(sock, (struct sockaddr *)&me, sizeof me) < 0) die("bind");
    } else {                          // client: we know the server
        peer.sin_family = AF_INET;
        peer.sin_port = htons(5555);
        if (inet_pton(AF_INET, argv[2], &peer.sin_addr) != 1) {
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
    
    for (;;) {
        // -1 means wait forever 
        if (poll(fds, 2, -1) < 0) die("poll");
    

        if (fds[0].revents & POLLIN) {                 // tunnel -> network
            ssize_t n = tun_read(tun, buf, sizeof buf);
            if (n < 0) die("tun_read");
            if (n == 0 || !have_peer) continue;        // server: with no client yet

            uint8_t sealed[2000 + HDR + TAG];
            size_t sealed_len = seal_packet(buf, n, sealed);
            sendto(sock, sealed, sealed_len, 0, (struct sockaddr *)&peer, sizeof peer);
        }
    

        if (fds[1].revents & POLLIN) {                 // network -> tunnel
            // remember from address of last valid packet 
            // sendto() that address when tun0 gets packet 
            struct sockaddr_in from;  
            socklen_t len = sizeof from;
            ssize_t n = recvfrom(sock, buf, sizeof buf, 0, (struct sockaddr *)&from, &len);
            if (n < 0) die("recvfrom");

            if (is_server) { // remember where the client is to reply to
                peer = from; 
                have_peer = 1; 
            }
            if (tun_write(tun, buf, n) < 0) perror("tun_write"); // not exist on single bad packet 
            

            // Note: current design == any one who send to port 5555 of the server 
            // will auto become the peer lol 
        }
    }

}