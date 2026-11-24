// The bucket rewrite: the two-pass (choose, solve) sub-step that replaces the
// pull kernel, and how the quad tree, the packed claim bytes and the workers
// fit into it. Companion to dag.typ (the dependency view); this is the
// algorithm view. Build: typst compile bucket.typ

#import "template.typ": report, note-box, step-block

#show: report.with(
  title: "The bucket sub-step",
  subtitle: "A two-pass rewrite: quad tree, packed claims, one algorithm for any worker count",
  authors: (
    (name: "António Lanção",    id: "up202205416"),
    (name: "David Sá",          id: "up202303580"),
    (name: "Henrique Teixeira", id: "up202306640"),
  ),
  course: "Parallel Computing 2026/27",
  institution: "Faculty of Sciences · University of Porto",
  abstract: [
    The current kernel computes each cell of the new world by _pulling_: every
    cell re-derives the choices of its four neighbours, so its cost is
    proportional to the area of the world, not to the number of animals. This
    note specifies the replacement: a sub-step in two passes. Pass 1 walks a
    quad tree that indexes only occupied leaves, and each animal _pushes_ a
    claim into a packed claim array; pass 2 resolves the claims. The cost
    becomes $O(A dot log_4 N)$ in the animals $A$, the sequential run is the
    same code with one worker, and the only cross-worker dependency shrinks
    from two boundary rows to one.
  ],
)

= From pull to push

In the pull kernel (`__rules_cell_update`), every destination cell examines
its four neighbours and re-runs `__rules_animal_dir` for each of them, because
whether a neighbour moves in depends on what _that_ neighbour sees. A cell of
the new world needs the 13 cells within Manhattan distance 2 of it in the old
world (the "one cell" section of `dag.typ`), which is also why a worker reads
two rows into each neighbouring zone.

The push form drops the redundant work by keeping each animal's choice where
it is made. The direction of an animal depends only on the _types_ of its four
neighbours, on $G$ and on its own coordinates -- five reads of the old world,
the same inputs the DAG in `dag.typ` already gives "Direction". Once an animal
knows where it goes, it records a *claim* at the destination; the destination
resolves the claims it received. Direction is computed once per animal
instead of up to five times per cell.

#note-box(label: "Cost")[ 
  Pull: $O(R dot C)$ per sub-step with a constant of roughly 20 to 50 reads
  per cell, whatever the density -- empty rows are still scanned (or
  mem-copied). Push: $O(A dot log_4 N)$ to find and move the $A$ animals plus
  $O(R dot C\/8)$ to scan the claim bytes; on the sparse worlds that
  dominate long runs (the 10 000-generation inputs drift to a few hundred
  animals in 40 000 cells) the animals term wins.
]

= The claim array

Each cell keeps four claim bits: one per neighbour direction that chose to
move into it (N, E, S, W for the claimants; for the fox sub-step a bit is also
set when a fox targets the rabbit cell it eats). Two adjacent cells share one
byte, two nibbles:

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
      table.cell(colspan: 4, align: center)[claims of cell $(r, 2b+1)$],
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
    A bit is set in pass 1 by the claimant coming from that direction.

    A claim byte is written by up to three workers: the east and west
    claimants of the home row, and the north and south claimants from the
    rows above and below -- which, near a zone boundary, live in the
    neighbouring worker's zone. Interior bytes of a zone are
    single-writer.
  ],
)

Bits are all the claim needs to carry. When a cell resolves its nibble, the
claimant's position follows from the direction and its own coordinate (one
add), and its ages are read straight from `src`, which is immutable between
the passes -- including the post-move rule (a procreating animal competes as
age 0), derived from the `src` age and the `GEN_PROC` parameter.

= The quad tree

The tree indexes occupancy: a node is non-empty when any animal lives in its
square. It exists to make pass 1 walk animals, not cells.

- *Leaves* are $4 times 4$ cell blocks. A leaf stores its animal count.
- *Descent*: from the root, skip empty children; only leaves with animals are
  visited. $log_4 N$ levels, $O(A)$ visited nodes.
- *Update*: pass 2 decrements the leaf an animal left and increments the leaf
  it arrived in. Since pass 2 already touches every mover, keeping the tree
  valid costs nothing but the two counter updates -- no rebuild. Animals that
  stay or die only decrement.

