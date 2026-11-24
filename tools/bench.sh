#!/usr/bin/env bash
# The simulator prints its timing to stderr, so stdout stays clean and is
# diffed byte-for-byte against the reference.
#
# A CSV summary is written to $CSV (default: benchmark.csv) with one row per
# (case, worker count): case,threads,reps,avg_ms,min_ms,speedup,status,times_ms
# Set CSV= to disable it.
#
# Usage:
#   tools/bench.sh                    # every pair, 5 reps, threads 1 2 4 8 16
#   tools/bench.sh 20x20 100x100      # only the named cases (no "input" prefix)
#   REPS=3 tools/bench.sh             # fewer repetitions
#   THREADS="1 4" tools/bench.sh      # custom worker counts
#   BIN=./sim tools/bench.sh          # use another binary
#   CSV=results.csv tools/bench.sh    # custom CSV path
#
# Build first with `make` (or `bear -- make all`).

set -u

cd "$(dirname "$0")/.."

BIN=${BIN:-./ecosystem}
EX=${EX:-examples}
REPS=${REPS:-5}
THREADS=${THREADS:-"1 2 4 8 16"}
CSV=${CSV-benchmark.csv}

if [[ ! -x $BIN ]]; then
  echo "error: $BIN not found; run 'make' first" >&2
  exit 1
fi

tmp_out=$(mktemp)
tmp_err=$(mktemp)
trap 'rm -f "$tmp_out" "$tmp_err"' EXIT

# Cases to run: positional args, or every examples/input* file.
if (( $# > 0 )); then
  names=("$@")
else
  names=()
  for inp in "$EX"/input*; do
    [[ -f $inp ]] || continue
    names+=("${inp#"$EX"/input}")
  done
fi

if [[ -n $CSV ]]; then
  printf 'case,threads,reps,avg_ms,min_ms,speedup,status,times_ms\n' > "$CSV"
fi

printf '%-22s %7s %11s %11s %9s %s\n' \
  "case" "threads" "avg(ms)" "min(ms)" "speedup" "status"
printf '%-22s %7s %11s %11s %9s %s\n' \
  "----------------------" "-------" "-----------" "-----------" "---------" "------"

for name in "${names[@]}"; do
  inp="$EX/input$name"
  ref="$EX/output$name"

  if [[ ! -f $inp || ! -f $ref ]]; then
    printf '%-22s %7s %11s %11s %9s %s\n' \
      "$name" "-" "-" "-" "-" "SKIP (missing input/output)"
    continue
  fi

  base_ms=""
  for t in $THREADS; do
    sum=0
    min=""
    runs=""
    bad=0

    for ((r = 1; r <= REPS; r++)); do
      "$BIN" "$t" < "$inp" > "$tmp_out" 2> "$tmp_err"
      ms=$(sed -n 's/.*simulation time: \([0-9.][0-9.]*\) ms.*/\1/p' "$tmp_err")
      [[ -z $ms ]] && ms=0

      if ! diff -q "$tmp_out" "$ref" > /dev/null; then
        if (( bad == 0 )); then
          echo "  !! $name t=$t: first mismatch (rep $r):" >&2
          diff "$tmp_out" "$ref" | head -n 5 >&2
        fi
        bad=$((bad + 1))
      fi

      sum=$(awk -v a="$sum" -v b="$ms" 'BEGIN { printf "%.6f", a + b }')
      runs+="${runs:+ }$ms"
      if [[ -z $min ]] || awk -v a="$ms" -v b="$min" 'BEGIN { exit !(a < b) }'; then
        min=$ms
      fi
    done

    avg=$(awk -v s="$sum" -v n="$REPS" 'BEGIN { printf "%.3f", s / n }')

    # Speedup is relative to the first worker count (normally 1).
    [[ -z $base_ms ]] && base_ms=$avg
    speedup=$(awk -v a="$base_ms" -v b="$avg" \
      'BEGIN { printf "%.2f", (b > 0 ? a / b : 0) }')

    if (( bad == 0 )); then
      status="OK"
    else
      status="MISMATCH($bad)"
    fi

    printf '%-22s %7s %11s %11s %9s %s\n' \
      "$name" "$t" "$avg" "$min" "${speedup}x" "$status"

    if [[ -n $CSV ]]; then
      printf '%s,%s,%s,%s,%s,%s,%s,"%s"\n' \
        "$name" "$t" "$REPS" "$avg" "$min" "$speedup" "$status" "$runs" >> "$CSV"
    fi
  done
done

if [[ -n $CSV ]]; then
  echo "CSV written to $CSV" >&2
fi
