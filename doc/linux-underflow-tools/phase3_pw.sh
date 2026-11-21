#!/bin/bash
# Phase 2f rerun: HF+ to PipeWire, captured from the sink monitor.
set -u
REPO=/home/kenji/src/airspy-fmradion
R=$REPO/work-linux/tools/run_pa.sh
HF=hf:82500000
L1="stress-ng --cpu 4 --cpu-method all"
run() { env "$@" "$R" | tail -1; }
run LABEL=2f-hf-pw-idle-mon SRC=$HF OUT=pipewire DUR=600
run LABEL=2f-hf-pw-L1-mon SRC=$HF OUT=pipewire DUR=600 LOAD="$L1"
