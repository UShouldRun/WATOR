/**
 * @file wator.h
 * @brief Wa-Tor Ecosystem Simulation (Public API)
 *
 * A population-dynamics simulation (predator-prey) on a 2-D grid, after
 * A. K. Dewdney's "Wa-Tor" planet ("Water Torus", Scientific American,
 * December 1984). Rabbits (prey) and foxes (predators) move and breed
 * each generation; foxes starve without prey; rocks are static.
 *
 * * World:
 * - What: one s_rows x s_cols grid and its simulation parameters.
 * - When: created once, loaded from an input file, run for s_gen
 *   generations, printed, destroyed.
 * - Time Complexity: O(s_gen * s_rows * s_cols) to run, O(s_rows * s_cols)
 *   to load or print.
 *
 * The world is opaque to the caller; all access goes through this API.
 * With s_threads <= 1 the simulation runs sequentially on the calling
 * thread, otherwise on s_threads pthreads (clamped so that every zone
 * keeps at least two rows).
 */
#ifndef __WATOR_H__
#define __WATOR_H__

#include <stdio.h>

#include "utils.h"

typedef struct world World;

World* wator_world_create    (void);
bool   wator_world_destroy   (World* w);
bool   wator_world_load      (World* w, FILE* in);
bool   wator_world_run       (World* w, const i32 s_threads);
void   wator_world_print     (const World* w, FILE* out);

#endif // !__WATOR_H__
