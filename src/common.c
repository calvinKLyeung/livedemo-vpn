#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "common.h"

#include <arpa/inet.h>
#include <stdio.h>

void die(const char *msg) { perror(msg); exit(1); }

#ifdef __APPLE__
/* ---------- macOS: utun ---------- */
#include <sys/kern_control.h>
#include <sys/sys_domain.h>
#include <net/if_utun.h>

int tun_open(char *ifname, size_t len) {
    int fd = socket(PF_SYSTEM, SOCK_DGRAM, SYSPROTO_CONTROL);
    if (fd < 0) die("socket(PF_SYSTEM)");

    struct ctl_info ci;
    memset(&ci, 0, sizeof ci);
    strlcpy(ci.ctl_name, UTUN_CONTROL_NAME, sizeof ci.ctl_name);
    if (ioctl(fd, CTLIOCGINFO, &ci) < 0) die("CTLIOCGINFO");

    struct sockaddr_ctl sc;
    memset(&sc, 0, sizeof sc);
    sc.sc_len     = sizeof sc;
    sc.sc_family  = AF_SYSTEM;
    sc.ss_sysaddr = AF_SYS_CONTROL;
    sc.sc_id      = ci.ctl_id;
    sc.sc_unit    = 0;                    // 0 = macOS picks the next free utunN
    if (connect(fd, (struct sockaddr *)&sc, sizeof sc) < 0)
        die("connect utun (did you use sudo?)");

    socklen_t n = (socklen_t)len;
    if (getsockopt(fd, SYSPROTO_CONTROL, UTUN_OPT_IFNAME, ifname, &n) < 0)
        die("UTUN_OPT_IFNAME");
    return fd;
}

/* utun puts a 4-byte address family in front of every packet */
ssize_t tun_read(int fd, uint8_t *buf, size_t len) {
    uint8_t tmp[4 + 2048];
    ssize_t n = read(fd, tmp, sizeof tmp);
    if (n < 0) return n;
    if (n < 4 || (size_t)(n - 4) > len) return 0;   // nothing useful
    memcpy(buf, tmp + 4, n - 4);                     // strip the prefix
    return n - 4;
}

ssize_t tun_write(int fd, const uint8_t *buf, size_t len) {
    uint8_t tmp[4 + 2048];
    if (len == 0 || len > 2048) return -1;
    uint32_t af = htonl((buf[0] >> 4) == 6 ? AF_INET6 : AF_INET);
    memcpy(tmp, &af, 4);                             // add the prefix back
    memcpy(tmp + 4, buf, len);
    ssize_t n = write(fd, tmp, len + 4);
    return n < 0 ? n : n - 4;
}

#else
/* ---------- Linux: tun0 ---------- */
#include <fcntl.h>
#include <net/if.h>  
#include <linux/if_tun.h>

int tun_open(char *ifname, size_t len) {
    int fd = open("/dev/net/tun", O_RDWR);
    if (fd < 0) die("open /dev/net/tun");

    struct ifreq ifr;
    memset(&ifr, 0, sizeof ifr);
    ifr.ifr_flags = IFF_TUN | IFF_NO_PI;             // raw IP packets, no prefix
    strncpy(ifr.ifr_name, "tun0", IFNAMSIZ - 1);
    if (ioctl(fd, TUNSETIFF, &ifr) < 0) die("TUNSETIFF (did you use sudo?)");

    strncpy(ifname, ifr.ifr_name, len - 1);
    ifname[len - 1] = '\0';
    return fd;
}

ssize_t tun_read(int fd, uint8_t *buf, size_t len)        { return read(fd, buf, len); }
ssize_t tun_write(int fd, const uint8_t *buf, size_t len) { return write(fd, buf, len); }
#endif

/* ---------- portable big-endian helpers (macOS has no htobe64) ---------- */
// basically host to big endian 64 but for mac lol
void put_be64(uint8_t *p, uint64_t v) {
    for (int i = 7; i >= 0; i--) { p[i] = v & 0xff; v >>= 8; }
}
uint64_t get_be64(const uint8_t *p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; i++) v = (v << 8) | p[i];
    return v;
}

/* print_ip() */
void print_ip(const uint8_t *p, ssize_t n) {
    if (n < 20) { printf("too short (%zd bytes)\n", n); return; }

    int version = p[0] >> 4;  //[4 bits version][4 bits IHL]
    if (version != 4) { printf("IPv%d packet, %zd bytes (skipping)\n", version, n); return; }

    int hdr_len = (p[0] & 0x0F) * 4;   // get Internet Header Length, usually 20, stored in / 4 form 
    int proto   = p[9];

    char src[INET_ADDRSTRLEN], dst[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, p + 12, src, sizeof src);
    inet_ntop(AF_INET, p + 16, dst, sizeof dst);

    printf("IPv4 %s -> %s  proto=%d  header=%d  total=%zd\n",
           src, dst, proto, hdr_len, n);
}