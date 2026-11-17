#include "replay.h"

void window_reset(struct window *w) {
    w->top  = 0;
    w->bits = 0;
}

int replay_ok(struct window *w, uint64_t c) {
    if (c == 0) return 0;    /* counters should start at 1*/

    /* newer packet: slide forward : )*/
    if (c > w->top) {        
        uint64_t shift = c - w->top;
        w->bits = (shift >= 64) ? 0 : (w->bits << shift);   /* shifting by 64+ is undefined in C language : ( */
        w->bits |= 1;        /* bit 0 == c == set to 1 by bit OR */
        w->top = c; 
        return 1;
    }
    /* No need to slide forward */
    uint64_t back = w->top - c;   /* diff == gap to set and fill */
    if (back >= 64) return 0;     /* too old: drop it */
    uint64_t mask = (uint64_t)1 << back; 
    if(w->bits & mask) return 0;  /* seen: must be replay */
    w->bits |= mask;
    return 1;
}