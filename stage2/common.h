#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>
#include <net/if.h>          // IFNAMSIZ

void     die(const char *msg);
int      tun_open(char *ifname, size_t len);   // fills in the interface name
ssize_t  tun_read(int fd, uint8_t *buf, size_t len);
ssize_t  tun_write(int fd, const uint8_t *buf, size_t len);
void     print_ip(const uint8_t *p, ssize_t n);   // for later 
void     put_be64(uint8_t *p, uint64_t v);        // for later 
uint64_t get_be64(const uint8_t *p);

#endif