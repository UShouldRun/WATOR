#ifndef WATOR_SEQUENTIAL_H
#define WATOR_SEQUENTIAL_H

#include "world.h"

// Runs the whole simulation (w->p.n_gen generations) on the calling thread.
// Reads the initial state from w->buf[0]; after 2 sub-steps per generation the
// final state is back in w->buf[0]. Returns 0 on success, -1 on allocation failure.
int sequential_run(World *w);

#endif
