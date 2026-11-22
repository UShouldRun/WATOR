#ifndef WATOR_PARAMS_H
#define WATOR_PARAMS_H

typedef struct {
    int gen_proc_rabbits;
    int gen_proc_foxes;
    int gen_food_foxes;
    int n_gen;
    int n_threads;
    int rebalance_every;  // X generations between load-balancing barriers
} Params;

#endif
