# Plan: measuring and evaluating PortAudio underflow risk on Linux (2026-10-20)

Status: plan only. Nothing in this memo has been run with `-P`, and no
tracked file has been changed. Machine: Raspberry Pi 5, Debian Trixie
(Raspberry Pi OS 64-bit), kernel `6.18.50+rpt-rpi-2712`. Tree: branch
`dev-macos-realtime-thread` at `f236638`.

Results: `doc/LINUX_UNDERFLOW_RESULTS_20261020.md`. This plan is kept as
written before the runs, except for this paragraph and the notes on moved
files. `work-linux/` is the working directory on the Pi and is not in the
repository; the tools from `work-linux/tools/` are copied to
`doc/linux-underflow-tools/`.

Inputs:

- `work-linux/SESSION-HANDOFF.md` (2026-10-19 handoff, written on the Mac;
  not in the repository).
- `doc/UNDERFLOW_RESEARCH_20261019.md` (the "macOS report").
- `doc/LINUX_PORTAUDIO_RESEARCH_20261019.md` (the "Linux report").
- A cpp-expert analysis of the source tree on this Pi (2026-10-20). Its
  logs are in this session's scratchpad. It ran the existing
  `build/airspy-fmradion` for file decodes and opened the Airspy HF+
  three times for a few seconds each, never with `-P`.

Evidence tags as in the Linux report: [C] code, [P] PortAudio source,
[M] measured on this Pi, [I] inference.

## 1. Questions the measurements must answer

| # | Question | Why it matters |
|---|---|---|
| Q1 | What stall tolerance does each output configuration really have on this Pi, at start and after an underflow? | The Linux report estimates about 5 ms. Section 3 below shows the value depends on block size, period, and history. |
| Q2 | How late does the decode thread (and the HF+ library threads) run under realistic and heavy load, and how often? | Underflow happens when this delay exceeds the tolerance. |
| Q3 | Combining Q1 and Q2: how many underflows per hour should be expected for each configuration and load? | This is the evaluation result. |
| Q4 | Do the program's counters (`UF=`, `Ring=`, `Trough=`, `Summary:`) match what really happens on PortAudio 19.6.0 with ALSA and with the PipeWire plugin? | The field results depend on them. The Linux report predicts `Trough=0.0ms`. |
| Q5 | Does clock drift between the SDR and the output device cause underflows that do not depend on load? | With a cushion of a few ms, drift matters much more on Linux than on macOS. |
| Q6 | Which mitigations (prefill, adapt, `-L`, `nice`, `chrt -f`, CPU governor, CPU pinning) reduce the rate, and at what latency cost? | Input for code changes (Linux report section 8). |
| Q7 | Ubuntu 26.04 only: does `UF=` stay 0 when a gap happens (the `e1b70d33` clock problem, test U3)? | Done later on the Ubuntu machine, not on this Pi. |

## 2. Facts on this Pi that change the Linux report's assumptions

From the cpp-expert run [M] unless marked:

1. **The Airspy HF+ delivers 4096 IQ samples per block here, not 2048.**
   That is 512 audio frames per write every 10.667 ms. The measured write
   interval was mean 10.665 ms, sd 0.37 ms, min 7.6 ms, and max 13.7 ms
   (270 writes). The first write is 411 frames. The library is
   `/usr/local/lib/libairspyhf.so.0` (1.8.1). The `if_blocksize = 2048`
   at `main.cpp:845` is used only for `stat_rate` and `RealtimeThread`
   [C]. Whether the Mac also gets 4096 has not been checked.
2. **The file source** (`test-files/piano_iqtest.wav`: 384 kHz, float32,
   20.000 s) writes 256 frames every 5.333 ms. Its first write is 155
   frames. The interval sd is 0.024 ms. Most test files are 20 s long;
   `interfm-20260724102822z-iq.wav` is 100 s and
   `joakfm-20260715045930z-iq.wav` is 60 s.
3. **Audio stack:**
   - PipeWire 1.4.2, wireplumber, and pipewire-pulse are running, and
     the ALSA `default` device is the PipeWire plugin. The PipeWire
     quantum is 1024 (min 32, max 2048) at 48 kHz.
   - The only ALSA playback cards are three `snd-aloop` cards
     (Loopback, Loopback_1, Loopback_2). No HDMI or USB DAC is listed.
   - The kernel has HZ=250 and aloop's `timer_source` is unset, so
     aloop's pointer moves in 4 ms steps [I].
   - PortAudio is Debian's 19.6.0-1.2+b3.
