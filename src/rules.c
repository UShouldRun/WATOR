#include "rules.h"

#include <string.h>

// Clockwise from north: N, E, S, W.
static const int DR[4] = { -1, 0, 1, 0 };
static const int DC[4] = {  0, 1, 0, -1 };

static inline int in_grid(const World *w, int r, int c) {
    return r >= 0 && r < w->R && c >= 0 && c < w->C;
}

static inline Cell get(const World *w, const Cell *g, int r, int c) {
    return g[(size_t)r * w->C + c];
}

static inline uint8_t age_inc(uint8_t a) { return a < 255 ? a + 1 : 255; }

// Rabbits

// Direction (index into DR/DC) the rabbit at (r,c) moves to in generation
// `gen`, or -1 if it has no empty neighbour.
static int rabbit_dir(const World *w, const Cell *src, int r, int c, int gen) {
    int dirs[4], n = 0;
    for (int d = 0; d < 4; d++) {
        int rr = r + DR[d], cc = c + DC[d];
        if (in_grid(w, rr, cc) && get(w, src, rr, cc).type == EMPTY)
            dirs[n++] = d;
    }
    return n ? dirs[(gen + r + c) % n] : -1;
}

// New content of cell (r,c) after the rabbit sub-step, (a cell looks at who could end up in it).
// This lets every worker write only its own zone.
static Cell rabbit_cell(const World *w, const Cell *src, int r, int c, int gen) {
    const int PR = w->p.gen_proc_rabbits;
    Cell cur = get(w, src, r, c);

    switch (cur.type) {
    case RABBIT:
        if (rabbit_dir(w, src, r, c, gen) < 0)           // stuck: stays, keeps ageing
            return (Cell){ RABBIT, age_inc(cur.proc_age), 0 };
        if (cur.proc_age >= PR)                          // moved away and procreated
            return (Cell){ RABBIT, 0, 0 };               // -> baby left behind
        return (Cell){ EMPTY, 0, 0 };                    // moved away

    case EMPTY: {
        // Rabbits in the 4 neighbours that chose to move here; the oldest wins.
        int best = -1;
        for (int d = 0; d < 4; d++) {
            int qr = r + DR[d], qc = c + DC[d];
            if (!in_grid(w, qr, qc)) continue;
            Cell q = get(w, src, qr, qc);
            if (q.type != RABBIT) continue;
            // q reaches (r,c) iff it moves in the opposite direction of d.
            if (rabbit_dir(w, src, qr, qc, gen) == ((d + 2) & 3) && (int)q.proc_age > best)
                best = q.proc_age;
        }
        if (best < 0) return (Cell){ EMPTY, 0, 0 };
        return (Cell){ RABBIT, best >= PR ? 0 : age_inc((uint8_t)best), 0 };
    }

    default:                                             // ROCK, FOX: untouched
        return cur;
    }
}

// Rabbit sub-step for generation `gen`: reads src, writes only the rows
// [row_begin,row_end) of dst. Reads reach up to 2 rows into the neighbouring
// zones (a neighbour rabbit's choice depends on its own neighbours), so every
// zone must have at least 2 rows.
//
// animals_per_row[r] is kept equal to the number of animals in row r of the
// *result* (saturating at 255), and a row is copied as-is when rows r-1, r
// and r+1 of src (all inside the zone) had no animals.
void rabbit_substep(Process *p, int gen, const Cell *src, Cell *dst) {
    const World *w = p->world;
    uint8_t prev_src = 0;   // src count of row r-1 (animals_per_row[r-1] is already overwritten)

    for (int r = p->row_begin; r < p->row_end; r++) {
        uint8_t cur_src  = p->animals_per_row[r];
        uint8_t next_src = r + 1 < p->row_end ? p->animals_per_row[r + 1] : 0;
        int interior = r > p->row_begin && r < p->row_end - 1;

        if (interior && !prev_src && !cur_src && !next_src) {
            memcpy(&dst[(size_t)r * w->C], &src[(size_t)r * w->C], (size_t)w->C * sizeof(Cell));
            // animals_per_row[r] is already 0
        } else {
            int count = 0;
            for (int c = 0; c < w->C; c++) {
                Cell out = rabbit_cell(w, src, r, c, gen);
                dst[(size_t)r * w->C + c] = out;
                if (out.type == RABBIT || out.type == FOX) count++;
            }
            p->animals_per_row[r] = count > 255 ? 255 : (uint8_t)count;
        }
        prev_src = cur_src;
    }
}

// Foxes

void fox_substep(Process *p, int gen, const Cell *src, Cell *dst) {
    // TODO: move/eat/breed/starve foxes, resolve conflicts,
    //       update animals_per_row (same convention as rabbit_substep).
    (void)p; (void)gen; (void)src; (void)dst;
}
