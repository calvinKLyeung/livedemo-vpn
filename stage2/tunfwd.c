#include <stdio.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

int main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: %s SERVER_IP\n", argv[0]); return 1; }

    char ifname[IFNAMSIZ];
    int tun  = tun_open(ifname, sizeof ifname);
    printf("tun interface: %s\n", ifname);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) die("socket");

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(5555);
    if (inet_pton(AF_INET, argv[1], &srv.sin_addr) != 1) {
        fprintf(stderr, "bad IP\n"); return 1;
    }

    uint8_t buf[2048];
    for (;;) {
        ssize_t n = tun_read(tun, buf, sizeof buf);
        if (n < 0) die("tun_read");
        if (n == 0) continue;
        print_ip(buf, n);
        if (sendto(sock, buf, n, 0, (struct sockaddr *)&srv, sizeof srv) < 0)
            perror("sendto");
    }
}