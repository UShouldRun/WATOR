// The simulation's dependencies, as DAGs: how the data of one generation
// flows into the next, and how that maps onto the worker threads.
// Build: typst compile dag.typ   (generation.svg comes from generation.dot)

#set document(title: "Ecosystem simulation: data-flow DAG")
#set page(paper: "a4", margin: (x: 1.8cm, y: 1.6cm), numbering: "1")
#set text(font: "Liberation Sans", size: 10pt, lang: "en")
#set par(justify: true)
#set heading(numbering: "1.")
#show raw: set text(font: "DejaVu Sans Mono", size: 9pt)

#align(center)[
  #text(size: 17pt, weight: "bold")[Ecosystem simulation: how the data flows]
  #v(2pt)
  #text(size: 10pt, fill: rgb("#475569"))[Dependency DAGs of one generation, of one cell, and of the worker threads]
]

#v(6pt)

The world at generation $G$ is the only input of generation $G + 1$. In
between, two sub-steps run one after the other: every rabbit moves (as if
at the same time), then every fox moves on the world the rabbits left. The
graphs below say what each value is computed from; an arrow
$A -> B$ means "$B$ needs $A$".

= One generation

#figure(
  image("generation.svg", width: 94%),
  caption: [Data flow of one generation. Blue: the world between sub-steps. Grey dashed boxes: inputs (the parameters, the generation number $G$ and the cell's coordinates $X, Y$). Dashed arrows: values copied unchanged.],
) <generation>

#pagebreak()

What @generation shows:

- *Direction only needs types.* An animal's choice depends on the _types_ of
  its four neighbours (which are empty, which hold a rabbit), on $G$ and on
  its own $X, Y$. The ages are not needed to choose, only to settle conflicts
  and to procreate.
- *The fox sub-step waits for the whole rabbit sub-step.* Foxes look for
  rabbits in "World after the rabbits", never in the world at $G$; that is
  the one ordering the rules impose.
- *Conflicts need everyone's choice first.* A cell knows who arrives only
  once all neighbours have chosen, so "Claims" sits after "Direction" for all
  animals. This is why the animals can be treated as moving at the same time.
- *Starvation comes before moving.* A fox without an adjacent rabbit checks
  `food_age + 1 ≥ GEN_FOOD_FOXES` first; only a fox that survives tries an
  empty cell.
- *Procreation needs to know whether the animal moved.* A baby is left in the
  old cell only when the animal moved away; one that stays just gets older.

= One cell

The program computes each cell of the new world by _pulling_: the cell looks
at who could end up in it, reading only the old world. That gives every cell
the same small, fixed set of inputs:

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
    - *Distance 1* (N, E, S, W): the animals that could move into `c`.
    - *Distance 2*: _their_ neighbours, because whether a neighbour moves into
      `c` depends on what that neighbour sees around itself.

    So a cell of the new world depends on the 13 cells within Manhattan
    distance 2 of it in the old world, and on nothing else. Rows are 2 deep in
    each direction: that is why a worker reads up to two rows of its neighbour
    zones, and why every zone keeps at least two rows.
  ],
)

Since the new world is written to a separate grid (the double buffer: sub-step
$s$ reads `buf[s & 1]` and writes `buf[(s + 1) & 1]`), no cell's result can
disturb another cell's inputs within a sub-step. All cells of a sub-step are
independent of each other: that is the parallelism.

= Generations and workers

Chaining @generation gives the whole run:

#align(center, text(size: 9.5pt)[
  World $G = 0$ #sym.arrow rabbits #sym.arrow foxes #sym.arrow World 1 #sym.arrow rabbits #sym.arrow foxes #sym.arrow #sym.dots #sym.arrow World `N_GEN`
])

That chain is strictly sequential: $2 dot.c$ `N_GEN` sub-steps, each needing
the previous one. The parallelism is _inside_ each sub-step, across cells.
With $T$ workers each owning a horizontal strip of rows, a strip's sub-step
$s$ needs its own sub-step $s - 1$ and the boundary rows of the strips just
above and below at $s - 1$ (from the radius above):

#figure(
  image("workers.svg", width: 100%),
  caption: [Task DAG with 3 workers and 2 generations. $R(i, s)$ / $F(i, s)$: worker $i$ running sub-step $s$ on its rows. There is no global barrier: a worker only waits for its two neighbours (`__sync_wait`), so strips far apart can be a few sub-steps apart.],
) <workers>

The critical path of @workers is load #sym.arrow init #sym.arrow $2 dot.c$
`N_GEN` sub-steps #sym.arrow join #sym.arrow print, whatever $T$ is: more
workers make each sub-step shorter, not the chain.
