#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "wator.h"

// Usage: ecosystem [n_threads] < input
// n_threads <= 1 (the default) runs the simulation sequentially.
int main(int argc, char **argv) {
  i32 n_threads = argc > 1 ? atoi(argv[1]) : 1;
  if (n_threads < 1) {
    fprintf(stderr, "usage: %s [n_threads] < input\n", argv[0]);
    return 1;
  }

  World* w = wator_world_create();
  if (!w) {
    fprintf(stderr, "failed to allocate world\n");
    return 1;
  }

  if (!wator_world_load(w, stdin)) {
    fprintf(stderr, "failed to load world\n");
    wator_world_destroy(w);
    return 1;
  }

  struct timespec t_start, t_end;
  clock_gettime(CLOCK_MONOTONIC, &t_start);

  if (!wator_world_run(w, n_threads)) {
    fprintf(stderr, "failed to run simulation\n");
    wator_world_destroy(w);
    return 1;
  }

  clock_gettime(CLOCK_MONOTONIC, &t_end);
  const double elapsed_ms = (t_end.tv_sec - t_start.tv_sec) * 1e3 +
                            (t_end.tv_nsec - t_start.tv_nsec) / 1e6;
  fprintf(stderr, "simulation time: %.3f ms\n", elapsed_ms);

  wator_world_print(w, stdout);
  wator_world_destroy(w);

  return 0;
}
