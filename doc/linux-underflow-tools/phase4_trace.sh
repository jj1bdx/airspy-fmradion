#!/bin/bash
# Find what delays the decode thread's wake-up on an idle system: run the
# HF+ at -L 40 and record sched_wakeup/sched_switch for 120 s.
set -u
REPO=/home/kenji/src/airspy-fmradion
T=$REPO/work-linux/tools
env LABEL=4-hf-idle-schedtrace SRC=hf:82500000 LAT=40 DUR=150 $T/run_pa.sh > /dev/null &
RUNPID=$!
sleep 10
RUN=$(ls -dt $REPO/work-linux/runs/*-4-hf-idle-schedtrace | head -1)
TID=$(awk -F= '/^pid=/{print $2}' "$RUN/env.txt")
sudo trace-cmd record -q -e sched:sched_switch -e sched:sched_wakeup \
  -o "$RUN/sched.dat" sleep 120 > /dev/null
sudo chown "$(id -u):$(id -g)" "$RUN/sched.dat"
wait $RUNPID
trace-cmd report -i "$RUN/sched.dat" > "$RUN/sched_report.txt" 2>/dev/null
python3 $T/wake_latency.py "$RUN/sched_report.txt" "$TID" > "$RUN/wake_latency.txt"
echo "$RUN"
