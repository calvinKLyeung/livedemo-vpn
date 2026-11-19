CFLAGS = -Wall -Wextra -g -O2 $(shell pkg-config --cflags libsodium)
VERSION := $(shell git rev-parse --short HEAD 2>/dev/null || echo dev)
CFLAGS  += -DVERSION=\"$(VERSION)\"
LDLIBS = $(shell pkg-config --libs libsodium)
PREFIX ?= /usr/local

all: livedemo-vpn keygen

livedemo-vpn: livedemo-vpn.c common.c replay.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

keygen: keygen.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

test_window: test_window.c replay.c
	$(CC) $(CFLAGS) -o $@ $^


test: test_window
	./test_window

install: livedemo-vpn
	install -d $(PREFIX)/bin                                   
	install -m 755 livedemo-vpn $(PREFIX)/bin/livedemo-vpn    

clean:
	rm -f livedemo-vpn keygen test_window

.PHONY: all clean test install