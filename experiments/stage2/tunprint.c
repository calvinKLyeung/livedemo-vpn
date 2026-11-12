#include <stdio.h>
#include "common.h"

int main(void) {
    char ifname[IFNAMSIZ];
    int tun = tun_open(ifname, sizeof ifname);
    printf("tun interface: %s\n", ifname);

    uint8_t buf[2048];
    for (;;) {
        ssize_t n = tun_read(tun, buf, sizeof buf);
        if (n < 0) die("tun_read");
        print_ip(buf, n);
    }
}