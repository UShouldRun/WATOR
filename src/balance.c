#include "balance.h"

long count_animals(const Process *p) {
    long total = 0;
    for (int r = p->row_begin; r < p->row_end; r++)
        total += p->animals_per_row[r];
    return total;
}

void rebalance(Process *procs, int T) {
    // TODO: barrier across all workers, compare balance_count_animals()
    //       per worker, and if the discrepancy is large shift the zone
    //       boundaries (row_begin/row_end), moving animals_per_row data and
    //       keeping the buffers consistent with the new ownership.
    (void)procs; (void)T;
}
