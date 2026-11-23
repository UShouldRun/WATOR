"""
@file bisect_gen.py
@brief Find the first generation where ecosystem diverges from the
       official per-generation snapshots (examples/allgen20x20).

* Usage:
  python3 tools/bisect_gen.py
* When:
  After a change to the rules, to check that the simulation still matches
  the reference at every generation, or to pinpoint the first generation
  where a mismatch appears.
* How:
  The snapshots (ground truth) start at generation 0. The simulation is
  deterministic, so the state of generation k depends only on the first k
  generations: "diverges at generation k" is therefore monotone in k, and
  a binary search over k suffices. Each probe rewrites the input file
  with N_GEN = k, runs ./ecosystem 1 and compares the positions of
  RABBIT / FOX / ROCK with the snapshot panel.
* Cost:
  O(log G) simulation runs, each O(k * R * C) for k generations.
"""

import subprocess

# ====================# CONFIG #====================

SNAP = "examples/allgen20x20"  # per-generation snapshots (ground truth)
INP  = "examples/input20x20"   # input file of the matching simulation
BIN  = "./ecosystem"           # binary under test


# ====================# SNAPSHOT PARSING #====================

def panel(line):
  """World panel of a snapshot line: the text between the first two '|'.

  A snapshot line looks like: |xxxxx|   |xxxxx| |xxxxx|
  (world, ages, ages + species); the first panel is the world.
  """
  a = line.index("|")
  b = line.index("|", a + 1)

  return line[a + 1 : b]


def positions(rows):
  """Position sets of rabbits, foxes and rocks of one snapshot panel."""
  pos = {"R": set(), "F": set(), "*": set()}
  for r, row in enumerate(rows):
    for c, ch in enumerate(panel(row)):
      if ch in "RF*":
        pos[ch].add((r, c))

  return pos


def parse_snap(path):
  """All snapshot panels of path, keyed by generation number."""
  gens = {}
  cur = None
  rows = None

  def flush():
    nonlocal rows
    if rows is not None:
      gens[cur] = positions(rows)
      rows = None

  with open(path) as f:
    for line in f:
      line = line.rstrip("\n")

      if line.startswith("Generation "):
        flush()
        cur = int(line.split()[1])

      elif line.startswith("|"):
        rows = rows or []
        rows.append(line)

  flush()

  return gens


# ====================# SIMULATION RUNS #====================

def run_at(k):
  """Position sets after k generations of the simulation, for the INP input."""
  with open(INP) as f:
    lines = f.readlines()

  head     = lines[0].split()
  head[3]  = str(k)  # N_GEN is the 4th parameter
  lines[0] = " ".join(head) + "\n"

  with open("/tmp/in_k.txt", "w") as f:
    f.writelines(lines)
  out = subprocess.run([BIN, "1"], stdin=open("/tmp/in_k.txt"),
                       capture_output=True, text=True).stdout

  pos = {"R": set(), "F": set(), "*": set()}
  for line in out.splitlines()[1:]:
    name, x, y = line.split()
    pos["*" if name == "ROCK" else name[0]].add((int(x), int(y)))

  return pos


def compare(snaps, k):
  """The only-ours / only-snapshot position sets of generation k, per species."""
  ours, ref = run_at(k), snaps[k]
  diff = {}
  for ch in "RF*":
    diff[ch] = (sorted(ours[ch] - ref[ch]), sorted(ref[ch] - ours[ch]))

  return diff


# ====================# MAIN #====================

def main():
  snaps = parse_snap(SNAP)
  n_gens = len(snaps)
  print(f"parsed {n_gens} generations, max = {n_gens - 1}")

  # Binary search for the first k whose positions differ from the snapshot.
  lo, hi = 0, n_gens - 1
  while lo < hi:
    mid = (lo + hi) // 2
    if not any(pair for _, pair in compare(snaps, mid).values()):
      lo = mid + 1
    else:
      hi = mid

  first = lo

  print("first diverging generation:", first)

  for k in (first - 1, first) if first > 0 else (0,):
    diff = compare(snaps, k)

    for ch in "RF*":
      only_ours, only_snap = diff[ch]
      print(f"  gen {k} {ch}: only-ours {only_ours}, only-snap {only_snap}")


if __name__ == "__main__":
  main()
