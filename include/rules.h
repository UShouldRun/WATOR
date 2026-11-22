#ifndef WATOR_RULES_H
#define WATOR_RULES_H

#include "cell.h"
#include "process.h"

// Rules only touch cells of the zone owned by p (reads may reach the
// neighbours' boundary rows). They know nothing about threads or sync.

// `gen` is the current generation number G (starts at 0), used by the
// (G+X+Y) mod P cell-selection criterion.
void rabbit_substep(Process *p, int gen, const Cell *src, Cell *dst);
void fox_substep(Process *p, int gen, const Cell *src, Cell *dst);

#endif
