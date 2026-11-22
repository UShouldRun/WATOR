#ifndef WATOR_SYNC_H
#define WATOR_SYNC_H

#include "process.h"

// Blocks until it is safe for p to run sub-step number `substep`
// (0-based; substep s reads buf[s%2] and writes buf[(s+1)%2]).
void sync_wait_neighbours(const Process *p, int substep);

// Marks `completed` sub-steps as done for p.
void sync_publish(Process *p, int completed);

#endif
