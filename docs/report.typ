// The project report: problem statement, the two implementations (pull
// kernel, bucket rewrite), and their data-flow DAGs. Build:
//   typst compile docs/report.typ
// dag.pdf (compiled from dag.typ) holds the generation-level DAG figure.

#import "template.typ": report, note-box, step-block

#show: report.with(
  title: "Ecosystem Simulation",
  subtitle: "Foxes and rabbits on a shared-memory Wa-Tor world",
  authors: (
    (name: "António Lanção",    id: "up202205416"),
    (name: "David Sá",          id: "up202303580"),
    (name: "Henrique Teixeira", id: "up202306640"),
  ),
  course: "Parallel Computing 2026/27",
  institution: "Faculty of Sciences · University of Porto",
  abstract: [
    We simulate a predator-prey ecosystem -- foxes, rabbits and rocks on an
    $R times C$ grid -- for `N_GEN` generations, with pthreads as the
    parallel model. Two implementations are presented. The first computes
    each new cell by _pulling_ the choices of its neighbours: correct and
    simple, but $O(R dot C)$ per sub-step whatever the density. The second,
    the _bucket_ rewrite, indexes the animals with a quad tree and splits
    each sub-step into two passes -- choose, then solve -- over a packed
    claim array, for a cost that follows the animal count. Both are the
    same program: with one worker the parallel code is the sequential run.
    The report states the rules and their conflict resolution, walks the
    data-flow DAGs of a generation and of a single cell, presents the
    parallel decomposition and its synchronisation, and closes with the
    evaluation plan.
  ],
)

#pagebreak()

= Introduction

Predator and prey keep each other in check: too many rabbits and the foxes
thrive; too many foxes and the rabbits crash, followed by the foxes. A
cellular simulation of that loop -- Wa-Tor, after A. K. Dewdney's
_"Water Torus"_ planet -- is a small system with exactly this dynamic, and
it is a good parallel-programming exercise for three reasons. The rules are
local (an animal sees only its four neighbours), so the work divides
naturally across a grid; the generations are a strictly sequential chain, so
the parallelism must be found *inside* a generation, not across them; and
the population drifts -- early generations are dense, long runs end sparse
-- so a fixed division of the world ages badly. Any implementation must
respect all three at once.

This report documents our solution for a shared-memory machine: a C11
library (`libwator`) behind a thin `ecosystem` program, pthreads for the
workers, and the two kernel designs we built, in the order we built them.

#pagebreak()

= Problem statement

The world is a matrix of $R$ rows and $C$ columns; a cell is free or holds
a rock, a rabbit or a fox. From an initial population the simulation
builds `N_GEN` generations, rabbits first and foxes second in each
generation. Three parameters rule the animals: `GEN_PROC_RABBITS` and
`GEN_PROC_FOXES`, the ages at which the species procreates, and
`GEN_FOOD_FOXES`, the age at which a fox starves.

*Movement.* Rabbits and foxes move one cell, horizontally or vertically,
never diagonally. A rabbit moves to an empty neighbour if it has one; a fox
prefers a neighbour holding a rabbit (which it eats), and falls back to an
empty cell. An animal with no admissible neighbour stays.

*Selection.* When several neighbours are admissible, the animal numbers
them $0 .. P-1$ clockwise from north and picks index
$(G + X + Y) mod P$, where $G$ is the current generation and $(X, Y)$ its
coordinates. The choice is deterministic and, by depending on the
position, spreads conflicts over the grid instead of concentrating them.

*Life events.* A fox that finds no adjacent rabbit first checks
`food_age + 1 >= GEN_FOOD_FOXES` and dies of starvation _before_ trying to
move; this ordering is the one subtlety of the rules. An animal old enough
to procreate that _moves_ leaves a newborn in its old cell, and both
ages reset to zero. Rocks never move and never host an animal.

*Conflicts.* All animals of a species move as if at the same time: several
may claim one cell. Only the claimant with the older procreation age
survives; the others die. For foxes, a tie on age goes to the least hungry
(age compared after the move, so a procreator competes as a newborn).
The order in which the program visits the animals must not matter -- the
conflict rule, not a visiting order, decides who lives.

*Input and output.* Both are the same format: a parameter line
(`GEN_PROC_RABBITS GEN_PROC_FOXES GEN_FOOD_FOXES N_GEN R C N`), then $N$
object lines (`ROCK|RABBIT|FOX X Y`). Input is read from stdin, the final
world is written to stdout in the same format.

#pagebreak()

= How the data flows

Every rule above has a data dependency, and making them explicit is what
both our designs rest on. An arrow $A -> B$ below means "$B$ needs $A$".

