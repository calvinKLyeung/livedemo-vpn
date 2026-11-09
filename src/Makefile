CFLAGS = -Wall -Wextra -g -O2 $(shell pkg-config --cflags libsodium)
LDLIBS = $(shell pkg-config --libs libsodium)

livedemo-vpn: livedemo-vpn.c common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

keygen: keygen.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f livedemo-vpn