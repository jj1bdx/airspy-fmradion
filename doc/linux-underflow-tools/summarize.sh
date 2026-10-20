#!/bin/bash
# One summary line per finished run: ALSA sizes, trace stats, capture events.
# Usage: summarize.sh RUNDIR...
REPO=/home/kenji/src/airspy-fmradion
T=$REPO/work-linux/tools
printf 'run\tperiod\tbuffer\tdur_s\tUF\tempty\tfill_min_ms\tfill_p0.1_ms\tdelay_max_ms\tpushiv_max_ms\tq_max\tcap_events\tcap_ms\tzero\trepeat\tsdrdrop\n'
for d in "$@"; do
  d=${d%/}
  [ -f "$d/trace.csv" ] || continue
  grep -q '^# end' "$d/trace.csv" || continue
  per=$(awk '/^period_size/{print $2}' "$d"/asound_params_0.txt 2>/dev/null)
  buf=$(awk '/^buffer_size/{print $2}' "$d"/asound_params_0.txt 2>/dev/null)
  python3 $T/trace_stats.py "$d" > "$d/trace_stats.txt" 2>&1
  st=$(python3 - "$d/trace_summary.json" <<'EOF'
import json, sys
s = json.load(open(sys.argv[1]))
f = s["fill_before_ms"]
print("\t".join(str(x) for x in [round(s["duration_s"]), s["underflow_flags"],
      s["ring_empty_before_write"], f.get("0"), f.get("0.1"), s["sched_delay_ms"]["max"],
      s["push_interval_ms"]["max"], s["queue_len_max"]]))
EOF
)
  if grep -q 'filesource' "$d/env.txt"; then
    ev=$(awk -F'\t' '$1=="ins"{n++; ms+=$5; if($6>0)z++; else r++} END{printf "%d\t%.2f\t%d\t%d", n, ms, z, r}' "$d/gaps.tsv")
  else
    ev=$(awk -F'\t' '$1=="zero"{n++; ms+=$4; z++} $1=="rep"{n++; ms+=$4; r++} END{printf "%d\t%.2f\t%d\t%d", n, ms, z, r}' "$d/gaps.tsv")
  fi
  drop=$(cut -f2- "$d/stderr.tsv" | grep -o 'SDR dropped samples=[0-9]*' | tail -1 | cut -d= -f2)
  printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$(basename "$d")" "$per" "$buf" "$st" "$ev" "$drop"
done
