#!/bin/sh
# Difficulty curve: the AI-driven player on every sphere level, a few seeds
# each, 3 minutes per run. Prints the SUMMARY lines of sim_bench.
#   core/tools/bench_levels.sh [bench binary] [seconds] [seeds...]
BIN=${1:-build/core/test/devoursphere_sim_bench}
SEC=${2:-180}
shift 2 2>/dev/null
SEEDS=${*:-"7 11 23"}
for lv in 1 2 3 4 5 6 7 8 9 10; do
  for seed in $SEEDS; do
    "$BIN" "$lv" "$SEC" "$seed" -q | grep SUMMARY
  done
done
