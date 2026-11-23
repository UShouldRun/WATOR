#include "rules.h"

#include <string.h>

// Clockwise from north: N, E, S, W.
static const int DR[4] = { -1, 0, 1, 0 };
static const int DC[4] = {  0, 1, 0, -1 };

enum { DIR_STAY = -1, DIR_DIE = -2 };

static inline int in_grid(const World *w, int r, int c) {
    return r >= 0 && r < w->R && c >= 0 && c < w->C;
}

static inline Cell get(const World *w, const Cell *g, int r, int c) {
    return g[(size_t)r * w->C + c];
}

static inline uint8_t age_inc(uint8_t a) { return a < 255 ? a + 1 : 255; }

// Where does the animal of type `sp` [species](RABBIT or FOX) at (r,c) go in generation `gen`?
// Returns 0..3 (index into DR/DC), DIR_STAY if it has nowhere to go, or DIR_DIE if it
// is a fox that starves.
//   rabbit: moves to an empty neighbour
//   fox:    moves to a rabbit neighbour if any; else starves if too hungry; else
//           moves to an empty neighbour
static int animal_dir(const World *w, const Cell *src, int r, int c, int gen, uint8_t sp) {
    int rab[4], emp[4], nr = 0, ne = 0; // rabbit, empty, rabbit size, empty size
    for (int d = 0; d < 4; d++) {
        int rr = r + DR[d], cc = c + DC[d];
        if (!in_grid(w, rr, cc)) continue;
        uint8_t t = get(w, src, rr, cc).type;
        if (t == EMPTY)                    emp[ne++] = d;
        else if (t == RABBIT && sp == FOX) rab[nr++] = d;
    }

    const int *pool = emp;
    int P = ne;
    if (sp == FOX) {
        if (nr > 0) {
            pool = rab;
            P = nr;
        } else if (get(w, src, r, c).food_age + 1 >= w->p.gen_food_foxes) {
            return DIR_DIE;     // starves after not finding a rabbit, before moving
        }
    }
    return P ? pool[(gen + r + c) % P] : DIR_STAY;
}

// New content of cell (r,c) after the sub-step of species `sp` (RABBIT or FOX).
// A cell looks at who could end up in it, using only src, so every worker can
// write only its own zone.
static Cell cell_update(const World *w, const Cell *src, int r, int c, int gen, uint8_t sp) {
    const int PG = sp == RABBIT ? w->p.gen_proc_rabbits : w->p.gen_proc_foxes;
    Cell cur = get(w, src, r, c);

    // The cell holds an animal of this species: it leaves, stays or dies.
    if (cur.type == sp) {
        int dir = animal_dir(w, src, r, c, gen, sp);
        if (dir == DIR_DIE)
            return (Cell){ EMPTY, 0, 0 };
        if (dir == DIR_STAY)         // stuck: stays and keeps ageing (a stuck fox is hungrier)
            return (Cell){ sp, age_inc(cur.proc_age), sp == FOX ? cur.food_age + 1 : 0 };
        if (cur.proc_age >= PG)      // moved away and procreated -> baby left behind
            return (Cell){ sp, 0, 0 };
        return (Cell){ EMPTY, 0, 0 };
    }

    // The cell can receive this species: empty, or (foxes) holding a rabbit to eat.
    if (cur.type == EMPTY || (sp == FOX && cur.type == RABBIT)) {
        // Neighbours of this species that chose to move here compete for the cell:
        // the oldest procreation age wins, ties go to the least hungry fox.
        int best_age = -1, best_food = 0;
        for (int d = 0; d < 4; d++) {
            int qr = r + DR[d], qc = c + DC[d];
            if (!in_grid(w, qr, qc)) continue;
            Cell q = get(w, src, qr, qc);
            if (q.type != sp) continue;
            // q reaches (r,c) iff it moves in the opposite direction of d.
            if (animal_dir(w, src, qr, qc, gen, sp) != ((d + 2) & 3)) continue;

            // A fox that lands on a rabbit eats it; otherwise it is one generation hungrier.
            int food = sp == FOX ? (cur.type == RABBIT ? 0 : q.food_age + 1) : 0;
            if (q.proc_age > best_age || (q.proc_age == best_age && food < best_food)) {
                best_age = q.proc_age;
                best_food = food;
            }
        }
        if (best_age < 0)
            return cur;              // nobody arrives: unchanged
        return (Cell){ sp, best_age >= PG ? 0 : age_inc((uint8_t)best_age), (uint16_t)best_food };
    }

    return cur;                      // rocks, and the other species: untouched
}

// Sub-step of species `sp` for generation `gen`: reads src, writes only the rows
// [row_begin,row_end) of dst. Reads reach up to 2 rows into the neighbouring
// zones (a neighbour animal's choice depends on its own neighbours), so every
// zone must have at least 2 rows.
//
// animals_per_row[r] is kept equal to the number of animals (rabbits + foxes) in
// row r of the *result* (saturating at 255), and a row is copied as-is when rows
// r-1, r and r+1 of src (all inside the zone) had no animals.
static void substep(Process *p, int gen, uint8_t sp, const Cell *src, Cell *dst) {
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
                Cell out = cell_update(w, src, r, c, gen, sp);
                dst[(size_t)r * w->C + c] = out;
                if (out.type == RABBIT || out.type == FOX) count++;
            }
            p->animals_per_row[r] = count > 255 ? 255 : (uint8_t)count;
        }
        prev_src = cur_src;
    }
}

void rabbit_substep(Process *p, int gen, const Cell *src, Cell *dst) {
    substep(p, gen, RABBIT, src, dst);
}

void fox_substep(Process *p, int gen, const Cell *src, Cell *dst) {
    substep(p, gen, FOX, src, dst);
}
