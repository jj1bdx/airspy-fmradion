#!/bin/bash
# PipeWire run with two captures: the sink monitor (run_pa.sh) and the
# loopback card's capture side, opened only after PipeWire's sink is
# running and with the sink's own hw_params, so aloop accepts both ends.
# aloop also requires the same interleaved/non-interleaved access on both
# ends; PipeWire uses MMAP_NONINTERLEAVED, hence arecord -I (one file per
# channel).
# Usage: pw_dualcap.sh LABEL DUR [LOAD]
set -u
REPO=/home/kenji/src/airspy-fmradion
T=$REPO/work-linux/tools
LABEL=$1
DUR=$2
LOAD=${3:-}
sudo prlimit --pid $$ --rtprio=30:30
env LABEL="$LABEL" SRC=hf:82500000 OUT=pipewire DUR="$DUR" LOAD="$LOAD" \
  "$T/run_pa.sh" > /dev/null &
RUNPID=$!
sleep 2
RUN=$(ls -dt "$REPO"/work-linux/runs/*-"$LABEL" | head -1)

# Wait for a RUNNING playback substream on card 0 and read its params.
SUB=
for _ in $(seq 1 300); do
  for s in /proc/asound/card0/pcm0p/sub*; do
    if grep -q 'state: RUNNING' "$s/status" 2>/dev/null; then
      SUB=$s
      break 2
    fi
  done
  sleep 0.1
done
if [ -z "$SUB" ]; then
  echo "no running sink substream" > "$RUN/aloopcap.log"
  wait $RUNPID
  exit 1
fi
N=${SUB##*sub}
cat "$SUB/hw_params" > "$RUN/aloopcap_sink_hw_params.txt"
FMT=$(awk '/^format:/{print $2}' "$SUB/hw_params")
PER=$(awk '/^period_size:/{print $2}' "$SUB/hw_params")
BUF=$(awk '/^buffer_size:/{print $2}' "$SUB/hw_params")
chrt -f 20 arecord -q -I -D "hw:0,1,$N" -f "$FMT" -c 2 -r 48000 \
  --period-size="$PER" --buffer-size="$BUF" -t raw \
  "$RUN/aloopcap_L.raw" "$RUN/aloopcap_R.raw" 2> "$RUN/aloopcap.log" &
CAPPID=$!
echo "sub=$N format=$FMT period=$PER buffer=$BUF" >> "$RUN/aloopcap.log"
wait $RUNPID
kill -INT $CAPPID 2>/dev/null
wait $CAPPID
# Interleave the two channels. Zero runs are format-independent: an
# all-zero S32 sample has the same bits as a float 0.0, so gapscan's
# zero-run detector applies directly.
python3 "$T/interleave32.py" "$RUN/aloopcap_L.raw" "$RUN/aloopcap_R.raw" "$RUN/aloopcap.raw"
"$T/gapscan" "$RUN/aloopcap.raw" > "$RUN/aloopcap_gaps.tsv"
echo "$RUN"
