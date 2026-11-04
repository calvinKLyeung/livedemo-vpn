CFLAGS = -Wall -Wextra -g -O2 $(shell pkg-config --cflags libsodium)
LDLIBS = $(shell pkg-config --libs libsodium)

mini-vpn: mini-vpn.c common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f min-vpn