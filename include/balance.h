#ifndef WATOR_BALANCE_H
#define WATOR_BALANCE_H

#include "process.h"

// Total animals in p's zone, from animals_per_row.
long count_animals(const Process *p);

// Barrier every `rebalance_every` generations: count animals per worker and,
// if the imbalance is large, resize the zones and move animals_per_row data.
void rebalance(Process *procs, int T);

#endif
