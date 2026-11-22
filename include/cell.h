#ifndef WATOR_CELL_H
#define WATOR_CELL_H

#include <stdint.h>

enum CellType { EMPTY = 0, ROCK = 1, RABBIT = 2, FOX = 3 };

typedef struct {
    uint8_t  type;      // EMPTY, ROCK, RABBIT, FOX
    uint8_t  proc_age;  // generations since last reproduction
    uint16_t food_age;  // generations since last meal (foxes only)
} Cell;
_Static_assert(sizeof(Cell) == 4, "Cell layout changed");

typedef struct { uint8_t proc_age; } Rabbit;
typedef struct { uint8_t proc_age; uint16_t food_age; } Fox;

static inline Rabbit rabbit_from_cell(Cell c)   { return (Rabbit){ c.proc_age }; }
static inline Fox    fox_from_cell(Cell c)      { return (Fox){ c.proc_age, c.food_age }; }
static inline Cell   cell_from_rabbit(Rabbit r) { return (Cell){ RABBIT, r.proc_age, 0 }; }
static inline Cell   cell_from_fox(Fox f)       { return (Cell){ FOX, f.proc_age, f.food_age }; }

#endif
