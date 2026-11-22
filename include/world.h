#ifndef WATOR_WORLD_H
#define WATOR_WORLD_H

#include <stddef.h>
#include <stdio.h>

#include "cell.h"
#include "params.h"

typedef struct {
    int    R, C;
    Cell  *buf[2];   // rabbit sub-step: A->B, fox sub-step: B->A
    Params p;
} World;

static inline Cell *cell_at(Cell *g, const World *w, int r, int c) {
    return &g[(size_t)r * w->C + c];
}

// Allocates both buffers (zeroed) for an R x C grid.
void world_alloc(World *w, int R, int C);
void world_free(World *w);

// Parses parameters + initial entities from f into buf[0]. Returns 0 on success.
int  world_load(World *w, FILE *f);

// Prints buf[which] in the required output format.
void world_print(const World *w, int which, FILE *out);

#endif
