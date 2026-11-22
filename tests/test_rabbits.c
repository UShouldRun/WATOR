#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "process.h"
#include "rules.h"
#include "world.h"

// Independent "push" reference: every rabbit picks its destination from src,
// movers compete for the destination cell, the oldest wins.
static void ref_rabbit_step(const World *w, int gen, const Cell *src, Cell *dst) {
    static const int DR[4] = { -1, 0, 1, 0 }, DC[4] = { 0, 1, 0, -1 };
    const int PR = w->p.gen_proc_rabbits;
    size_t n = (size_t)w->R * w->C;
    int *best = malloc(n * sizeof(int));
    for (size_t i = 0; i < n; i++) { best[i] = -1; dst[i] = (Cell){ EMPTY, 0, 0 }; }

    for (int r = 0; r < w->R; r++)
        for (int c = 0; c < w->C; c++) {
            size_t i = (size_t)r * w->C + c;
            Cell cur = src[i];
            if (cur.type == ROCK || cur.type == FOX) { dst[i] = cur; continue; }
            if (cur.type != RABBIT) continue;

            size_t opts[4]; int k = 0;
            for (int d = 0; d < 4; d++) {
                int rr = r + DR[d], cc = c + DC[d];
                if (rr < 0 || rr >= w->R || cc < 0 || cc >= w->C) continue;
                if (src[(size_t)rr * w->C + cc].type == EMPTY)
                    opts[k++] = (size_t)rr * w->C + cc;
            }
            if (!k) {
                dst[i] = (Cell){ RABBIT, cur.proc_age < 255 ? cur.proc_age + 1 : 255, 0 };
                continue;
            }
            size_t d = opts[(gen + r + c) % k];
            if (cur.proc_age >= PR) dst[i] = (Cell){ RABBIT, 0, 0 };   // baby
            if ((int)cur.proc_age > best[d]) best[d] = cur.proc_age;
        }
    for (size_t i = 0; i < n; i++)
        if (best[i] >= 0)
            dst[i] = (Cell){ RABBIT, best[i] >= PR ? 0 : best[i] + 1, 0 };
    free(best);
}

static void random_world(World *w, int density) {
    for (int i = 0; i < w->R * w->C; i++) {
        int x = rand() % 100;
        Cell c = { EMPTY, 0, 0 };
        if (x < density) {
            int t = rand() % 4;
            if (t == 0) c.type = ROCK;
            else if (t == 3) { c.type = FOX; c.proc_age = rand() % 4; c.food_age = rand() % 3; }
            else { c.type = RABBIT; c.proc_age = rand() % (w->p.gen_proc_rabbits + 3); }
        }
        w->buf[0][i] = c;
    }
}

// Runs the real rabbit_substep zone by zone (sequentially) and compares each
// generation with the reference, for many random worlds and thread counts.
static void test_against_reference(void) {
    srand(12345);
    for (int iter = 0; iter < 3000; iter++) {
        World w = {0};
        int R = 1 + rand() % 14, C = 1 + rand() % 12;
        world_alloc(&w, R, C);
        w.p.gen_proc_rabbits = 1 + rand() % 4;
        random_world(&w, (int[]){ 5, 15, 40, 80 }[rand() % 4]);

        int T = 1 + rand() % (R / 2 > 0 ? R / 2 : 1);   // zones have >= 2 rows
        Process procs[16] = {0};
        process_init_all(procs, &w, T);

        Cell *ref = malloc((size_t)R * C * sizeof(Cell));
        for (int gen = 0; gen < 6; gen++) {
            ref_rabbit_step(&w, gen, w.buf[0], ref);
            for (int t = 0; t < T; t++)
                rabbit_substep(&procs[t], gen, w.buf[0], w.buf[1]);
            assert(memcmp(ref, w.buf[1], (size_t)R * C * sizeof(Cell)) == 0);

            // animals_per_row must describe the new grid
            for (int t = 0; t < T; t++)
                for (int r = procs[t].row_begin; r < procs[t].row_end; r++) {
                    int n = 0;
                    for (int c = 0; c < C; c++) {
                        uint8_t ty = w.buf[1][(size_t)r * C + c].type;
                        n += ty == RABBIT || ty == FOX;
                    }
                    assert(procs[t].animals_per_row[r] == n);
                }
            memcpy(w.buf[0], w.buf[1], (size_t)R * C * sizeof(Cell));
        }
        free(ref);
        process_destroy_all(procs, T);
        world_free(&w);
    }
}

// Hand-checked cases from the course example (generation 0 -> 1, rabbits only).
static void test_example(void) {
    World w = {0};
    world_alloc(&w, 5, 5);
    w.p.gen_proc_rabbits = 2;
    *cell_at(w.buf[0], &w, 0, 0) = (Cell){ ROCK, 0, 0 };
    *cell_at(w.buf[0], &w, 0, 2) = (Cell){ RABBIT, 0, 0 };
    *cell_at(w.buf[0], &w, 0, 4) = (Cell){ FOX, 0, 0 };
    *cell_at(w.buf[0], &w, 3, 0) = (Cell){ RABBIT, 0, 0 };
    *cell_at(w.buf[0], &w, 4, 0) = (Cell){ RABBIT, 0, 0 };

    Process procs[2] = {0};
    process_init_all(procs, &w, 2);
    for (int t = 0; t < 2; t++) rabbit_substep(&procs[t], 0, w.buf[0], w.buf[1]);

    assert(cell_at(w.buf[1], &w, 0, 1)->type == RABBIT);   // (0,2) -> west
    assert(cell_at(w.buf[1], &w, 0, 2)->type == EMPTY);
    assert(cell_at(w.buf[1], &w, 3, 1)->type == RABBIT);   // (3,0) -> east
    assert(cell_at(w.buf[1], &w, 4, 1)->type == RABBIT);   // (4,0) -> east
    assert(cell_at(w.buf[1], &w, 0, 4)->type == FOX);

    process_destroy_all(procs, 2);
    world_free(&w);
}

int main(void) {
    test_example();
    test_against_reference();
    puts("rabbit tests passed");
    return 0;
}
