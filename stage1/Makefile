CFLAGS = -Wall -Wextra -g

all: udp_echo_server udp_echo_client

udp_echo_server: udp_echo_server.c
	gcc $(CFLAGS) -o $@ $<

udp_echo_client: udp_echo_client.c
	gcc $(CFLAGS) -o $@ $<

clean:
	rm -f udp_echo_server udp_echo_client