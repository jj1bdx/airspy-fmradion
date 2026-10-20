# Tools for the Linux underflow measurements (2026-10-20)

Scripts and programs used for `doc/LINUX_UNDERFLOW_RESULTS_20261020.md`,
following `doc/LINUX_UNDERFLOW_MEASUREMENT_PLAN_20261020.md`. They are kept
as they were run on the Raspberry Pi 5.

The shell scripts hard-code the paths used on the Pi: the repository at
`/home/kenji/src/airspy-fmradion`, the tools in `work-linux/tools/`, the
builds in `work-linux/build-f236638/` and `work-linux/build-t7/`, and the
run data in `work-linux/runs/`. None of these directories is in the
repository. Change the paths before running the scripts elsewhere.

`run_pa.sh` plays with `-P` to an `snd-aloop` loopback card or to
PipeWire. The scripts use the Airspy HF+, `sudo` (for `prlimit` and
`trace-cmd`), `chrt`, `stress-ng`, and `iperf3`.

## Building the C programs

```sh
cc -O2 -o gapscan gapscan.c -lm
cc -O2 -o stall_inject stall_inject.c
cc -O2 -o pa_devs pa_devs.c -lportaudio
```

The measurement-only trace build is `f236638` with
`doc/LINUX_UNDERFLOW_RESULTS_20261020_trace.diff` applied.

## Files

| File | Purpose |
|---|---|
| `run_pa.sh` | One test run: plays to a loopback card or PipeWire, captures the output, samples `/proc/asound`, and optionally injects stalls, runs a load, or sets a scheduling policy. Settings are environment variables listed at the top. Writes one run directory. |
| `phase2.sh` | Phase 2a (static check) and 2c (stall ladders), file source and HF+. |
| `phase3.sh` | Phase 2d: HF+ under load, scheduling variants, and PipeWire. |
| `phase3b.sh` | `phase3.sh` with the `iperf3` time set to 86400 s, its maximum. |
| `phase3_net.sh` | Rerun of the network load (L5). |
| `phase3_drift.sh` | Phase 2e: forced drift with a positive `-r`. |
| `phase3_pw.sh` | Phase 2f: HF+ to PipeWire, captured from the sink monitor. |
| `pw_dualcap.sh` | Phase 2f: HF+ to PipeWire, captured from the sink monitor and from the loopback card's capture side. |
| `phase4_trace.sh` | Phase 4: scheduler trace (`trace-cmd`) of an idle HF+ run. |
| `summarize.sh` | One summary line per finished run. |
| `gapscan.c` | Finds gaps in a captured stereo FLOAT_LE stream: runs of zeros, and inserted or lost frames against a reference decode. |
| `stall_inject.c` | Stops one thread (ptrace) or a whole process (`SIGSTOP`) for given durations and logs each stall. |
| `pa_devs.c` | Lists PortAudio devices; `run_pa.sh` uses it to find the device index. |
| `asound_sample.py` | Samples an ALSA substream through `/proc/asound`. |
| `statuslog.py` | Timestamps `airspy-fmradion` stderr lines. |
| `trace_stats.py` | Summarizes a T7 block trace. |
| `stall_eval.py` | Evaluates injected stalls against the T7 trace and the capture gaps. |
| `wake_latency.py` | Wake-up latency of one thread from a `trace-cmd` report, with the tasks that held the CPU for each latency over 2 ms. |
| `interleave32.py` | Interleaves two mono 32-bit raw files into one stereo file. |
