#!/bin/bash
# Phase 2e: forced drift. Positive -r drains the ring (the decoder makes
# fewer frames than the output consumes), giving underflows without a
# backlog.
set -u
REPO=/home/kenji/src/airspy-fmradion
R=$REPO/work-linux/tools/run_pa.sh
HF=hf:82500000
run() { env "$@" "$R" | tail -1; }
run LABEL=2e-file-L40-r100 SRC=file:$REPO/test-files/interfm-20260724102822z-iq.wav \
  LAT=40 EXTRA="-r 100" DUR=130
run LABEL=2e-hf-L40-r200 SRC=$HF LAT=40 EXTRA="-r 200" DUR=240
run LABEL=2e-hf-L43-r200 SRC=$HF LAT=43 EXTRA="-r 200" DUR=240
run LABEL=2e-hf-L40-pre30-r200 SRC=$HF LAT=40 PREFILL=30 EXTRA="-r 200" DUR=240
