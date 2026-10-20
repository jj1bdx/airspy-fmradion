#!/bin/bash
# Phase 2d: HF+ under load, scheduling variants, and configurations.
# Usage: phase3.sh [loads|variants|pipewire]... ; MIN=minutes per run (10)
set -u
REPO=/home/kenji/src/airspy-fmradion
R=$REPO/work-linux/tools/run_pa.sh
HF=hf:82500000
MIN=${MIN:-10}
DUR=$((MIN * 60))
TMP=$REPO/work-linux/loadtmp
mkdir -p "$TMP"

L1="stress-ng --cpu 4 --cpu-method all"
L2="stress-ng --vm 2 --vm-bytes 75%"
L3="stress-ng --hdd 2 --hdd-bytes 1g --temp-path $TMP"
L4="cmake -S $REPO -B $REPO/work-linux/build-load >/dev/null; \
while :; do cmake --build $REPO/work-linux/build-load --target clean >/dev/null; \
cmake --build $REPO/work-linux/build-load -j4 >/dev/null; done"
L5="iperf3 -s -1 >/dev/null & sleep 1; iperf3 -c 127.0.0.1 -t 86400 -P 4 >/dev/null"
WORST=${WORST:-$L1}
WORSTNAME=${WORSTNAME:-L1}

run() { env "$@" "$R" | tail -1; }

for part in "$@"; do
  case $part in
  loads)
    run LABEL=2d-hf-L40-L1cpu SRC=$HF LAT=40 DUR=$DUR LOAD="$L1"
    run LABEL=2d-hf-L40-L4build SRC=$HF LAT=40 DUR=$DUR LOAD="$L4"
    run LABEL=2d-hf-L40-L2vm SRC=$HF LAT=40 DUR=$DUR LOAD="$L2"
    run LABEL=2d-hf-L40-L3io SRC=$HF LAT=40 DUR=$DUR LOAD="$L3"
    run LABEL=2d-hf-L40-L5net SRC=$HF LAT=40 DUR=$DUR LOAD="$L5"
    ;;
  variants)
    run LABEL=2d-hf-L40-$WORSTNAME-fifo SRC=$HF LAT=40 DUR=$DUR LOAD="$WORST" SCHED=fifo
    run LABEL=2d-hf-L40-$WORSTNAME-nice SRC=$HF LAT=40 DUR=$DUR LOAD="$WORST" SCHED=nice
    run LABEL=2d-hf-L40-pre30-$WORSTNAME SRC=$HF LAT=40 PREFILL=30 DUR=$DUR LOAD="$WORST"
    run LABEL=2d-hf-L60-$WORSTNAME SRC=$HF LAT=60 DUR=$DUR LOAD="$WORST"
    ;;
  pipewire)
    run LABEL=2f-hf-pw-idle SRC=$HF OUT=pipewire DUR=$DUR
    run LABEL=2f-hf-pw-$WORSTNAME SRC=$HF OUT=pipewire DUR=$DUR LOAD="$WORST"
    ;;
  esac
done
rm -rf "$TMP"