#figure(
  image("generation.svg", width: 96%),
  caption: [Data flow of one generation. Blue: the world between sub-steps.
    Grey dashed boxes: inputs (the parameters, the generation $G$ and the
    coordinates $X, Y$). Dashed arrows: values copied unchanged.],
) <generation>

@generation is the contract both implementations must satisfy. Its reading
drives everything that follows:

- *Direction needs only types.* Whether an animal moves, and where, depends
  on the _types_ of its four neighbours, on $G$ and on its own $(X, Y)$.
  Ages enter later: to settle conflicts and to procreate, never to choose.
- *The fox sub-step waits for the whole rabbit sub-step.* Foxes hunt in
  the world the rabbits left, never in the world at $G$. That is the only
  ordering the rules impose between the sub-steps.
- *Conflicts need everyone's choice first.* A cell knows who arrives only
  after all its neighbours have chosen, so "claims" sits after "direction"
  for every animal -- this is what makes the simultaneous-move semantics
  implementable.
- *Starvation precedes movement.* The starvation check is a gate before
  the move attempt, not a consequence of it.
- *Procreation needs the movement outcome.* A baby is left only by an
  animal that moved; one that stays just grows older.

== One cell

#grid(
  columns: (auto, 1fr),
  gutter: 14pt,
  align: horizon,
  {
    let r = 2
    let cell(dx, dy) = {
      let d = calc.abs(dx) + calc.abs(dy)
      let fill = if d == 0 { rgb("#4338ca") } else if d == 1 { rgb("#a5b4fc") } else if d == 2 { rgb("#e0e7ff") } else { white }
      let txt  = if d == 0 { text(fill: white, weight: "bold")[c] } else if d <= 2 { text(size: 8pt)[#d] } else { [] }
      box(width: 18pt, height: 18pt, fill: fill, stroke: 0.5pt + rgb("#94a3b8"), align(center + horizon, txt))
    }
    grid(
      columns: 5, gutter: 0pt,
      ..range(-r, r + 1).map(dy => range(-r, r + 1).map(dx => cell(dx, dy))).flatten()
    )
  },
  [
    *Distance 1* (N, E, S, W): the animals that could move into `c`.
    *Distance 2*: _their_ neighbours, because whether a neighbour moves
    into `c` depends on what that neighbour sees around itself.

    A cell of the new world depends on the 13 cells within Manhattan
    distance 2 of it in the old world, and on nothing else. Rows are two
    deep in each direction -- that is why a worker reads up to two rows of
    its neighbour zones, and why every zone keeps at least two rows.
  ],
)

The new world is written to a separate grid (the double buffer: sub-step
$s$ reads `buf[s & 1]` and writes `buf[(s + 1) & 1]`), so no result can
disturb another cell's inputs within a sub-step. All cells of a sub-step
are independent -- that is the parallelism. Chaining sub-steps is strictly
sequential: $2 dot.c$ `N_GEN` of them, each needing the previous one.

#pagebreak()

= Implementation 1: the pull kernel

The first version computes each new cell by _pulling_: every cell of `dst`
asks, for each of its four neighbours, "would _you_ move into me?" -- and
answers the question by re-running that neighbour's direction logic on
the neighbour's own four neighbours. Rocks and empty cells that receive
nobody are copied unchanged; an occupied cell writes its departure (empty,
a newborn, or itself if it stayed).

#figure(
  image("workers.svg", width: 100%),
  caption: [Task DAG with 3 workers and 2 generations. $R(i, s)$ / $F(i, s)$:
    worker $i$ running sub-step $s$ on its rows. No global barrier: a worker
    waits only on its two neighbours (`__sync_wait`), so distant strips can
    drift a few sub-steps apart.],
) <workers>

*Correct by construction.* A cell's answer uses only `src`, which nobody
writes during the sub-step, so every worker can compute its rows without
touching anyone else's -- there is no shared write at all in the kernel.
The cost is that the 13-cell window is re-derived per cell: each of the
four neighbour checks re-reads the neighbour's window, so a cell costs
roughly 20--50 reads, empty or not.

*Zones.* $T$ workers own horizontal strips of rows
(`[row_begin, row_end)`); a strip's sub-step $s$ needs its own $s-1$ and
the boundary rows of its neighbours at $s-1$ (the two-deep window again).
Each worker publishes its progress in an atomic counter; a worker waits
until both neighbours reached the sub-step it is about to run
(`__sync_wait` / `__sync_publish`). Neighbours therefore differ by at most
one sub-step, and there is no global barrier -- the schedule of @workers.
Zones are capped at $R\/2$ rows each so the two-row window always fits.

