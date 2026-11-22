#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "world.h"

static const char *EXAMPLE =
    "2 4 3 6 5 5 9\n"
    "ROCK 0 0\n"
    "RABBIT 0 2\n"
    "FOX 0 4\n"
    "FOX 1 0\n"
    "FOX 1 4\n"
    "ROCK 2 4\n"
    "RABBIT 3 0\n"
    "RABBIT 4 0\n"
    "FOX 4 4\n";

// Same objects, row-major order, N_GEN printed as 0.
static const char *EXPECTED =
    "2 4 3 0 5 5 9\n"
    "ROCK 0 0\n"
    "RABBIT 0 2\n"
    "FOX 0 4\n"
    "FOX 1 0\n"
    "FOX 1 4\n"
    "ROCK 2 4\n"
    "RABBIT 3 0\n"
    "RABBIT 4 0\n"
    "FOX 4 4\n";

static int load_str(World *w, const char *s) {
    FILE *f = fmemopen((void *)s, strlen(s), "r");
    assert(f);
    int rc = world_load(w, f);
    fclose(f);
    return rc;
}

static void test_roundtrip(void) {
    World w = {0};
    assert(load_str(&w, EXAMPLE) == 0);
    assert(w.R == 5 && w.C == 5);
    assert(w.p.gen_proc_rabbits == 2 && w.p.gen_proc_foxes == 4);
    assert(w.p.gen_food_foxes == 3 && w.p.n_gen == 6);
    assert(cell_at(w.buf[0], &w, 0, 2)->type == RABBIT);
    assert(cell_at(w.buf[0], &w, 2, 4)->type == ROCK);
    assert(cell_at(w.buf[0], &w, 4, 4)->type == FOX);

    char *buf = NULL;
    size_t len = 0;
    FILE *out = open_memstream(&buf, &len);
    assert(out);
    world_print(&w, 0, out);
    fclose(out);
    assert(strcmp(buf, EXPECTED) == 0);

    free(buf);
    world_free(&w);
}

static void test_invalid(void) {
    World w = {0};
    assert(load_str(&w, "") != 0);
    assert(load_str(&w, "1 1 1 1 2 2 1\nROCK 5 0\n") != 0);          // out of bounds
    assert(load_str(&w, "1 1 1 1 2 2 1\nTREE 0 0\n") != 0);          // unknown object
    assert(load_str(&w, "1 1 1 1 2 2 2\nROCK 0 0\nFOX 0 0\n") != 0); // same cell
    assert(load_str(&w, "1 1 1 1 2 2 3\nROCK 0 0\n") != 0);          // truncated
}

int main(void) {
    test_roundtrip();
    test_invalid();
    puts("world tests passed");
    return 0;
}
