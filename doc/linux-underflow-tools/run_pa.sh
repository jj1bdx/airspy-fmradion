#!/bin/bash
# One airspy-fmradion -P test run to a loopback card, with capture,
# /proc/asound sampling, optional stall injection, load, and scheduling.
#
# Settings are environment variables:
#   LABEL    run label (required)
#   SRC      file:<path> or hf:<freq_hz>        (default file:piano_iqtest.wav)
#   OUT      alsa (direct hw:2,0) or pipewire  (default alsa)
#   LAT      -L value in ms, empty for default
#   PREFILL  --portaudio-prefill ms, empty for none
#   ADAPT    --portaudio-prefill-adapt ms, empty for none
#   EXTRA    extra decoder args, e.g. "-E 36" or "-r 100"
#   DUR      seconds before SIGINT (hf source; also a cap for file)
#   STALL    stall_inject args after -p, e.g. "-m thread -w 3000 -d 10"
#   SCHED    none | nice | fifo                 (default none)
#   LOAD     shell command run during the test (killed at the end)
#   BIN      binary (default work-linux/build-t7 if present, else build-f236638)
#   NOCAP=1  skip the arecord capture
set -u
REPO=/home/kenji/src/airspy-fmradion
T=$REPO/work-linux/tools
: "${LABEL:?LABEL required}"
SRC=${SRC:-file:$REPO/test-files/piano_iqtest.wav}
OUT=${OUT:-alsa}
LAT=${LAT:-}
PREFILL=${PREFILL:-}
ADAPT=${ADAPT:-}
EXTRA=${EXTRA:-}
DUR=${DUR:-130}
STALL=${STALL:-}
SCHED=${SCHED:-none}
LOAD=${LOAD:-}
NOCAP=${NOCAP:-0}
if [ -z "${BIN:-}" ]; then
  if [ -x $REPO/work-linux/build-t7/airspy-fmradion ]; then
    BIN=$REPO/work-linux/build-t7/airspy-fmradion
  else
    BIN=$REPO/work-linux/build-f236638/airspy-fmradion
  fi
fi

START_EPOCH=$(date +%s)
RUN=$REPO/work-linux/runs/$(date -u +%Y%m%d-%H%M%S)-$LABEL
mkdir -p "$RUN"

# Output device: PortAudio index by name, PCM directory and capture device.
case $OUT in
alsa)
  DEV=$($T/pa_devs 2>/dev/null | awk -F'\t' '$NF ~ /\(hw:2,0\)$/ {print $1+0}')
  PCMDIR=/proc/asound/card2/pcm0p
  CAPDEV=plughw:2,1,0
  ;;
pipewire)
  DEV=$($T/pa_devs 2>/dev/null | awk -F'\t' '$NF == "pipewire" {print $1+0}')
  PCMDIR=/proc/asound/card0/pcm0p   # PipeWire's own sink PCM
  # Record the sink monitor through PipeWire: opening the loopback capture
  # side constrains the sink's ALSA parameters and can stop it starting.
  CAPDEV=pwmon
  PWSINK=$(wpctl inspect @DEFAULT_AUDIO_SINK@ | awk -F'"' '/node.name/{print $2}')
  ;;
*)
  echo "bad OUT" >&2
  exit 2
  ;;
esac
[ -n "$DEV" ] || { echo "PortAudio device not found" >&2; exit 1; }

# Source arguments.
case $SRC in
file:*)
  SRCARGS=(-t filesource -c "filename=${SRC#file:}")
  ;;
hf:*)
  SRCARGS=(-t airspyhf -c "freq=${SRC#hf:}")
  ;;
esac
ARGS=(-m fm "${SRCARGS[@]}" -v -P "$DEV")
[ -n "$LAT" ] && ARGS+=(-L "$LAT")
[ -n "$PREFILL" ] && ARGS+=(--portaudio-prefill "$PREFILL")
[ -n "$ADAPT" ] && ARGS+=(--portaudio-prefill-adapt "$ADAPT")
# shellcheck disable=SC2206
[ -n "$EXTRA" ] && ARGS+=($EXTRA)

# Scheduling: raise this shell's limits so children may use them unprivileged.
WRAP=()
case $SCHED in
nice)
  sudo prlimit --pid $$ --nice=30:30
  WRAP=(nice -n -10)
  ;;
fifo)
  sudo prlimit --pid $$ --rtprio=30:30
  WRAP=(chrt -f 10)
  ;;
esac
sudo prlimit --pid $$ --rtprio=30:30 2>/dev/null || true