*Skipping empty regions.* Each worker keeps `animals_per_row`: the count
of animals in each of its rows, updated as it writes `dst`. A row whose
neighbourhood is all zero is `memcpy`-ed instead of computed, which makes
sparse generations cheap on the _write_ side. The read side still walks
the whole world.

#pagebreak()

= Implementation 2: the bucket rewrite

The pull kernel's cost follows the area of the world. The rewrite makes
it follow the animals instead, with three ideas: a quad tree to find the
animals, a packed claim array to record their choices, and a two-pass
sub-step (choose, then solve) that keeps the simultaneous-move semantics.
It is the same program with any worker count -- with $T = 1$ it _is_ the
sequential run; the pull kernel survives only as an independent oracle in
the tests.

== Data structures

The world is a struct of arrays; the ages live in their own arrays
because pass 1 scans cells without needing them. The cell data and the
claim data each get their own struct -- same block-major layout, same
size field, the packed word variants drop in as the array type:

```c
typedef struct world {  // World, opaque in wator.h
  WorldGrid       grid;     // cells, block-major
  WorldGridClaims claims;   // claim bits, block-major

  i32
    s_rows,    // grid rows
    s_cols;    // grid columns

  u16
    *proc_age,  // breeding age of the animal in the cell
    *food_age;  // starvation age of the animal in the cell
} World;
```

```c
typedef struct world_grid {
  u32 s_grid;  // 4x4 block count
  u8* blocks;  // one byte per cell: empty, rock, rabbit, fox
} WorldGrid;   // packed variant: u32* blocks, one word per block

typedef struct world_grid_claims {
  u32 s_grid;  // 4x4 block count
  u8* blocks;  // two cells per byte, one nibble each
} WorldGridClaims;  // packed variant: u64* blocks, one word per block
```

The quad trees are instantiated independently -- one for each pass,
same shape, separate lifetimes:

```c
struct quad_tree {
  u32 s_nodes;  // node count, root included
  u32* nodes;   // leaf: 4x4 type mask; internal: animal or claims
                // count (pass 1 or 2, depends on the usage)
};
```

Both grid structs store their array block-major (the layout of @layout):
block `(br, bc)` is the contiguous run
`blocks[(br * s_blocks_row + bc) * 16]`, its four rows consecutive; a
cell `(r, c)` lives at
`(r / 4 * s_blocks_row + c / 4) * 16 + (r % 4) * 4 + c % 4`. The chosen
types keep the grid at one byte per cell and the claims at two cells
per byte; the packed variants -- `u32*` grid, `u64*` claims, one word
per block -- are the considerations of @layout, not the choice. With
`u32*`, a cell read is a shift-mask on the block word and a cell write
a read-modify-write; with `u64*`, a whole block's claims are one atomic
RMW instead of eight. Whether they pay off is a Results question.

The two trees and the claims array are `memset`-cleared per sub-step;
the grid and the ages are double-buffered (`src` / `dst`) between
sub-steps.

== The claim array

Each cell keeps four claim bits -- one per neighbour direction that chose
to move into it (for the fox sub-step, a bit is also set when a fox
targets the rabbit it eats). Two adjacent cells share one byte, two
nibbles:

