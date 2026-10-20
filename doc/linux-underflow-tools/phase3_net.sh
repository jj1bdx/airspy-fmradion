#!/bin/bash
REPO=/home/kenji/src/airspy-fmradion
env LABEL=2d-hf-L40-L5net-rerun SRC=hf:82500000 LAT=40 DUR=600 \
  LOAD="iperf3 -s -1 >/dev/null & sleep 1; iperf3 -c 127.0.0.1 -t 86400 -P 4 >/dev/null" \
  $REPO/work-linux/tools/run_pa.sh | tail -1
