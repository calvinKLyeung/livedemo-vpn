#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>

static void die(const char *m) { perror(m); exit(1); }

int main(int argc, char **argv) {
    if (argc != 3) {
        fprintf(stderr, "usage: %s SERVER_IP MESSAGE\n", argv[0]);
        return 1;
    }
    int s = socket(AF_INET, SOCK_DGRAM, 0);
    if (s < 0) die("socket");

    struct sockaddr_in srv = {0};
    srv.sin_family = AF_INET;
    srv.sin_port = htons(5555);
    if (inet_pton(AF_INET, argv[1], &srv.sin_addr) != 1) {
        fprintf(stderr, "not a valid IPv4 address: %s\n", argv[1]);
        return 1;
    }

    struct timeval tv = { .tv_sec = 2 };              // give up after 2 seconds
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); // set timeout 

    size_t len = strlen(argv[2]);   // fine here: the message really is text
    if (sendto(s, argv[2], len, 0, (struct sockaddr *)&srv, sizeof srv) < 0)
        die("sendto");

    char buf[2000];
    ssize_t n = recv(s, buf, sizeof buf, 0);  // receive echo 
    if (n < 0) die("recv (no reply within 2 s?)");
    printf("reply (%zd bytes): %.*s\n", n, (int)n, buf);   // print exactly n bytes
    return 0;
}