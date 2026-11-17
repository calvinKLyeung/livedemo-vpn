#include <stdint.h>

/* Reference RFC 4303 §3.4.3: Sequence Number Verification */
struct window {
    uint64_t top;
    uint64_t bits;   /* bit i is 1 if counter (top - i) has been received */
}; 

void window_reset(struct window *w);
int replay_ok(struct window *w, uint64_t c);

