#ifndef WATOR_PROCESS_H
#define WATOR_PROCESS_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "world.h"

typedef struct Process Process;

// A worker: owns rows [row_begin, row_end) and is the only one writing there.
struct Process {
    int       id;
    pthread_t thread;
    World    *world;

    // Zone owned (1D strips). For 2D blocks add col_begin/col_end.
    int row_begin, row_end;

    // Number of sub-steps completed: 2*gen + (0 = start, 1 = rabbits done).
    // See sync.c for the window rule neighbours must respect.
    _Atomic int progress;

    // Neighbouring workers (NULL when none). 1D: up/down. 2D: add left/right.
    Process *up, *down;

    // Animals per row inside the zone; rows with 0 are skipped.
    uint8_t *animals_per_row;   // size = world->R
};

// Allocates per-process data, assigns zones and links neighbours.
void process_init_all(Process *procs, World *w, int T);
void process_destroy_all(Process *procs, int T);

// (Re)assigns contiguous row strips; also the hook for 2D blocks later.
void process_assign_zones(Process *procs, int R, int T);

// Thread body: runs all sub-steps of the simulation.
void *process_worker_main(void *arg);

// Spawns T threads and joins them. Returns 0 on success.
int process_run_all(Process *procs, int T);

#endif
