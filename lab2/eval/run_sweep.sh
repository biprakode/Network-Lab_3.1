#!/usr/bin/env bash
# Per-combo isolation: each trial runs in its own process with a hard timeout so
# a stalled transfer cannot block the whole sweep. Run from the lab2/ directory.
set -u
CSV=eval/EVAL_RESULTS.csv
LOG=eval/run_sweep.log
TRIALS=${1:-3}
PER_TRIAL_TIMEOUT=${2:-10}

rm -f "$CSV"
: > "$LOG"

# Seed the CSV (header + one throwaway P=0 row), then drop the throwaway row.
timeout 10 ./eval/eval_main one 0 0.0 0 >>"$LOG" 2>&1
grep -vE '^[A-Za-z]+,[0-9]+,0\.0,0,' "$CSV" > "$CSV.tmp" && mv "$CSV.tmp" "$CSV"

for m in 0 1 2; do
  for p in 0.0 0.1 0.2 0.3 0.4 0.5; do
    for t in $(seq 1 "$TRIALS"); do
      if ! timeout "$PER_TRIAL_TIMEOUT" ./eval/eval_main one "$m" "$p" "$t" >>"$LOG" 2>&1; then
        echo "TIMEOUT/FAIL m=$m p=$p t=$t" | tee -a "$LOG"
      fi
    done
  done
done

echo "SWEEP_DONE rows=$(wc -l < "$CSV")"
