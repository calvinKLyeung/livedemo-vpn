#include <assert.h>
#include <stdio.h>
#include "replay.h"

int main(void) {
    struct window w;
    window_reset(&w);
    assert(replay_ok(&w, 1));
    assert(replay_ok(&w, 2));
    assert(!replay_ok(&w, 2));      /* duplicate */
    assert(replay_ok(&w, 5));
    assert(replay_ok(&w, 4));       /* late but genuine */
    assert(replay_ok(&w, 3));
    assert(!replay_ok(&w, 4));      /* replay of a late packet */
    assert(replay_ok(&w, 200));     /* big jump */
    assert(!replay_ok(&w, 136));    /* 64 behind: too old */
    assert(replay_ok(&w, 137));     /* 63 behind: still in the window */
    assert(!replay_ok(&w, 0));
    puts("window tests passed");
    return 0;
}