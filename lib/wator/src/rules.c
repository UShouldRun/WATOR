// TODO(refactor): replace the pull kernel with the bucket kernel (two passes
// over the quad trees and the claims); details below the include.

#include "wator_private.h"

// TODO(refactor): this file is still the pull kernel on the row-major Cell
// layout, which no longer exists (Cell, CELL_EMPTY, World, w->p, buf).
// Replace it with the bucket kernel of the report:
//  - pass 1 (choose): walk the occupancy tree over the zone, compute each
//    animal's direction from src (grid via __wator_world_grid_get, ages from
//    src->proc_age / food_age at the same index), fetch_or the claim nibble
//    in wator->claims, fetch_add the claims tree on the cell's first claim.
//  - pass 2 (solve): walk the claims tree, resolve each claimed cell
//    (oldest-after-the-move, ties to the least hungry fox), write dst grid
//    and ages, apply departures / procreation, update the occupancy tree.
//  - cells that nothing touched must still be carried to dst (rocks, the
//    other species, animals that stay): copy src -> dst for the zone, or
//    write every stayer explicitly in pass 1/2.
//  - transposition: when wator->transposed, number neighbours in input
//    clockwise order = stored W, S, E, N (drow/dcol below are N, E, S, W).
//  - ages are u16 now: __rules_age_inc saturates at 255 (u8), widen it.
// Keep the pull kernel's logic available to the tests as the oracle.

#include <string.h>

// ====================# PRIVATE #====================

// Clockwise from north: N, E, S, W.
static const i32 drow[4] = {-1, 0, 1, 0};
static const i32 dcol[4] = {0, 1, 0, -1};

enum {
  DIR_STAY = -1,
  DIR_DIE = -2,
};

static inline bool __rules_in_grid(const World *w, const i32 r, const i32 c) {
  return r >= 0 && r < w->s_rows && c >= 0 && c < w->s_cols;
}

static inline Cell __rules_get(const World *w, const Cell *g, const i32 r,
                               const i32 c) {
  return g[(i64)r * w->s_cols + c];
}

static inline u8 __rules_age_inc(const u8 age) {
  return age < 255 ? age + 1 : 255;
}

// Where does the animal of species `species` (WATOR_RABBIT or WATOR_FOX) at
// (row, col) go in generation `gen`? Returns 0..3 (index into drow/dcol),
// DIR_STAY when it has nowhere to go, or DIR_DIE for a fox that starves.
//   rabbit: moves to an empty neighbour
//   fox:    moves to a rabbit neighbour if any; else starves if too hungry;
//           else moves to an empty neighbour
static i32 __rules_animal_dir(const World *w, const Cell *src, const i32 row,
                              const i32 col, const i32 gen, const u8 species) {
  i32 rabbit[4], empty[4], n_rabbit = 0, n_empty = 0;

  for (i32 dir = 0; dir < 4; dir++) {
    i32 rrow = row + drow[dir], ccol = col + dcol[dir];

    if (!__rules_in_grid(w, rrow, ccol))
      continue;

    u8 t = __rules_get(w, src, rrow, ccol).type;
    if (t == WATOR_EMPTY)
      empty[n_empty++] = dir;
    else if (t == WATOR_RABBIT && species == WATOR_FOX)
      rabbit[n_rabbit++] = dir;
  }

  const i32 *pool = empty;
  i32 P = n_empty;
  if (species == WATOR_FOX) {
    if (n_rabbit > 0) {
      pool = rabbit;
      P = n_rabbit;

    } else if (__rules_get(w, src, row, col).food_age >=
               w->p.s_gen_food_foxes - 1) {
      return DIR_DIE; // starves after not finding a rabbit, before moving
    }
  }

  return P > 0 ? pool[(gen + row + col) % P] : DIR_STAY;
}

