#!/bin/bash
# Phase 2a (static check) and 2c (stall ladders), file source and HF+.
# Usage: phase2.sh [static|ladder|hfstatic|hfladder]...
set -u
REPO=/home/kenji/src/airspy-fmradion
R=$REPO/work-linux/tools/run_pa.sh
PIANO=file:$REPO/test-files/piano_iqtest.wav
INTERFM=file:$REPO/test-files/interfm-20260724102822z-iq.wav
HF=hf:82500000
LADDER="2,4,6,8,10,12,14,16,18,20,25,30,40,50,75,100"

run() { env "$@" "$R" | tail -1; }

for part in "$@"; do
  case $part in
  static)
    for L in 8 15 21 40 43 60 100; do
      run LABEL=2a-file-L$L SRC=$PIANO LAT=$L DUR=40
    done
    for L in 15 40 60 100; do
      run LABEL=2a-file-L$L-pre30 SRC=$PIANO LAT=$L PREFILL=30 DUR=40
    done
    ;;
  hfstatic)
    for L in 40 43 60 100; do
      run LABEL=2a-hf-L$L SRC=$HF LAT=$L DUR=60
    done
    for L in 40 60; do
      run LABEL=2a-hf-L$L-pre30 SRC=$HF LAT=$L PREFILL=30 DUR=60
    done
    ;;
  ladder)
    for mode in thread process; do
      run LABEL=2c-file-L40-$mode SRC=$INTERFM LAT=40 DUR=130 \
        STALL="-m $mode -w 5000 -g 3000 -d $LADDER"
      run LABEL=2c-file-L60-$mode SRC=$INTERFM LAT=60 DUR=130 \
        STALL="-m $mode -w 5000 -g 3000 -d $LADDER"
      run LABEL=2c-file-L40-pre30-$mode SRC=$INTERFM LAT=40 PREFILL=30 DUR=130 \
        STALL="-m $mode -w 5000 -g 3000 -d $LADDER"
    done
    ;;
  hfladder)
    for mode in thread process; do
      run LABEL=2c-hf-L40-$mode SRC=$HF LAT=40 DUR=70 \
        STALL="-m $mode -w 5000 -g 3000 -d $LADDER"
      run LABEL=2c-hf-L60-$mode SRC=$HF LAT=60 DUR=70 \
        STALL="-m $mode -w 5000 -g 3000 -d $LADDER"
    done
    run LABEL=2c-hf-L40-pre30-process SRC=$HF LAT=40 PREFILL=30 DUR=70 \
      STALL="-m process -w 5000 -g 3000 -d $LADDER"
    ;;
  esac
done
