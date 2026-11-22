#include "world.h"

#include <stdlib.h>
#include <string.h>

void world_alloc(World *w, int R, int C) {
    w->R = R;
    w->C = C;
    size_t n = (size_t)R * C;
    w->buf[0] = calloc(n ? n : 1, sizeof(Cell));
    w->buf[1] = calloc(n ? n : 1, sizeof(Cell));
}

void world_free(World *w) {
    free(w->buf[0]);
    free(w->buf[1]);
    w->buf[0] = w->buf[1] = NULL;
}

// Only the simulation fields of w->p are set; n_threads etc. are left untouched.
int world_load(World *w, FILE *f) {
    Params *p = &w->p;
    int R, C, N;

    if (fscanf(f, "%d %d %d %d %d %d %d",
               &p->gen_proc_rabbits, &p->gen_proc_foxes, &p->gen_food_foxes,
               &p->n_gen, &R, &C, &N) != 7)
        return -1;
    if (R <= 0 || C <= 0 || N < 0 || p->n_gen < 0)
        return -1;

    world_alloc(w, R, C);
    if (!w->buf[0] || !w->buf[1]) {
        world_free(w);
        return -1;
    }

    for (int i = 0; i < N; i++) {
        char name[16];
        int x, y;
        if (fscanf(f, "%15s %d %d", name, &x, &y) != 3) goto fail;
        if (x < 0 || x >= R || y < 0 || y >= C) goto fail;

        Cell *c = cell_at(w->buf[0], w, x, y);
        if (c->type != EMPTY) goto fail;   // two objects on the same cell

        if      (strcmp(name, "ROCK")   == 0) c->type = ROCK;
        else if (strcmp(name, "RABBIT") == 0) c->type = RABBIT;
        else if (strcmp(name, "FOX")    == 0) c->type = FOX;
        else goto fail;
        // proc_age and food_age start at 0 (buffers are zeroed).
    }
    return 0;

fail:
    world_free(w);
    return -1;
}

// Output format = input format, with N_GEN printed as 0 and N the number of
// objects in the final state, listed in row-major order.
void world_print(const World *w, int which, FILE *out) {
    const Cell *g = w->buf[which];
    size_t n = (size_t)w->R * w->C;

    long count = 0;
    for (size_t i = 0; i < n; i++)
        if (g[i].type != EMPTY) count++;

    fprintf(out, "%d %d %d %d %d %d %ld\n",
            w->p.gen_proc_rabbits, w->p.gen_proc_foxes, w->p.gen_food_foxes,
            0, w->R, w->C, count);

    static const char *const names[] = {
        [ROCK] = "ROCK", [RABBIT] = "RABBIT", [FOX] = "FOX"
    };
    for (int r = 0; r < w->R; r++) {
        for (int c = 0; c < w->C; c++) {
            uint8_t t = g[(size_t)r * w->C + c].type;
            if (t != EMPTY) fprintf(out, "%s %d %d\n", names[t], r, c);
        }
    }
}