#note-box(label: "Worker count")[ 
  The four in the tree is the block size (and the claim nibble width), not a
  constraint on the worker count. Zones are still horizontal row strips, so
  $T = 1, 2, 4, 8, 16$ all fit the tree unchanged: each worker descends only
  into the leaves overlapping its rows.
]

= The two passes

#step-block(1, "Pass 1 -- choose", [
  Each worker walks its rows of the tree, skipping empty leaves. For each
  animal, compute its direction (five reads of `src`, the starvation check
  before the fall-back for foxes) and record:
  - *stays* / *dies*: a departure status in the worker's own scratch -- the
    animal does not touch the claim array;
  - *moves*: set its direction's bit in the destination's nibble with
    `atomic_fetch_or` on `claims[r][c >> 1]`, shifted by $4 (c mod 2)$.

  The claim byte is the only shared structure written. Its atomicity covers
  the zone-boundary case; interior bytes are uncontended and the fetch-or is a
  single cached RMW.
])

#step-block(2, "Pass 2 -- solve", [
  Each worker scans the claim bytes of its rows (a row can be skipped when
  `animals_per_row` says the rows above, at and below are empty, as today).
  For each non-zero nibble: read each claimant from `src` by coordinate,
  resolve oldest-after-the-move wins, ties to the least hungry fox, and write
  `dst`. Departure status and procreation are applied at the source cells.
  Update the tree leaves for every animal that moved.

  The claim array itself is a free index: non-zero bytes are the only cells
  that can have received an animal, so pass 2 is $O(R dot C\/8 + A)$.
])

#note-box(label: "Sequential is T = 1")[ 
  Nothing above changes with one worker: the atomics are uncontended, the
  tree is walked whole, and pass 2 owns every row. The sequential run is this
  same algorithm, so one implementation serves both, and the old pull kernel
  remains only as an independent oracle in the tests.
]

= Synchronisation

Within a sub-step there is now one cross-worker dependency, and it is half of
the old one: a claim bit written into my row comes from a neighbour of the
cell, so the read window of pass 2 is one row into the neighbouring zone (the
pull kernel needed two, since a neighbour's choice depends on _its_
neighbours). The `progress` counter protocol of `__sync_wait` / `__sync_publish`
is reused, with two publishes per sub-step:

#align(center, text(size: 9pt)[
  pass 1 #sym.arrow `progress_pass1++` #sym.arrow wait neighbours' `pass1` #sym.arrow pass 2 #sym.arrow `progress_substep++`
])

A worker's pass 2 reads the claim bits of its boundary rows, which the
neighbouring workers wrote in their pass 1; waiting for their `progress_pass1`
is exactly the existing window rule, one level down. There is no global
barrier, and the "every zone needs at least two rows" constraint of the pull
kernel becomes "at least one".

= Complexity and what stays open

Per sub-step: pass 1 is $O(A dot log_4 N)$; pass 2 is
$O(R dot C\/8)$ in the worst case (dense world) and $O(A)$ in practice via
the claim bytes and row counters. Against the pull kernel's $O(R dot C)$
constant-heavy scans, the win grows with sparsity, and with `N_GEN`: the
foxes thin the rabbits out, and the runs spend most generations in the
sparse regime.

Open, in the order they should be decided:

+ *Leaf size.* $4 times 4$ matches the claim nibble; $8 times 8$ halves the
  tree depth. Decide on the benchmark, not on taste.
+ *Paired solve.* Solving the two nibbles of a byte together batches the
  reads of pass 2; it does not de-race pass 1 (the boundary bits are still
  written by the neighbours), so it is an optimisation on top of the
  atomics, not an alternative to them.
+ *Scratch exchange instead of atomics.* Each worker claims into a private
  array and pass 2 merges the two neighbours' boundary rows after the wait:
  no atomic at all, at the price of $T$ extra claim arrays. The benchmark
  should compare this against the atomic fetch-or.
+ *Load balancing.* The tree also knows where the animals are; walking
  occupied leaves by weight is the natural input to the `rebalance` hook,
  replacing `animals_per_row` counts with leaf counts. Deferred until the
  static-zone version is measured.
+ *SoA.* Splitting `type` from the ages shrinks pass 1's window to one byte
  per cell. Deferred with the quad tree discussion; the claim design does
  not depend on it.