#grid(
  columns: (auto, 1fr),
  column-gutter: 14pt,
  align: horizon,
  table(
    columns: 8,
    inset: (x: 5pt, y: 4pt),
    align: center,
    table.header(
      table.cell(colspan: 4)[claims of cell $(r, 2b)$],
      table.cell(colspan: 4)[claims of cell $(r, 2b+1)$],
    ),
    ..("N", "E", "S", "W", "N", "E", "S", "W").map(t => box(
      width: 14pt,
      height: 14pt,
      fill: if t == "N" { rgb("#eef2f7") } else { white },
      stroke: 0.5pt + rgb("#94a3b8"),
      align(center + horizon, text(size: 8pt)[#t]),
    )).flatten(),
  ),
  [
    Byte $b$ of the claim array holds the claims of the cell pair
    $(r, 2b)$ and $(r, 2b+1)$: two nibbles, four direction bits each --
    an optimal layout, with every bit of every byte allocated.

    A claim byte is written by up to three workers: the east and west
    claimants of the home row, and the north and south claimants from the
    rows above and below -- which, near a zone boundary, live in the
    neighbouring worker's zone. Interior bytes of a zone are
    single-writer.
  ],
)

Bits are all a claim needs to carry. When a cell resolves its nibble, the
claimant's position follows from the direction and the cell's coordinate
(one add), and its ages are read from `src`, immutable between the
passes -- including the post-move rule (a procreator competes as a
newborn), derived from the `src` age and the `GEN_PROC` parameter.

== The quad trees

There are two trees, one per pass, and they share one node shape: a
flat array of `u32`, one word per node. The word's meaning is fixed by
the node's level, which the descent already knows: at the leaf level it
is the $4 times 4$ type mask (two bits per cell: empty, rock, rabbit,
fox); above the leaves it is the number of animals in the subtree. The
tree keeps the summary property for descent: a node with `value == 0`
has no animal below it, so pass 1 skips it without touching its
children.

- *The occupancy tree* (for pass 1): pass 1 descends from the root,
  skipping empty subtrees, and visits only leaves with animals -- it
  walks animals, not cells, $O(A)$ visited nodes under the
  $log_4 N$ descent.
- *The claims tree* (for pass 2): its leaf `value` is the number of
  _claimed_ cells in the block -- cells some animal chose to move into,
  whether or not another chose the same one. Pass 2 goes block by
  block, descending only into blocks with claims and resolving each
  claimed cell's direction bits in turn. Indexing claims rather than
  conflicts matters: a cell with a single claimant still needs pass 2
  (to eat, reset ages, or leave a newborn), and "conflict" is a
  property pass 2 discovers, not one it can be indexed by.

Both trees share the leaf maintenance: pass 2 decrements the leaf an
animal left and increments the leaf it arrived in -- it already touches
both as a mover; animals that stay or die only decrement. The claims
counts reset with one `memset` per sub-step; the array is a few hundred
bytes at $N = 40 000$, which is why no epoch or double-buffering scheme
is worth its code here.

A block's claims may come from up to four neighbouring blocks -- a cell
in a block's corner can be claimed from the block's north, east, south
and west neighbours -- so pass 2 may need to load a neighbouring block:
up to three, in the corner cases. The blocks are small and contiguous
in the leaf array, so the neighbour loads are nearby reads, not tree
walks.

#note-box(label: "Worker count")[
  The four in the tree is the block size (and the claim nibble width),
  not a constraint on the worker count. Zones are still horizontal row
  strips, so $T = 1, 2, 4, 8, 16$ all fit the tree unchanged: each worker
  descends only into the leaves overlapping its rows.
]

== Memory layout: blocks, not rows <layout>

The cell array and the claim array are stored block-major rather than
row-major. The world is cut into $4 times 4$ blocks; the array is a
contiguous sequence of blocks, ordered block-row by block-row; inside a
block, the four rows are consecutive -- a 16-byte run covering 16 cells.
A block is then one contiguous read, and its neighbours -- the blocks
sharing its edges, the ones a corner claim needs -- are the next reads
over in the same block-row or one block-row stride away, not strided
walks through a row-major matrix. The trees index this layout directly:
the leaf children of a node at `(block_row, block_col)` are four
consecutive leaves, and a leaf is addressed by `(r / 4, c / 4)` alone.

Two packing options sit on top of this layout, and the choice between
them is a consideration rather than a decision:

- *Cell array as `u32[]`*: one word per block -- two bits per cell, the
  $4 times 4$ type mask is the word. Reads a whole block at once, and
  the leaf `value` is the loaded word; the cost is that a single-cell
  read is a shift-mask and a single-cell write is a read-modify-write.
- *Claim array as `u64[]`*: one word per block -- one nibble per cell,
  all 16 cells' direction bits in a single word. The same $4 times 4$
  grain as the cell array, so the two layouts map one-to-one; a
  block's claims are one atomic RMW instead of eight.

The plain byte layouts -- one `u8` per cell, one byte per cell pair --
are the alternative: cheaper single-cell access, more bytes to sweep.
The tree makes the packed layouts cheap to use, because the leaves
already work at block grain; whether the packed forms pay off in the
benchmark is a Results question.

== The two passes

#step-block(1, "Pass 1 -- choose", [
  Each worker walks its rows of the occupancy tree, skipping empty
  leaves. For each animal, compute its direction (five reads of `src`,
  the starvation check before the fall-back for foxes) and record:
  - *stays* / *dies*: a departure status in the worker's own scratch --
    the animal does not touch the claim array;
  - *moves*: set its direction's bit in the destination's nibble with
    `atomic_fetch_or` on `claims[r][c >> 1]`, shifted by $4 (c mod 2)$
    -- and, in the same breath, set the destination leaf's bit in the
    claims tree (with its path up to the root) so pass 2 finds it. The
    OR is idempotent, so redundant sets from different claimants are
    harmless; only the RMW itself must be atomic, which it already is.

  Pass 1 therefore _builds_ the claims tree as a by-product: it is the
  one pass that knows every destination. The claim bytes and the claims
  tree are the only shared structures written. Atomicity covers the
  zone-boundary case; interior bytes are uncontended and the fetch-or
  is a single cached RMW.
])

