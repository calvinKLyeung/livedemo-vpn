#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "common.h"


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
        // basic one way tunnel from clident to server, and print the content ip
        print_ip((uint8_t *)buf, n);

    }


    return 0;
}

