#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"

// static void die(const char *m) {
//     perror(m);
//     exit(1);
// }

int main (void) {
    int s = socket(AF_INET, SOCK_DGRAM, 0);   // UDP 
    if (s < 0) die ("socket");

    struct sockaddr_in me = {0};
    me.sin_family = AF_INET;
    me.sin_port = htons(5555);
    me.sin_addr.s_addr = htonl(INADDR_ANY); // listen on every interface
    if (bind(s, (struct sockaddr *)&me, sizeof me) < 0) die("bind"); // bind socket and port tgt 
    printf("listening on UDP 5555\n");



    char buf[2000];
    for (;;) {
        struct sockaddr_in from;
        socklen_t len = sizeof from;
        ssize_t n = recvfrom(s, buf, sizeof buf, 0, (struct sockaddr *)&from, &len);
        if (n < 0) die("recvfrom");
        // decode the arrived data 
        print_ip((uint8_t *)buf, n);

        // char ip[INET_ADDRSTRLEN];
        // inet_ntop(AF_INET, &from.sin_addr, ip, sizeof ip); // turn binary address of IP to char arr 
        // printf("got %zd bytes from %s:%d\n", n, ip, ntohs(from.sin_port));
        // // send the same n bytes back to 'from' using sendto()
        // ssize_t sent_n = sendto(s, buf, n, 0, (struct sockaddr *)&from, len);
        // if (sent_n < 0) die("sendto");
    }


    return 0;
}