#step-block(2, "Pass 2 -- solve", [
  Each worker walks its rows of the claims tree, visiting only blocks
  with claimed cells. For each non-zero nibble: read each claimant from
  `src` by coordinate, resolve oldest-after-the-move wins (ties to the
  least hungry fox), write `dst`, and apply departures and procreation
  at the source cells. Update the occupancy tree's leaf counters for
  every animal that moved, and clear a claims-tree leaf bit whenever
  conflict resolution kills its last claimant, so the counts stay
  honest.

  Pass 2 no longer scans the claim bytes: the claims tree indexes
  exactly the blocks that can have received an animal, so pass 2 is
  $O(A)$ visited work under the same $log_4 N$ descent. A claimed cell
  on a block edge may have claimants in a neighbouring block; the
  block-by-block order loads them from the leaf array, as above -- at
  most three neighbour blocks, in the corner cases.
])

#note-box(label: "Sequential is T = 1")[
  Nothing changes with one worker: the atomics are uncontended, the tree
  is walked whole, and pass 2 owns every row. The sequential run is this
  same algorithm, so one implementation serves every worker count, and
  the old pull kernel stays in the test suite as an independent oracle.
]

== Synchronisation

The two-pass form also halves the cross-worker window. A claim bit
written into my row comes from a neighbour of the cell -- distance 1, not
distance 2 -- so pass 2's read window is one row into the neighbouring
zone, where the pull kernel needed two (a neighbour's _choice_ depended
on its own neighbours). The `__sync_wait` / `__sync_publish` protocol is
reused with two publishes per sub-step:

#align(center, text(size: 9pt)[
  pass 1 #sym.arrow `progress_pass1++` #sym.arrow wait neighbours' `pass1` #sym.arrow pass 2 #sym.arrow `progress_substep++`
])

A worker's pass 2 reads claim bits its neighbours wrote in their pass 1,
so waiting for their `progress_pass1` is the old window rule one level
down. There is still no global barrier, and the "at least two rows per
zone" constraint becomes "at least one".

== Cost

Per sub-step, both passes are animal-indexed: pass 1 is
$O(A dot log_4 N)$, and pass 2 -- with the claims tree replacing the
claim-byte scan -- is $O(A dot log_4 N)$ too, worst case; the byte scan
that made it $O(R dot C\/8)$ is gone. Against the pull kernel's
$O(R dot C)$ with a heavy constant, the win grows with sparsity -- and
with `N_GEN`, because the foxes thin the rabbits out and long runs spend
most of their generations in the sparse regime. The price is the two bit
arrays and leaf counters (together under $N\/12$ bytes) and the claim
array ($R dot C\/8$ bytes).

#pagebreak()

= Results

#note-box(label: "TBD")[
  The benchmark has not been run yet; this section holds the evaluation
  plan until the numbers exist. It will compare the two kernels on the
  assignment inputs and on the unbalanced variants, for
  $T in {1, 2, 4, 8, 16}$ workers, reporting the time of the computation
  only (I/O excluded) with speedups against the sequential run and
  against one worker. Three measurements matter beyond the headline
  speedups: the dense-to-sparse drift over the 10 000 generation runs
  (where the animal-indexed cost should hold while the area-indexed one
  decays to `memcpy` overhead), the boundary between the atomic fetch-or
  and a scratch-exchange variant (each worker claiming into a private
  array, merged after the wait -- no atomic at all, at the price of $T$
  claim arrays), and the leaf size ($4 times 4$ against $8 times 8$).
  What stays open beyond that: the tree also knows where the animals are,
  so weighted occupied leaves are the natural input to a dynamic
  load-balancing pass, replacing the per-row counts the current
  `rebalance` hook uses.
]

#pagebreak()

= Conclusion

The rules of Wa-Tor are local, but their conflict semantics force care:
"everyone moves at the same time" only holds if conflicts are resolved by
age rather than by visiting order, and starvation gates movement rather
than following it. Implementation 1 buys correctness from structure --
every cell computed from the immutable old world, no shared writes -- at
an area-indexed cost. Implementation 2 keeps the exact same semantics
while making the cost follow the animals: a quad tree to find them, four
claim bits packed two-cells-per-byte to record their choices, and a
choose/solve split whose only cross-worker dependency is one boundary
row instead of two. The sequential run is the $T = 1$ case of the same
code, which is how we think a shared-memory program should be built: one
algorithm, any worker count, and the interesting work in the data layout.