4. **Scheduling:**
   - The kernel is `SMP PREEMPT` (not RT) and the governor is
     `ondemand`.
   - `ulimit -r` is 0 and `sched_rt_runtime_us` is 950000.
   - The fair server is on: 50 ms per 1 s per CPU.
   - `perf`, `bpftrace`, `trace-cmd`, `cyclictest`, and `ts` are not
     installed. `strace`, `gdb`, `taskset`, `chrt`, `pidstat`, `mpstat`,
     and `vcgencmd` are installed, and so is python3 (in `~/.venv`).
5. **USB:**
   - The HF+ is on Bus 001 (xhci-hcd.0).
   - The RTL2838 (Bus 003) and the KIOXIA USB 3 drive (Bus 004) are on
     xhci-hcd.1.
   - The RTL-SDR could not be claimed (`usb_claim_interface error -6`),
     probably because the kernel DVB driver holds it.
6. **`build/airspy-fmradion` is stale.** It was built from `9570a05` on
   Oct 17, so it has no `Trough=`, no `(+s)` start-up count, and no
   `--realtime`.
7. **Decode CPU** on this Pi is about 7% of one core (file source) and 5%
   (HF+), for FM stereo without `-E` (`ps %CPU`). Throughput is not the
   problem; scheduling delay and cushion size are.

## 3. Model to test: the stall tolerance on ALSA depends on history

Derived from the Linux report section 3 and the code facts above [I].
Testing this model is the first goal of the measurements.

Definitions: P = ALSA period = `L` × 12 frames (`L` in ms, 48 kHz, 4
periods); B = buffer = `L` × 48 + P frames; b0 = first block; b = steady
block. PortAudio starts the PCM once at least P frames are queued, and
ALSA declares an xrun when the ring is empty.

- **At start:** S0 = the smallest total b0 + k·b that reaches P. Just
  before each later write the fill is about S0 − b, so the tolerance
  is T0 = S0 − b, minus the arrival jitter of the blocks.
- **After an underflow without a backlog** (the data was lost or never
  late: a stall of the whole process, an SDR-side drop, or drift):
  PortAudio drops and re-prepares the PCM. The next write restarts it
  once P frames are queued, so S' = ceil(P / b) · b and T' = S' − b.
  The prefill from `prime()` is gone after the drop. `adapt()` adds
  10 ms per underflow until its cap; after the cap, a further drop also
  loses that silence.
