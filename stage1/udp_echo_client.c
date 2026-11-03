#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>

static const int PING_COUNT = 100;

static void die(const char *m) { perror(m); exit(1); }

int main(int argc, char **argv) {
    if (argc != 2 && argc != 3) {
        fprintf(stderr, "usage: %s SERVER_IP [MESSAGE]\n", argv[0]);
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

    struct timeval tv = { .tv_sec = 1 };              // give up after 1 seconds
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv); // set timeout 

    if (argc == 3) {
        size_t len = strlen(argv[2]);   // fine here: the message really is text
        if (sendto(s, argv[2], len, 0, (struct sockaddr *)&srv, sizeof srv) < 0)
            die("sendto");

        char buf[2000];
        ssize_t n = recv(s, buf, sizeof buf, 0);  // receive echo 
        if (n < 0) die("recv (no reply within 2 s?)");
        printf("reply (%zd bytes): %.*s\n", n, (int)n, buf);   // print exactly n bytes
    } else { //if (argc == 2)  for timing 
        int received = 0;
        double sum = 0, min = 1e9, max = 0;
        for (int i = 0; i < PING_COUNT; i++) {
            // set up time vars 
            struct timespec t0, t1;
            clock_gettime(CLOCK_MONOTONIC, &t0); // start timer 


            char msg[32];
            int mlen = snprintf(msg, sizeof msg, "ping %d", i);   // numbered message
        
            // send msg, wait for reply, time it 
            if (sendto(s, msg, mlen, 0, (struct sockaddr *)&srv, sizeof srv) < 0)
                die("sendto");
    
            char buf[2000];
            ssize_t n;
            int seq;
        
            for (;;) {
                n = recv(s, buf, sizeof buf - 1, 0);  
                if (n < 0) break;  
                buf[n] = '\0'; // turn it to str for scanf 
                if (sscanf(buf, "ping %d", &seq) == 1 && seq == i) 
                    break; // correct reply : )
                // else loop to receive again 
            }
            
            clock_gettime(CLOCK_MONOTONIC, &t1); // stop timer 

            
            // continue to next ping if lost : (
            if (n < 0) {
                printf("ping %d: lost somewhere in the abyss\n", i);
                continue; 
            }
            printf("reply (%zd bytes): %.*s\n", n, (int)n, buf);   // print exactly n bytes

            // convert to milisec and get sum 
            double ms = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6;

            received += 1;
            sum += ms; 
            if (ms < min) min = ms; 
            if (ms > max) max = ms;
        }
        printf("%d sent, %d received, %.1f%% lost\n", PING_COUNT, received, (PING_COUNT - received) * 100.0 / PING_COUNT);
        if (received > 0)
            printf("rtt min/avg/max = %.3f/%.3f/%.3f ms\n", min, sum / received, max);
    }
    return 0;
}