{
  echo "label=$LABEL"
  echo "date_utc=$(date -u +%FT%TZ)"
  echo "mono_ns_at_start=$(python3 -c 'import time;print(time.clock_gettime_ns(time.CLOCK_MONOTONIC))')"
  echo "bin=$BIN"
  echo "cmd=${WRAP[*]} $BIN ${ARGS[*]}"
  echo "out=$OUT dev=$DEV pcmdir=$PCMDIR capdev=$CAPDEV"
  echo "stall=$STALL"
  echo "sched=$SCHED"
  echo "load=$LOAD"
  echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor)"
  echo "throttled_before=$(vcgencmd get_throttled)"
  echo "temp_before=$(vcgencmd measure_temp)"
  echo "loadavg_before=$(cat /proc/loadavg)"
} > "$RUN/env.txt"

mkfifo "$RUN/stderr.fifo"
python3 $T/statuslog.py "$RUN/stderr.fifo" "$RUN/stderr.tsv" &
LOGPID=$!

export AFM_TRACE=$RUN/trace.csv
"${WRAP[@]}" "$BIN" "${ARGS[@]}" > "$RUN/stdout.txt" 2> "$RUN/stderr.fifo" &
PID=$!
echo "pid=$PID" >> "$RUN/env.txt"

python3 $T/asound_sample.py "$PCMDIR" "$RUN" 20 30 &
SAMPPID=$!

# Start the capture once the playback PCM is open.
CAPPID=
if [ "$NOCAP" != 1 ]; then
  if [ "$CAPDEV" = pwmon ]; then
    chrt -f 20 pw-record --target "$PWSINK" -P '{ stream.capture.sink = true }' \
      --format f32 --rate 48000 --channels 2 --raw - \
      > "$RUN/capture.f32" 2> "$RUN/arecord.log" &
  else
    for _ in $(seq 1 1000); do
      if grep -qv closed "$PCMDIR"/sub0/status 2>/dev/null; then
        break
      fi
      sleep 0.01
    done
    chrt -f 20 arecord -q -D "$CAPDEV" -f FLOAT_LE -c 2 -r 48000 -t raw \
      "$RUN/capture.f32" 2> "$RUN/arecord.log" &
  fi
  CAPPID=$!
fi

INJPID=
if [ -n "$STALL" ]; then
  # shellcheck disable=SC2086
  $T/stall_inject -p $PID $STALL -o "$RUN/stalls.tsv" 2> "$RUN/stall_inject.log" &
  INJPID=$!
fi

LOADPID=
if [ -n "$LOAD" ]; then
  setsid bash -c "$LOAD" > "$RUN/load.log" 2>&1 &
  LOADPID=$!
fi

# Watchdog: SIGINT after DUR seconds, SIGKILL 10 s later.
( sleep "$DUR"; kill -INT $PID 2>/dev/null; sleep 10; kill -KILL $PID 2>/dev/null ) &
WDPID=$!

wait $PID
echo "exit=$?" >> "$RUN/env.txt"
kill $WDPID 2>/dev/null
[ -n "$LOADPID" ] && kill -TERM -- -$LOADPID 2>/dev/null
[ -n "$INJPID" ] && { kill -INT $INJPID 2>/dev/null; wait $INJPID; }
sleep 0.5
[ -n "$CAPPID" ] && { kill -INT $CAPPID 2>/dev/null; wait $CAPPID; }
wait $SAMPPID
wait $LOGPID
{
  echo "throttled_after=$(vcgencmd get_throttled)"
  echo "temp_after=$(vcgencmd measure_temp)"
  echo "loadavg_after=$(cat /proc/loadavg)"
} >> "$RUN/env.txt"
rm -f "$RUN/stderr.fifo"
if [ "$OUT" = pipewire ]; then
  journalctl --since "@$START_EPOCH" --no-pager -o short-precise \
    _COMM=pipewire + _COMM=wireplumber 2>/dev/null |
    grep -v -E 'spa.audioadapter: +(Id|Int|Prop|Choice|Object)' > "$RUN/pw_journal.txt"
fi

# Gap analysis.
if [ -f "$RUN/capture.f32" ]; then
  case $SRC in
  file:*)
    REFKEY=$(basename "${SRC#file:}" .wav)$(echo "$EXTRA" | tr -c 'A-Za-z0-9.-' '_')
    REF=$REPO/work-linux/runs/refs/$REFKEY.f32
    if [ ! -f "$REF" ]; then
      mkdir -p "$REPO/work-linux/runs/refs"
      # shellcheck disable=SC2086
      env -u AFM_TRACE "$BIN" -m fm "${SRCARGS[@]}" -q $EXTRA -F "$REF" 2>/dev/null
    fi
    $T/gapscan -r "$REF" "$RUN/capture.f32" > "$RUN/gaps.tsv"
    ;;
  *)
    $T/gapscan "$RUN/capture.f32" > "$RUN/gaps.tsv"
    ;;
  esac
fi
echo "$RUN"
