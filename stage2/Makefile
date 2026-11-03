CFLAGS = -Wall -Wextra -g -O2 $(shell pkg-config --cflags libsodium)
LDLIBS = $(shell pkg-config --libs libsodium)

tunprint: tunprint.c common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tunfwd: tunfwd.c common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

tunrecv: tunrecv.c common.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

clean:
	rm -f tunprint tunfwd tunrecv