// New content of cell (row, col) after the sub-step of species `species`. A
// cell looks at who could end up in it, using only src, so every worker can
// write only its own zone.
static Cell __rules_cell_update(const World *w, const Cell *src, const i32 row,
                                const i32 col, const i32 gen,
                                const u8 species) {
  i32 PG =
      species == WATOR_RABBIT ? w->p.s_gen_proc_rabbits : w->p.s_gen_proc_foxes;

  Cell cur = __rules_get(w, src, row, col);

  // The cell holds an animal of this species: it leaves, stays or dies.
  if (cur.type == species) {
    i32 dir = __rules_animal_dir(w, src, row, col, gen, species);

    if (dir == DIR_DIE)
      return CELL_EMPTY;

    if (dir ==
        DIR_STAY) // stuck: stays and keeps ageing (a stuck fox is hungrier)
      return (Cell){.type = species,
                    .proc_age = __rules_age_inc(cur.proc_age),
                    .food_age = species == WATOR_FOX ? cur.food_age + 1 : 0};

    if (cur.proc_age >=
        PG) // moved away and procreated -> a baby is left behind
      return (Cell){species, 0, 0};

    return CELL_EMPTY;
  }

  // The cell can receive this species: it is empty, or (for foxes) holds a
  // rabbit to eat. Neighbours of this species that chose to move here
  // compete for the cell: the oldest procreation age after the move wins,
  // ties go to the least hungry fox.
  if (cur.type == WATOR_EMPTY ||
      (species == WATOR_FOX && cur.type == WATOR_RABBIT)) {
    i32 best_age = -1, best_food = 0;

    for (i32 dir = 0; dir < 4; dir++) {
      i32 qrow = row + drow[dir], qcol = col + dcol[dir];

      if (!__rules_in_grid(w, qrow, qcol))
        continue;

      Cell q = __rules_get(w, src, qrow, qcol);
      if (q.type != species)
        continue;

      // q reaches (row, col) iff it moves in the opposite direction of dir.
      if (__rules_animal_dir(w, src, qrow, qcol, gen, species) !=
          ((dir + 2) & 3))
        continue;

      // A fox that lands on a rabbit eats it;
      // otherwise it is one generation hungrier.
      i32 food = species == WATOR_FOX
                     ? (cur.type == WATOR_RABBIT ? 0 : q.food_age + 1)
                     : 0;

      // Compare the age the animal will have *after* moving. An animal that
      // is procreating resets to 0, so it competes as a newborn: the one it
      // leaves behind and the one that arrives are both age 0.
      i32 age = q.proc_age >= PG ? 0 : __rules_age_inc(q.proc_age);
      if (age > best_age || (age == best_age && food < best_food)) {
        best_age = age;
        best_food = food;
      }
    }

    if (best_age < 0)
      return cur; // nobody arrives: unchanged

    return (Cell){.type = species,
                  .proc_age = (u8)best_age,
                  .food_age = (u16)best_food};
  }

  return cur; // rocks, and the other species: untouched
}

// Sub-step of species `species` for generation `gen`: reads src, writes only
// the rows [row_begin, row_end) of dst. Reads reach up to 2 rows into the
// neighbouring zones (a neighbour animal's choice depends on its own
// neighbours), so every zone needs at least 2 rows.
//
// animals_per_row[r] is kept equal to the number of animals in row r of the
// result (saturating at 255); a row is copied as-is when rows r - 1, r and
// r + 1 of src (all inside the zone) had no animals.
static void __rules_substep(
  Process*    p, 
  const i32   gen, const u8 species,
  const Cell* src, Cell*    dst
) {
  const World *w = p->world;

  u8 prev_src = 0, // src count of row r - 1 (animals_per_row[r - 1] already
                   // overwritten)
     cur_src  = 0;

  for (i32 row = p->row_begin; row < p->row_end; prev_src = cur_src, row++) {
    u8 next_src = row + 1 < p->row_end ? p->animals_per_row[row + 1] : 0;

    cur_src = p->animals_per_row[row];

    i32 interior = row > p->row_begin && row < p->row_end - 1;
    if (interior && prev_src == 0 && cur_src == 0 && next_src == 0) {
      i64 s_row = (i64)row * w->s_cols;
      memcpy(&dst[s_row], &src[s_row], (i64)w->s_cols * sizeof(Cell));
      // animals_per_row[row] is already 0
      continue;
    }

    i32 n_animals = 0;

    for (i32 col = 0; col < w->s_cols; col++) {
      Cell out = __rules_cell_update(w, src, row, col, gen, species);
      dst[(i64)row * w->s_cols + col] = out;

      n_animals += out.type == WATOR_RABBIT || out.type == WATOR_FOX;
    }

    p->animals_per_row[row] = n_animals > 255 ? 255 : (u8)n_animals;
  }
}

void __wator_process_rabbit_substep(
  Process *p, 
  const i32 gen, const Cell *src,
  Cell *dst
) {
  __rules_substep(p, gen, WATOR_RABBIT, src, dst);
}

void __wator_process_fox_substep(
  Process *p,
  const i32 gen, const Cell *src,
  Cell *dst
) {
  __rules_substep(p, gen, WATOR_FOX, src, dst);
}