- **After an underflow with a backlog** (the decode thread was late but
  the source kept delivering): the backlog refills the ring up to
  min(B, backlog). The tolerance grows, and the extra fill stays as
  added latency (the macOS report's "latency ratchet").

Predicted tolerance (ms), if the device grants the requested sizes:

| `-L` | P (frames) | File source T0 / T' | HF+ here T0 / T' |
|---|---|---|---|
| 8 | 96 | below 0 / 0 | below 0 / 0 |
| 15 | 180 | 3.2 / 0 | below 0 / 0 |
| 40 (default) | 480 | 8.6 / 5.3 | 8.6 / **0** |
| 43 | 516 | 8.6 / 5.3 | 8.6 / 10.7 |
| 60 | 720 | 13.9 / 10.7 | 8.6 / 10.7 |
| 100 | 1200 | 24.6 / 21.3 | 19.2 / 21.3 |

"Below 0" means the second write already finds the ring empty. "0"
means that after a no-backlog restart the ring empties at about the time
the next block is due, so any late block causes another underflow. With
the HF+ jitter measured above (up to 3 ms late), this would give a series
of short gaps.

The point to check first: **with the HF+ at the default `-L 40`, a single
underflow without a backlog may leave the stream with no margin.** For
`-L` of at least 43 the post-restart margin is one block. The Linux
report's "about 5 ms" is the file-source value after a restart, not the
value at start.

With `--portaudio-prefill X` (X ≥ P), T0 ≈ X + b0 − b, limited by the
clamp 0.8·B − b0 (`prime()` uses the unraised `outputLatency` estimate)
[C]. This holds only until the first no-backlog underflow.

Unknowns that can change these numbers:

- whether PortAudio 19.6.0 starts at one period (`start_threshold` in
  `sw_params`);
- aloop's 4 ms pointer steps;
- what period and buffer the PipeWire plugin really grants;
- whether the HF+'s 512-frame blocks arrive evenly at the output.

## 4. Evaluation method

1. **Measure the tolerance per configuration directly** (Q1). Inject
   stalls of known length D and find the largest D that leaves no gap,
   both at start and after a forced no-backlog underflow (section 6,
   Phase 2c).
2. **Measure the delay distribution per load and scheduling setting**
   (Q2). The delay is "how late the write of block n happens compared
   to its nominal time". It is mostly independent of `-L` and prefill,
   so it can be measured once per load and scheduling setting, without
   `-P` (Phase 1).
3. **Predict the rate** (Q3). The expected underflows per hour for a
   configuration is the rate of delays above its tolerance from step 2.
   Also record the margin left before each write: its low tail shows
   near misses. That gives an estimate even when a run has no underflow.
   For tail extrapolation, fit the delay exceedance rate against the
   threshold (for example 2 to 30 ms) on a log scale.
4. **Verify a few cells with real `-P` runs** (Phase 2d). Compare
   predicted and observed counts, using the ground-truth gap detector,
   not only `UF=`.
5. **Report per configuration:** tolerance at start and after a restart,
   predicted and observed underflows per hour under each load, added
   latency (mean fill), and the latency ratchet after events.

Acceptance target: to be set by the user (section 9). A suggestion for
a continuous listening or streaming setup: no gap in 24 h under the
normal workload of this Pi, with the predicted rate at least 3 times
below one per day.

## 5. Tools to prepare (none needs `-P`)

All tools go in `work-linux/tools/`, and run outputs go in
`work-linux/runs/<YYYYMMDD-HHMMSS>-<label>/`.

| Tool | Purpose | Notes |
|---|---|---|
| T1 separate build | `f236638` built in `work-linux/build-f236638/` following the `build` skill | Leaves `build/` (possibly in use) alone. CMake fetches dependencies over the network. |
| T2 `pa_devs.c` | Lists PortAudio device index, name, host API, max output channels, and default low and high output latency | The program cannot list devices [C]; `-P` takes an index. |
| T3 status logger (python) | Reads stderr, splits on `\r` and `\n`, and prefixes each line with `CLOCK_MONOTONIC` time; writes a TSV | The status line has no timestamp; `-v` prints about every 107 ms (file) or 213 ms (HF+) [M]. |
| T4 stall injector (C) | Mode `thread`: `PTRACE_SEIZE` the decode thread (TID = PID), then repeat `PTRACE_INTERRUPT`, sleep D ms, `PTRACE_CONT`. Mode `process`: `SIGSTOP`/`SIGCONT`. Logs each stall's start and end time. | ptrace stops one thread only, needs no root (same uid, no Yama), and avoids the fair-server problem: with a `SCHED_FIFO` busy loop the starved thread still gets 50 ms/s. |
| T5 `/proc/asound` sampler (shell or python) | Saves `hw_params` and `sw_params` once after start, and polls `status` (state, avail, `hw_ptr`, `appl_ptr`) every 50 to 100 ms | Ground truth for period, buffer, and `start_threshold`. Works for hw and aloop PCMs, not for the PipeWire plugin. |
| T6 capture and gap analysis (python, numpy) | `arecord` on the loopback capture side to WAV. For file sources, aligns the capture with the bit-exact `-R` reference and reports each inserted silence (position, length). For live sources, reports runs of digital zero of 0.5 ms or longer on both channels. | Check the false-positive rate on a run without stalls. Log `arecord` overruns, which would look like gaps; use a large capture buffer. |
| T7 measurement-only instrumentation | Per the cpp-expert proposal: a preallocated trace ring on the decode thread with pull time, decode end, write start and end, pre- and post-write free frames, the underflow flag, and the queue length; push timestamps in `DataBuffer`; one-time start-up lines with host API, granted `outputLatency`, and free frames right after `Pa_StartStream()` (that is B). Dumped at exit to a path from an environment variable. | Written by cpp-expert as `work-linux/*.diff` (now `doc/LINUX_UNDERFLOW_RESULTS_20261020_trace.diff`), built only in `work-linux/`, never committed unless asked. Without it, Phase 1 needs root tracing instead. |

Optional, if the user allows installs: `stress-ng`, `rt-tests`
(`cyclictest`), `trace-cmd` or `linux-perf` (it must match the rpt
kernel), `iperf3`, and `moreutils` (`ts`).

## 6. Phases

### Phase 0: setup and records (no `-P`)

1. Record the environment in each run directory:
   - `uname -a`, `ulimit -r -l -e`, `sched_rt_runtime_us`, and the
     governor;
   - `vcgencmd get_throttled`, `measure_temp`, and `measure_clock arm`;
   - `aplay -l`, `aplay -L`, `cat /proc/asound/cards`, and `lsusb -t`;
   - `dpkg -l | grep -i portaudio` and `apt-cache policy
     libportaudio2` (this answers the Linux report's open question 3);
   - `pw-metadata -n settings`;
   - the binary's `Git Commit SHA1` banner.
2. Ask the user which loopback card and subdevice are free (section 9).
   Confirm with `/proc/asound/Loopback*/pcm0p/sub*/status` and
   `pcm1c/sub*/status`; both sides must show `closed`.
3. Build T1, then T2 to T6. Ask cpp-expert for T7 if the user approves.
4. Check the decoder baseline: a file decode with the T1 binary to `-R`
   must be byte-identical to the `build/` binary's output (3839596
   bytes).

### Phase 1: delay distribution without `-P` (Q2)

Run the T7 build with `-R` output (or `-R` plus root tracing). The time
from source push to write start is the scheduling part of the delay. The
source's arrival jitter is the other part.

1. Sources:
   - the file source, using the 100 s file, repeated;
   - the HF+ live, only when the user is not using it, for 10 to 30
     minutes per cell.
2. Loads, each for at least 10 minutes:
   - L0: idle.
   - L1: CPU on all cores (`stress-ng --cpu 4`, or four `yes >/dev/null`).
   - L2: memory, `stress-ng --vm 2 --vm-bytes 75%`.
   - L3: storage writes to the SD card and to the USB drive.
   - L4: a `-j4` build of a copy of this repository.
   - L5: network transfer (`iperf3` or a large download).
   - L6: L1 for 30 minutes, to reach thermal throttling, while
     watching `get_throttled`.
   - L7: the user's normal workload on this Pi.
3. Scheduling variants. All except S0 need sudo (section 9).
   - S0: as is.
   - S1: `nice -n -10`.
   - S2: `chrt -f 10` for all threads (launch under `chrt`).
   - S3: S2 plus `taskset -c 2,3`, with the load on cores 0 and 1.
   - S4: S0 with the `performance` governor.
4. Record the delay percentiles (p50, p99, p99.9, max) and the counts
   per hour above 2, 5, 10, 20, and 30 ms. Also record the per-block
   decode time, to check that `-E 36` and `-E 100` stay well below the
   block period on the Pi's cores.
5. If installs are allowed, run `cyclictest` (`--policy=other` and
   `-p 10`) under each load as a baseline for the system, independent
   of this program.

### Phase 2: silent `-P` to an unused loopback device (Q1, Q3, Q4, Q5)

This needs approval (section 9). It never uses the PipeWire default
or a device that something else reads.

- **2a. Static check.** Run the file source with `-P <idx> -v` at `-L` 8,
  15, 40, 43, 60, and 100, without and with `--portaudio-prefill` 10 and
  30.
  - From T5 (`hw_params` and `sw_params`), record the granted period,
    buffer, and `start_threshold`.
  - From T7, record `outputLatency`.
  - Compare the fill trace with section 3's S0 and T0.
- **2b. Telemetry check.** Inject single stalls (T4 `process` mode) of
  20, 50, and 300 ms.
  - Check that each one gives exactly one capture gap of the expected
    length.
  - Check that `UF=` rises by one per event on 19.6.0.
  - Confirm the `Trough=0.0ms` bias, and correct `Ring=` and `Trough=`
    offline as true fill = shown + (B − capacity).
  - Check that the end-of-stream gap is never reported (the latch).
  - Check whether aloop feeds the capture side zeros while the playback
    side is in xrun.
- **2c. Tolerance calibration.** One injected stall per program run, so
  each trial starts fresh.
  - D from 2 to 30 ms in 1 to 2 ms steps, plus 50 and 100 ms, with
    about 10 trials per D.
  - Use both T4 modes: `thread` (with backlog) and `process` (no
    backlog).
  - Then a two-stall run: a `process` stall that forces an underflow,
    then a short `thread` stall, to measure T' and test the "HF+ at
    `-L 40` has no margin after a restart" prediction.
  - Repeat the key cells with the HF+ live.
- **2d. Verification under load.** Pick 4 to 6 configurations: `-L 40`,
  `-L 40 --portaudio-prefill 30`, `-L 43`, `-L 60`, and the best
  result from 2c. Run them under L1, L4, and L7 with S0, and with S2 if
  approved. Use 30 to 60 minutes per cell with the HF+, and the 100 s
  file repeated for the file source. Compare observed gaps (T6) with the
  Phase 1 prediction, and record the added latency.
- **2e. Drift.**
  - First a controlled case: the file source with `-r +100` (the ring
    drains at about 4.8 frames/s) and with `-r +20`. Measure the time to
    the first underflow and what follows. Prediction for the file source
    at `-L 40`: about 411 / 4.8 ≈ 86 s, then the post-restart margin of
    5.3 ms.
  - Use `-r 0.0001` as the control, since any non-zero `-r` turns on the
    IF resampler [C].
  - Then the real case: the HF+ live to the loopback for 1 to 2 hours
    with no load. The slope of the pre-write fill (T7) gives the
    SDR-to-Pi clock offset in ppm, and from it the expected time
    between drift-caused underflows. Note that aloop runs on the Pi's
    system clock, not on a DAC clock, so a real DAC needs its own run
    later.
- **2f. The PipeWire path**, only if the user uses it on this Pi.
  - Run `-P <PipeWire device> -v` to a null sink or to a sink the user
    names.
  - Record the client's period and buffer, the graph quantum, and the
    ERR count in `pw-top`.
  - Capture from the sink's monitor with `pw-record` for T6.
  - Repeat 2b and a short 2c here, since the plugin reports `-EPIPE`
    differently from a hw PCM.

### Phase 3: mitigations and decision (Q6)

Using the Phase 1 delay tables and the Phase 2 tolerance tables:

- rank `-L`, prefill, adapt, `chrt`, the governor, and pinning by
  predicted rate and latency cost;
- run the best two configurations for 24 h in the user's real setup;
- then decide which code changes from the Linux report section 8 to
  write. Candidates are the ALSA capacity fix, an underflow detector
  independent of PortAudio, a Linux `--realtime`, a default ALSA
  prefill, and keeping the prefill after a restart.

### Phase 4: Ubuntu 26.04 machine (later)

Repeat Phase 0, 1, 2a, 2b, and 2c on the Ubuntu box. Add U3 (Linux
report section 9, step 9) on an aloop device and through PipeWire. Note
that there `Pa_Initialize()` needs a reachable PulseAudio server.

## 7. Rules for the test runs

- Never run `-P` to a device that is in use, or to the PipeWire default,
  without the user's approval. Never kill an `airspy-fmradion` process
  this session did not start.
- Use the HF+ only when the user says it is free. An SDR can be opened
  by one process only.
- Script runs with `timeout -s INT -k 10`. `SIGINT` gives a clean exit
  with the `Summary:` line [M, file output]. A process stuck in
  `Pa_WriteStream()` needs `-k` [P].
- Undo every root change after each session: the governor, `sysctl`,
  tracefs, and `chrt` limits.
- Watch `vcgencmd get_throttled` during loads; stop if the Pi
  throttles in a run that is not meant to test throttling.
- Results from aloop and PipeWire are labeled by output path. They do
  not stand for a USB or HDMI DAC.

## 8. Corrections for the Linux report (for a later doc update)

1. The HF+ block on this Pi is 4096 IQ samples, which gives 512 frames
   per 10.667 ms. The report's 256 frames per 5.33 ms holds for the
   file source only.
2. The report's "about 5 ms" tolerance is the file-source value after a
   restart. At start it is about 8.6 ms. For the HF+ at `-L 40` the
   value after a restart may be 0 (section 3).
3. Prefill protects only until the first underflow without a backlog
   [I]; the report presents it as a lasting cushion.
4. This Pi runs PipeWire, and its only playback cards are snd-aloop. The
   report assumed a Lite image with no sound server and a USB DAC or
   HDMI.

## 9. Questions and approvals for the user

1. How is audio played from this Pi in practice? Possibilities include a
   loopback card read by a streaming program, or PipeWire to a network
   sink. Which loopback cards and subdevices are free for tests?
2. Which SDR is used here (the HF+ on Bus 001?), and when is it free for
   tests?
3. Approvals, each separately:
   - `-P` to an unused loopback subdevice (silent);
   - `-P` through PipeWire (Phase 2f);
   - sudo for `nice`, `chrt`, the governor, `kernel.sched_schedstats`,
     and tracefs;
   - installing `stress-ng`, `rt-tests`, `trace-cmd` or `linux-perf`,
     `iperf3`, and `moreutils`;
   - the measurement-only instrumentation (T7), built in `work-linux/`
     only;
   - a separate build in `work-linux/build-f236638/`.
4. Targets: what latency is acceptable, and what underflow rate counts
   as acceptable (section 4)?
5. Is the "normal workload" (L7) of this Pi known: other receivers,
   encoders, or a desktop session?
