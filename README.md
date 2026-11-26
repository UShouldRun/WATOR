# WATOR - Parallel Ecosystem Simulation

A parallel simulation of a predator-prey ecosystem,
originally created by Alexander K. Dewdney for
Scientific American (December 1984).

## Overview

Simulate an ecosystem of **rabbits** (prey) and **foxes**
(predators) on a grid world. Foxes hunt rabbits, rabbits
reproduce, and the population dynamics evolve over
generations according to deterministic rules.

The simulation runs in parallel across multiple worker
threads, each responsible for a zone of the world grid.

## The Name: Wa-Tor

**Wa-Tor** is the name of the planet the animals live on.
It comes from **Water Torus**: the world is a torus
(donut-shaped surface), so an animal walking off the top
edge reappears at the bottom, and off the left edge
reappears at the right.

The simulation was devised by A. K. Dewdney and
published in Scientific American (December 1984) in
his "Computer Recreations" column, under the title
"Sharks and fish wage an ecological war on the toroidal
planet Wa-Tor". Our version replaces the original
sharks and fish with foxes and rabbits.

## Rules

### Rabbits
- Move to empty adjacent cells (north, east, south, west)
- Procreate every `GEN_PROC_RABBITS` generations
- When procreating, stay in place and age resets

### Foxes
- Try to eat a rabbit in an adjacent cell
- If no rabbits, try to move to an empty cell
- Starve and die after `GEN_FOOD_FOXES` generations without eating
- Procreate every `GEN_PROC_FOXES` generations

### Rocks
- Static obstacles that no animal can occupy

### Movement Selection
Animals number their admissible neighbours `0 .. P-1` clockwise from north
and pick index `(generation + row + col) mod P`.
This ensures deterministic, non-conflicting movement across the grid.

## Building

```
make          # build libwator.a, ecosystem and the tests
make test     # run the test program
make tsan     # rebuild with -fsanitize=thread and run the tests
```

## Usage

```
./ecosystem [n_threads] < input_file > output_file
```

- `n_threads`: number of parallel workers (default: 1 = sequential)
- `input_file`: the initial ecosystem state
- `output_file`: the final ecosystem state

### Example

```
./ecosystem 4 < examples/input5x5 > examples/output5x5
```

### Input Format
```
GEN_PROC_RABBITS GEN_PROC_FOXES GEN_FOOD_FOXES N_GEN R C N
ROCK X Y
RABBIT X Y
FOX X Y
...
```

### Output Format
Same format as input, showing final state.

## Parallel Implementation

The simulation divides the world into horizontal zones, each owned by a worker thread:

- **Zone Assignment**: Each worker owns rows `[row_begin, row_end)` of the grid
- **Communication**: Workers synchronize boundary rows with neighbors
- **Load Balancing**: every `rebalance_every` generations there is
  a barrier hook to redistribute zones by animal density
  (counting is in place; the redistribution is a TODO)

The implementation follows a two-phase generation:
1. **Rabbit sub-step**: All rabbits move and reproduce
2. **Fox sub-step**: Foxes hunt, move, and reproduce

This ordering ensures foxes see the updated rabbit positions after each generation.

## Test Files

The `examples/` directory includes:
- Small worlds (`5x5`, `10x10`, `20x20`)
- Large worlds (`100x100`, `200x200`)
- Balanced and unbalanced test cases

## Performance

The parallel implementation scales with thread count. 
For large worlds, use more threads than cores to overlap 
computation with synchronization overhead.

# Authors

- António Lanção
- David Sá
- Henrique Teixeira
