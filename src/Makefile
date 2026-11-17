CFLAGS = -Wall -Wextra -g -O2 $(shell pkg-config --cflags libsodium)
LDLIBS = $(shell pkg-config --libs libsodium)

all: livedemo-vpn keygen

livedemo-vpn: livedemo-vpn.c common.c replay.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

keygen: keygen.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_window: test_window.c replay.c
	$(CC) $(CFLAGS) -o $@ $^


test: test_window
	./test_window

clean:
	rm -f livedemo-vpn keygen test_window

.PHONY: all clean