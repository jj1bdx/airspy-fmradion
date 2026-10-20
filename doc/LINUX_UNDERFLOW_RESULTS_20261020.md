# Linux underflow measurements on the Raspberry Pi 5 (2026-10-20)

Status: measurements finished on 2026-10-20. Plan:
`doc/LINUX_UNDERFLOW_MEASUREMENT_PLAN_20261020.md`. Tools:
`doc/linux-underflow-tools/`. The run data (captures, logs, and traces,
5.1 GB, one directory per run named `<UTC time>-<label>`) and the builds
were kept on the Pi outside the repository and were deleted on
2026-10-20.

## Setup

- Raspberry Pi 5, Debian Trixie, kernel `6.18.50+rpt-rpi-2712` (`SMP
  PREEMPT`), governor `ondemand`, PortAudio 19.6.0 (Debian 19.6.0-1.2+b3).
- Binaries, each in its own build directory:
  - f236638, unmodified.
  - f236638 plus the measurement-only trace
    (`doc/LINUX_UNDERFLOW_RESULTS_20261020_trace.diff`; format in
    `doc/LINUX_UNDERFLOW_RESULTS_20261020_trace_format.md`). Decoded audio
    is byte-identical to the unmodified build.
- Output: PortAudio device 4, `Loopback: PCM (hw:2,0)` (ALSA, card
  Loopback_2). The capture side `plughw:2,1,0` is recorded with `arecord`
  as FLOAT_LE and compared with a `-F` decode of the same file (`gapscan`),
  so every inserted or lost frame is found exactly.
- Background processes always running: `dump1090-fa` (RTL-SDR, nice -5,
  about 7% CPU), `fr24feed`, PipeWire 1.4.2 with wireplumber and
  pipewire-pulse (idle). These are the Pi's normal workload.
- Test processes run at nice 0, `SCHED_OTHER`, unless stated.

### The `chrt` command and why the tests use it

`chrt` (util-linux) runs a command with a chosen Linux scheduling policy
and priority, or changes them for a running process. `chrt -f 10 cmd`
runs `cmd` under `SCHED_FIFO` at real-time priority 10. The kernel
always runs a runnable `SCHED_FIFO` thread before any normal
(`SCHED_OTHER`) thread on the same CPU, and that thread keeps the CPU
until it blocks or a higher-priority real-time thread wants it. Threads
created after the start inherit the policy. That covers the decode
thread, the source thread, and the libairspyhf and libusb threads.

Why the tests use it:

- **It tests the main mitigation without code changes.** The load tests
  show the decode thread losing its margin when it shares CPUs with
  normal-priority load on equal terms. Running the program under `chrt
  -f 10` shows whether a real-time policy removes those underflows. A
  Linux branch of `--realtime` would set the same policy (Linux report
  section 6.3).
- **For comparison, `nice -n -10` is also tested.** It only gives the
  thread a larger share within `SCHED_OTHER`. It cannot preempt other
  work the way a real-time policy can.
- **Priority 10 is deliberately low.** It is above all normal processes
  but below the existing real-time threads on this Pi. Those are the
  kernel's display threads (`SCHED_FIFO` 50), `rtkit-daemon` (`SCHED_RR`
  99), and the `arecord` capture (20). The program therefore cannot
  delay the kernel or the measurement. PipeWire's threads showed no
  real-time policy while idle (`ps -eLo cls,rtprio`). If PipeWire gets
  one through rtkit while playing, a lower priority for this program
  keeps the audio server first.
- **Privileges:**
  - `ulimit -r` is 0 for the user here, so `chrt` would fail without
    privileges.
  - The run script raises the real-time limit of its own shell once
    with `sudo prlimit --pid $$ --rtprio=30:30`. The program itself
    then runs as the normal user, not as root, and the limit ends with
    that shell.
  - The system-wide limit `kernel.sched_rt_runtime_us` (950000 of
    1000000) keeps all real-time threads below 95% of each second, so
    a runaway thread cannot lock up the Pi.
- **The capture side also uses it.** `arecord` runs under `chrt -f 20`
  in every test, so the recording itself does not drop data under load
  and the gap count reflects only the program under test.

## Results

### ALSA parameters granted at `-L 40` (loopback)

From `/proc/asound/card2/pcm0p/sub0/{hw_params,sw_params}` and the trace
header [M]:

| Item | Value |
|---|---|
| format, channels, rate | FLOAT_LE, 2, 48000 |
| period_size / buffer_size | 480 / 2400 frames (10 / 50 ms) |
| start_threshold / stop_threshold | 480 / 2400 |
| `outputLatency` | 40.000 ms |
| free frames right after `Pa_StartStream()` (B) | 2400 |

This matches the Linux report's mapping (period `L/4`, buffer `L` + one
period, PCM started at one period).

### File source, `-L 40`, idle

| Run | Length | Underflows (`UF`) | Capture gaps | Lowest fill before a write | Notes |
|---|---|---|---|---|---|
| `20261020-050420-smoke-file-L40` (no trace) | 20 s | 1 | 1 gap, 0.77 ms, at 15.8 s | 7.2 ms (from `/proc`, 20 ms windows) | Pointers reset at 15.78 s (drop and restart); after the restart the lowest fill rose to 8.0 ms |
| `20261020-051325-t7-file-L40-interfm` | 100 s | 0 | 0 | 3.2 ms (one write); 7.2 ms at the 1st percentile | Largest push-to-write delay 4.4 ms; file pacing jitter up to 3.6 ms |

Observations [M]:

- The fill moves in 192-frame (4 ms) steps, as aloop's pointer advances
  once per jiffy (HZ = 250).
- The status line reports `Trough=0.0ms` and `min ring fill=0.0ms` while
  the real lowest fill was 3.2 ms, and `Ring=` capacity reads 46.8 ms
  instead of 50 ms. This confirms the telemetry bias in the Linux report
  (section 4.3).
- PortAudio 19.6.0 reported the one real gap as one underflow.
- At the end of each file run, the last 475 to 512 frames (about one
  block) are missing from the capture: the stream stop drops what is
  left in the ring. This is not an underflow.

### Airspy HF+, 82.5 MHz, `-L 40`, idle, 15 minutes

Run `20261020-051705-hf-idle-L40-15min` [M]:

| Item | Value |
|---|---|
| Underflows, capture gaps, SDR drops, queue drops | 0, 0, 0, 0 |
| Block | 512 frames every 10.667 ms (4096 IQ samples) |
| Fill before a write | median 11.2 ms; 1st percentile 7.2 ms; minimum 0.56 ms |
| Writes with less than 2 / 4 / 6 ms left | 4 / 5 / 16 in 15 minutes |
| Push-to-write delay | p50 0.76 ms, p99.9 0.88 ms, max 5.1 ms |
| Decode thread wait in `DataBuffer` | p99.9 0.03 ms, max 4.25 ms |
| Decode time per block (wall and CPU) | 0.75 ms median, 0.95 ms max |
| HF+ push interval | median 10.666 ms; min 3.6 ms; max 17.7 ms |
| HF+ sample clock against `CLOCK_MONOTONIC` | −0.395 ppm |

The near misses have two causes:

1. **Late HF+ USB delivery.** A block arrives up to 7 ms late, and the
   next one follows after about 3.6 ms. This gave the two 0.56 ms near
   misses at 16 s and 278 s.
2. **Decode-thread wake-up about 4 ms late.** The block waits 3.5 to
   4.25 ms in `DataBuffer` before the decode thread pulls it, about
   once a minute. That is one scheduler tick (HZ = 250). The suspicion
   that `dump1090-fa` caused this was wrong: see "Phase 4: what delays
   the decode thread on an idle system" below.

**Drift:** the HF+ runs 0.395 ppm slow against the Pi's clock. The
pilot-based `ppm=` on the status line also reads −0.39, which suggests
the Pi's NTP-disciplined clock is close to true time. aloop consumes at
the Pi's clock rate, so the ring drains by 0.019 frames/s. The idle
trough of about 7 ms (347 frames) lasts about 5 hours. A 24/7 stream
should therefore expect a drift underflow about every 5 hours, plus
whatever follows the restart (Phase 2).

**Note on fill readings [I]:** `Pa_GetStreamWriteAvailable()` reads the
hardware pointer as the kernel last updated it. For aloop that happens
at its timer callbacks (period boundaries) and at status reads. The
fill can therefore be overstated by up to one period (10 ms at
`-L 40`). The kernel decides an xrun at those same updates. The capture
comparison is the ground truth for gaps.

### Phase 2a: static check (idle; file 20 s, HF+ 60 s per run)

`doc/linux-underflow-tools/summarize.sh` output, condensed [M]. "Fill"
is the fill before a write (the margin when the block arrives). "Events" are capture
insertions (file source, against the reference) or zero runs and exact
repeats (HF+).

| Source, `-L`, prefill | period / buffer | UF | Events | Fill min / 0.1 % (ms) |
|---|---|---|---|---|
| file 8 | 96 / 480 | 20 | 99: 81 repeats of 480 frames + 18 silences, 842 ms inserted; queue peak 155 blocks | 0 / 0 |
| file 15 | 180 / 900 | 1 | 1 silence, 0.77 ms | 0 / 3.2 |
| file 21 | 252 / 1260 | 1 | 1 silence, 0.77 ms | 0 / 1.9 |
| file 40 | 480 / 2400 | 0 | 0 | 3.2 / 7.2 |
| file 43 | 516 / 2580 | 0 | 0 | 3.2 / 7.2 |
| file 60 | 720 / 3600 | 0 | 0 | 3.2 / 11.2 |
| file 100 | 1200 / 6000 | 0 | 0 | 3.2 / 17.9 |
| file 15, prefill 30 (clamped) | 180 / 900 | 0 | 0 | 7.0 / 7.0 |
| file 40 / 60 / 100, prefill 30 | | 0 | 0 | 23.9 to 26.6 |
| HF+ 40 | 480 / 2400 | 0 | 0 | 7.2 / 7.2 |
| HF+ 43 | 516 / 2580 | 0 | 0 | 5.9 / 5.9 |
| HF+ 60 | 720 / 3600 | 0 | 0 | 5.9 / 5.9 |
| HF+ 100 | 1200 / 6000 | 0 | 0 | 8.6 / 16.6 |
| HF+ 40 / 60, prefill 30 | | 0 | 0 | 25.2 / 25.2 |

Findings:

- ALSA grants period = `L`/4 and buffer = 5 periods for every `-L`
  tested, with `start_threshold` = one period.
- `-L` 8 is unusable: it underflows continuously and adds latency (842
  ms in 20 s). `-L` 15 and 21 underflow once at start, as predicted
  (T0 at or below 0 to 3 ms).
- For the HF+, `-L` 43 and 60 do not raise the margin at start (S0 is
  still 923 frames); only `-L` 100 does. This matches section 3 of the
  plan.
- `--portaudio-prefill 30` raises the margin to about 25 ms for both
  sources, which is prefill minus about 5 ms.
- **On aloop an xrun can come out as a repeat of the stale ring
  contents, not silence.** At `-L` 8, 81 of 99 events repeated a full
  buffer (480 frames). The streaming program would hear a 10 ms
  stutter. PortAudio reported 20 underflows for 99 events, so `UF=`
  undercounts there. At `-L` 15 and above, every event so far was one
  silence with one `UF`.

### Phase 2c: stall ladders

Stalls of 2 to 100 ms, one every 3 s, in one run. "thread": only the
decode thread stops (ptrace), so the source keeps delivering and a
backlog forms. "process": `SIGSTOP` on the whole process; the file source
drops the missed time, so no backlog forms. The HF+ keeps its USB data
in kernel and library buffers, so it gets a backlog even in process
mode. Per-stall results are from
`doc/linux-underflow-tools/stall_eval.py` [M].

| Run | Largest stall with no gap (before the first event) | After the first event |
|---|---|---|
| file `-L` 40, thread | 10 ms (12 ms gave 0.77 ms) | margin grows with each backlog refill: 16 to 20 ms stalls pass, 25 ms and up give gaps of about stall − margin |
| file `-L` 40, process | 12 ms (0.77 ms gap) | **margin stays at about 10 ms (trace reading; true value is lower)**: every stall of 14 ms or more gives a gap of about stall − 6 to 8 ms |
| file `-L` 60, process | 12 ms | same pattern, margin about 14 ms |
| file `-L` 40, prefill 30, thread | 30 ms | 40 ms and up give gaps |
| file `-L` 40, prefill 30, process | 16 ms (0.1 ms gap) | **prefill lost**: margin drops to about 10 ms, and every stall of 18 ms or more gives a gap |
| HF+ `-L` 40, thread / process | 20 ms (UF flags without a gap at 14 and 20 ms) | gaps from 25 ms; margin grows with the backlog |
| HF+ `-L` 60, thread / process | 8 ms (10 ms gave an event) | similar |
| HF+ `-L` 40, prefill 30, process | 30 ms | 40 ms and up give gaps |

Findings:

- A stall starts at a random point of the block cycle, so the stall it
  takes to cause an underflow is between the fill before a write (about
  7 ms at `-L` 40) and the fill after it (about 20 ms). The quantity
  that decides real underflows is the **lateness of a write against the
  fill before it**, and the trace measures both directly.
- **Confirmed:** after an underflow without a backlog, the stream
  restarts with one or two blocks and the prefill is gone. Prefill
  therefore protects only until the first no-backlog event (an SDR-side
  drop, or drift).
- **Confirmed:** after an underflow with a backlog, the margin and the
  latency both grow (the latency ratchet).
- The UF flag and the capture agree on every event at `-L` 40 and 60,
  except some UF flags with no measurable gap (under 0.1 ms).

### Phase 2d: HF+ under load, `-L 40`, 10 minutes each

Test process at nice 0, `SCHED_OTHER`, governor `ondemand` [M].

| Load | UF | Ring empty (trace) | Audible gaps (capture) | Fill 0.1 % (ms) | Push-to-write delay p99.9 / max (ms) | Delays over 5 / 8 / 10 ms per hour | Decode wall max (ms) | Temp after |
|---|---|---|---|---|---|---|---|---|
| none (15 min baseline) | 0 | 0 | 0 | 7.2 | 0.88 / 5.1 | 4 / 0 / 0 | 0.95 | 57 °C |
| L1 CPU, `stress-ng --cpu 4` | 52 | 51 | 11 silences, 23.4 ms total | 1.3 | 5.5 / 9.4 | 822 / 6 / 0 | 9.2 | 76 °C |
| L4 build, `cmake --build -j4` loop | 4 | 4 | 2 silences, 6.1 ms | 3.2 | 4.8 / 10.2 | 306 / 42 / 6 | 8.6 | 74 °C |
| L2 memory, `stress-ng --vm 2 --vm-bytes 75%` | 1 | 1 | 1 silence, 4.8 ms | 3.2 | 4.6 / 12.2 | 162 / 24 / 6 | 10.0 | 72 °C |
| L3 storage, `stress-ng --hdd 2` on NVMe | 0 | 0 | 0 | 8.6 | 1.5 / 10.8 | 30 / 6 / 6 | 5.6 | 68 °C |
| L5 network, `iperf3 -P 4` over 127.0.0.1 (rerun) | 4 | 4 | 3 silences, 14.1 ms total | 5.3 | 9.7 / 15.3 | 17151 / 1926 / 234 | 13.2 | 77 °C |

The first L5 run was invalid (`iperf3` rejected `-t 100000`); the
rerun used `-t 86400`. L5 sends TCP over the loopback interface, so it
loads the CPUs through the kernel network stack (softirq and copies),
not the Ethernet hardware.

Runs L1 to L4 reported `get_throttled` 0x0 (no throttling). Queue
length never exceeded 2, and there were no SDR drops. During the `nice`
variant run (next section), `get_throttled` changed to 0x80000: bit 19,
"soft temperature limit has occurred". The bit stays set until reboot,
so it does not show which later runs, including the L5 rerun, were
affected; temperatures peaked at 79 °C.

Findings:

- **Full CPU load on all cores is the worst case:** 11 audible gaps in
  10 minutes (about 66 per hour), each 0.5 to 4 ms. Under equal-weight
  `SCHED_OTHER` competition, the decode thread's wall time per block
  rises from 0.95 ms to 9.2 ms (it is preempted), and the 7 ms margin
  is used up.
- **Most PortAudio underflows on aloop are not audible:** 52 UF and 51
  ring-empty writes, but only 11 silences in the capture. The ring hits
  exactly empty at an aloop pointer update and is refilled before any
  silence is played. On aloop, `UF=` overcounts what a listener hears.
  The reverse happens at `-L` 8, where repeats are not counted.
- A build load or memory pressure gives a few gaps per hour. Storage
  I/O on the NVMe gives none.
- Network load through the loopback interface is the second worst: the
  push-to-write delay exceeded 8 ms 1926 times per hour and reached
  15.3 ms, and HF+ blocks arrived up to 26 ms apart. It still gave only
  3 gaps, because most long delays were followed by a backlog that
  refilled the ring. Two of the gaps came in the first 2 s, while the
  start margin was small.
- The HF+ drift figure is reliable only on an idle system. Under load,
  the push timestamps give −0.96 to +0.28 ppm.

### Phase 2e: forced drift (no-backlog underflows)

**Purpose.** In a 24/7 stream, the HF+ clock and the output clock (the
Pi's system clock for aloop) differ by a fraction of a ppm. When the SDR
is the slower one, the ring drains slowly until it underflows. Such an
underflow has no backlog behind it: no blocks wait in `DataBuffer` to
refill the ring. The stall ladders could not produce this case with the
HF+, because its USB buffers keep the data during a stop. At the
measured real drift (−0.395 ppm) the first event would take about 5
hours. These tests answer two questions in minutes:

- What does the stream do after a no-backlog underflow?
- How does the outcome depend on `-L` and prefill?

**Method.** `-r 200` makes the decoder produce 200 ppm fewer audio frames
than the output consumes, so the ring drains by 9.6 frames/s, about 500
times faster than the real drift. Nothing else changes, apart from the
IF resampler being switched on. The runs used the HF+ at `-L` 40, at
`-L` 43, and at `-L` 40 with `--portaudio-prefill 30`, 240 s each, plus
the file source at `-L` 40 with `-r 100` for 100 s.

**Prediction** (plan section 3; written before the runs):

- `-L` 40: the period (480 frames) is smaller than one HF+ block (512
  frames). After the first underflow, PortAudio restarts the PCM as
  soon as one block is queued, so the post-restart margin T' is 0. With
  the ring still draining, the stream should underflow again and again:
  a cascade.
- `-L` 43: the period (516 frames) exceeds one block. The restart waits
  for two blocks, T' is one block (512 frames, 10.7 ms), and the stream
  should underflow once every 512 / 9.6 ≈ 53 s, without a cascade.
- `-L` 40 with prefill 30: the first underflow should be delayed by about
  25 ms of margin / 9.6 frames/s ≈ 125 s. After it the prefill is gone,
  and the cascade of `-L` 40 should follow.
- In every case, the total silence inserted over the run should equal
  the drift deficit. The ring cannot create frames, so the deficit has
  to come out as silence somewhere.

**Results**, 240 s per HF+ run [M]:

| Run | First underflow | After it | UF | Silences in the capture (any length) |
|---|---|---|---|---|
| HF+ `-L` 40 | 20 s | **cascade**: a one-frame silence about every 64 ms until the end | 2213 | 1525 runs (1489 of one frame), 44.6 ms total |
| HF+ `-L` 43 | 40 s | one gap of about 386 frames (8 ms) every 40 s | 5 | 5 runs, 40.3 ms total |
| HF+ `-L` 40, prefill 30 | 137 s (prefill 30 ms / 9.6 frames/s) | the same cascade as `-L` 40 (the prefill is gone) | 1425 | 983 runs, 21.0 ms |
| file `-L` 40, `-r 100` (100 s) | 30 s | second gap at 39 s | 2 | 2 gaps, 6.5 ms |

The total silence equals the drift deficit after the first underflow.
The ring cannot create frames, so the deficit always comes out as
silence. The configuration decides whether it comes out as one longer
gap or as a train of clicks.

Prediction against result:

| Prediction | Result |
|---|---|
| `-L` 40: cascade after the first underflow | Confirmed: one-frame silences about every 64 ms for the rest of the run |
| `-L` 43: one gap per 53 s, no cascade | No cascade, confirmed. The spacing was 40 s, and each gap was about 386 frames (9.6 frames/s × 40 s = 384). The usable margin after a restart is about 384 frames, not 512; the difference (about 2.7 ms) fits the HF+ arrival jitter and aloop's 4 ms pointer steps. |
| Prefill 30: first underflow at about 125 s, then the cascade | Confirmed: 137 s, then the cascade |
| Total silence equals the drift deficit | Confirmed. Silence against the deficit after the first underflow: 2139 / 2112 frames (`-L` 40), 1932 / 1920 (`-L` 43), 1009 / 990 (prefill 30) |

This confirms the history-dependent model in section 3 of the plan:

- With the HF+'s 512-frame blocks and `-L` 40, the period is 480 frames,
  smaller than one block. After a no-backlog underflow, the stream
  restarts holding one block and has no margin. Every later deficit
  frame becomes a one-frame dropout: a click.
- At `-L` 43 the period (516) exceeds one block, so the restart holds
  two blocks and the stream keeps one block (10.7 ms) of margin. The
  deficit then accumulates and comes out as one gap of about 8 ms each
  time.
- Prefill only delays the first event.

**Expected effect of the real drift** (HF+ −0.395 ppm against the Pi
clock, 0.019 frames/s) [I, from the measured rates]:

| Config | First drift underflow | After it |
|---|---|---|
| `-L` 40 (also the default) | about 5 h (347 frames) | a one-frame click about every 53 s, until a scheduling delay creates a backlog that refills the ring |
| `-L` 43 or more | about 5 h | one gap of about 8 ms (512 frames) about every 7.5 h |
| prefill 30 at `-L` 40 | about 17 h | as `-L` 40 |

The sign matters. An SDR running fast against the output would never
drain the ring; the backlog would grow instead. This HF+ runs slow.

### Phase 2d variants: mitigations under full CPU load

Same load as L1 (`stress-ng --cpu 4 --cpu-method all`), HF+ at
82.5 MHz, 10 minutes each [M]. Only one setting changes per run.
"Delay" is the push-to-write delay: from the HF+ block entering
`DataBuffer` to the start of its `Pa_WriteStream()` call.

| Variant | UF | Ring empty | Audible gaps | Fill 0.1 % (ms) | Delay max (ms) | Delays over 5 / 8 / 10 ms per hour | Decode wall max (ms) | HF+ push interval max (ms) |
|---|---|---|---|---|---|---|---|---|
| none, `-L 40` (Phase 2d) | 52 | 51 | 11, 23.4 ms total | 1.3 | 9.4 | 822 / 6 / 0 | 9.2 | 19.7 |
| `chrt -f 10`, `-L 40` | 0 | 0 | 0 | 8.6 | 0.93 | 0 / 0 / 0 | 0.83 | 16.9 |
| `nice -n -10`, `-L 40` | 2 | 2 | 2: 0.8 ms at 5 s, 9.3 ms at 23 s | 7.2 | 7.3 | 300 / 0 / 0 | 5.5 | 27.6 |
| `-L 40`, prefill 30 | 0 | 0 | 0 | 19.9 | 8.3 | 480 / 6 / 0 | 8.3 | 19.1 |
| `-L 60` | 1 | 1 | 1: 32.8 ms at 0.3 s | 34.7 | 11.6 | 420 / 18 / 12 | 8.8 | 47.9 |

Findings:

- **`SCHED_FIFO` removes the CPU-load problem.** Under `chrt -f 10` the
  decode thread is never preempted by the load: the decode wall time
  per block stays at the idle value (0.83 ms), and no delay reached
  1 ms. The HF+ library and libusb threads inherit the policy, so block
  delivery was also no worse than idle (16.9 ms maximum interval).
- **`nice -n -10` helps but does not fix it.** A higher-weight
  `SCHED_OTHER` thread still has to wait for the running thread's time
  slice to end, which the kernel checks at its 4 ms tick. Delays over
  5 ms fell from 822 to 300 per hour, and both gaps came in the first
  23 s, while the start margin was small.
- **Prefill 30 absorbed the load:** the 0.1 % fill stayed at 19.9 ms,
  well above the 8.3 ms maximum delay. This holds only until the first
  no-backlog underflow, which removes the prefill (Phases 2c and 2e).
- **`-L 60` does not raise the margin at start**, as in Phase 2a. Its
  one gap (32.8 ms) came 0.3 s after start, when stress-ng started its
  four workers and the HF+ delivered no block for 47.9 ms. After that
  the backlog refill raised the fill to 34.7 ms (0.1 %), and the rest of
  the run was clean.
- Several gaps in the load runs fall in the first seconds of the run.
  The start margin (T0 in the plan) is lower than the margin after the
  first backlog refill, so the start is the weakest point of a run.

### Phase 4: what delays the decode thread on an idle system

Method [M]: one HF+ run at `-L 40` for 150 s on the idle system (no load
other than `dump1090-fa`, `fr24feed`, and this Claude Code session).
`trace-cmd record -e sched:sched_switch -e sched:sched_wakeup` recorded
120 s of it. `doc/linux-underflow-tools/wake_latency.py` measured, for
each wake-up of the
decode thread, the time until it ran, and listed the tasks that held the
CPU in between. The trace clock differs from `CLOCK_MONOTONIC` by a fixed
1.9937 s. With that offset, 1999 of 2000 sampled pulls from the T7 trace
match a decode-thread wake-up within 0.2 ms.

| Item | Value |
|---|---|
| Decode-thread wake-ups | 11251 |
| Wake-up to run, p50 / p99 / p99.9 | 0.001 / 0.003 / 0.040 ms |
| Over 1 ms | 1 (3.87 ms) |
| Task holding the CPU in that case | `HeapHelper` threads of PID 215543, which is the Claude Code process running these tests |
| Underflows, capture gaps | 1, 4.8 ms (229 frames) |

Findings:

- The run's one underflow is the 3.87 ms wake-up delay: the T7 trace
  shows a `DataBuffer` wait of 3.92 ms for that block, and the ring held
  about 4.5 ms of audio at the time.
- `dump1090-fa` did not delay the decode thread in this sample. The
  delay came from another `SCHED_OTHER` process on the same CPU. The
  `HeapHelper` thread was still runnable when it was switched out (state
  R), so the kernel preempted it 3.87 ms after the wake-up instead of at
  the wake-up. A delay under one 4 ms tick fits a preemption at the next
  tick [I]. The 4 ms delays in the 15-minute baseline are probably the
  same kind of event [I], but that run was not traced, so the task that
  caused them is not known.
- **The test harness itself is part of the "idle" load.** Claude Code
  ran on the same Pi during every test. A production Pi without an
  interactive session may see fewer of these delays.
- The idle margin differs from run to run: the 0.1 % fill was 4.6 ms in
  this run, 7.2 ms in the 15-minute baseline, and 8.6 ms in two other
  runs. It stays nearly constant within a run. The likely cause [I] is
  the phase between the HF+ block arrivals (10.667 ms) and aloop's 4 ms
  pointer updates at stream start. A run that starts with a 4.6 ms
  margin underflows at a 4 ms delay; one with 8.6 ms does not.

### Phase 2f: output through PipeWire

Setup [M]:

- PortAudio device 17, `pipewire` (the pipewire-alsa plugin), with no
  `-L` option, so the default suggested latency of 40 ms applies. The
  plugin reports a 2400-frame (50 ms) buffer to PortAudio, the same as
  the direct ALSA path at `-L 40`.
- PipeWire's default sink is `alsa_output.platform-snd_aloop.0.analog-stereo`
  on loopback card 0, opened as S32_LE with a 1024-frame period and a
  32768-frame buffer, and driven by PipeWire's timer.
- PipeWire's graph runs at 256 frames (5.33 ms) per cycle (`pw-top`).
- **PipeWire runs without real-time priority on this Pi.** Its data
  thread is `SCHED_OTHER` at nice 0. rtkit-daemon has supervised 0
  threads since boot. `/etc/security/limits.d/25-pw-rlimits.conf`
  grants `rtprio 95` to the `pipewire` group, but the user running
  PipeWire is not in that group. The plugin's own thread inside
  `airspy-fmradion` ran at `SCHED_FIFO` 30, because the test shell had
  raised `RLIMIT_RTPRIO`; without that it would depend on rtkit too.
- Captures, three kinds:
  - The first two runs recorded card 0's capture side with `arecord`,
    which interfered with PipeWire (below).
  - The `-mon` runs record the sink monitor through PipeWire with
    `pw-record` (`stream.capture.sink = true`). The monitor carries what
    PipeWire's graph sends to the sink, including the silence it
    inserts when a client runs short. A file-source run checked this
    method: exact alignment with the reference, no insertions.
  - The `-dual` runs add a capture of card 0's capture side: what a
    program reading the loopback receives. It opens after the sink is
    running, with the sink's own parameters and non-interleaved access
    (`arecord -I`). With interleaved access, `arecord` fails with an
    I/O error: snd-aloop also requires both ends to use the same access
    type.

Results [M] (HF+ at 82.5 MHz; "Loopback side" is the `-dual` capture):

| Run | Load, length | Program UF | Monitor capture | Loopback side | Sink errors | Added latency |
|---|---|---|---|---|---|---|
| `pw-idle` | idle, 10 min | 0 | not captured | not captured | none logged | none |
| `pw-idle-mon` | idle, 10 min | 1 | 2 silences: 12.8 ms at 116 s, 5.3 ms at 425 s | not captured | none logged | none |
| `pw-idle-dual` | idle, 5 min | 0 | 1 silence, 5.3 ms (one cycle), at about 16 s | the same event: silences of 5.3 and 10.7 ms and 3 short replays (2.2 ms), within 0.15 s | none logged | none |
| `pw-L1` | full CPU | hung: the sink never started (below) | | | | |
| `pw-L1-mon` | full CPU, 10 min | 1 | 1 silence, 2.1 ms at 0.7 s | not captured | 106 xruns (`pw-top`), 31 logged | 290 ms after 10 min, still rising |
| `pw-L1-dual` | full CPU, 5 min | 0 | none | 39 silences of exactly 256 frames (208 ms), 77 short replays of stale audio (57.5 ms), in 257 s | 15 logged | 200 ms after 5 min, still rising |

The sink errors are `spa.alsa: front:0p: snd_pcm_mmap_commit error:
Broken pipe` in the journal, that is, ALSA underruns of PipeWire's own
sink on the loopback card. The journal logs fewer of them than `pw-top`
counts.

Findings:

- **Under full CPU load, PipeWire itself is the main source of gaps.**
  Its graph thread misses cycles, and the sink underruns on the
  loopback card. A program reading the loopback received about 9 gaps
  a minute: one-cycle silences (5.33 ms) and short replays of audio
  from 32768 frames (one sink buffer) earlier. That is about 8 times
  the rate of the direct ALSA path under the same load (11 gaps in 10
  minutes). `airspy-fmradion` saw no underflow, and the monitor capture
  shows no gap, because the faults happen between the graph and the
  card.
- **Each sink underrun delays the program's audio instead of dropping
  it, so the latency keeps growing.** The `DataBuffer` queue grew by one
  block every 15 to 22 s, to 26 blocks (290 ms) after 10 minutes. In a
  long run it would grow until the 1024-block cap (about 11 s), after
  which `DataBuffer` drops blocks [I].
- At idle, PipeWire added two kinds of event in 10 minutes. The first
  was a 12.8 ms silence after one block was pulled 5.3 ms late. That
  silence is longer than the delay, and the plugin's buffer read
  8.6 ms just before, so the plugin's fill reading overstates the
  margin by up to about one cycle [I]. The second was one silent cycle
  (256 frames) at a moment when the program's buffer held 16 ms or
  more; the T7 trace does not explain it. In the 5-minute `-dual` run,
  one such silent cycle reached the loopback as two silences (5.3 and
  10.7 ms) and three short replays: a missed graph cycle also disturbs
  the sink.
- Counting both idle runs, PipeWire gave about 3 events in 15 minutes,
  against 1 in about 18 minutes on the direct ALSA path. Both counts are
  small.
- The fix for the load case is to give PipeWire real-time priority, for
  example by adding the user to the `pipewire` group and restarting
  the session (not tested). The production path described for these
  tests writes to the loopback directly and does not depend on this.

**A PipeWire sink that fails to start stops the program without any
message** (run `20261020-080453-2f-hf-pw-L1`) [M]:

- PipeWire logged `spa.alsa: Broken configuration: no configurations
  available` at 08:04:54, about 1 s after the program started. The sink
  node then went `suspended -> error (Start error: Operation not
  supported)`.
- The program wrote 3 blocks into the plugin's buffer: its free space
  went 1989, 1477, 965, 453 frames, with nothing consumed. The fourth
  `Pa_WriteStream()` never returned.
- For the next 590 s there was no audio and no error. `DataBuffer`
  logged one queue overflow at 11 s, and the status line never updated.
- SIGINT at 600 s printed `Stopping by getting signal: Interrupt`, but
  the program did not exit. The test harness killed it 10 s later.

The cause of the sink failure is not established. The failure came
before this run's `arecord` started, so that capture was not the direct
cause. snd-aloop makes the two ends of a cable agree on their
parameters, so a capture opened first with other parameters can block
the sink's configuration [I]. In the earlier idle run, `arecord` on that
capture side failed with an I/O error while PipeWire played normally.
That error matches the access-type mismatch found in the `-dual` runs
(interleaved capture against a non-interleaved sink). The production path
described for these tests (`-P` straight into a loopback card that a
streaming program reads) does not pass through PipeWire.

**Why the program hangs** (static reading of f236638 by the cpp-expert
agent; file and line references are in its report):

- The signal thread only sets `stop_flag`. The main thread is the decode
  and output thread, and it checks `stop_flag` only at the top of its
  loop. It was inside `Pa_WriteStream()`, so it never saw the flag, and
  the cleanup code never ran.
- There is no timeout or watchdog on a write that does not return, and
  nothing checks that the stream consumes audio after
  `Pa_StartStream()` succeeds.
- The source thread keeps pushing. `DataBuffer` is capped at 1024 blocks
  and drops the oldest block on each later push. It logs the overflow
  only once per episode, so the count of dropped blocks (about 55,000
  over 590 s [I]) was never shown. Memory stays bounded.
- The status line is printed by a separate thread, but only from data
  that the decode thread publishes after a write returns.
- Related findings from the same reading:
  - `DataBuffer::pull()` waits without checking `stop_flag`, and the
    HF+ source never ends the buffer. An HF+ that stops delivering
    would therefore also make the program ignore SIGINT.
  - When a write does return an error, the program logs it and stops,
    but `main()` exits with status 0.

## Conclusions

### Answers to the plan's questions

**Q1, stall tolerance.** For the HF+ at `-L 40` (period 480, buffer
2400 frames):

- At start, the margin before a write is 4.6 to 8.6 ms. Which value a
  run gets is decided at stream start, and it stays for the run.
- After an underflow with a backlog, the margin grows by about the
  length of the delay, and so does the latency.
- After an underflow without a backlog (drift, or an SDR-side gap), the
  margin is 0 at `-L 40` and about 8 ms at `-L 43`.
- `--portaudio-prefill 30` adds about 25 ms, until the first
  no-backlog underflow removes it.

**Q2, delays.** Push-to-write delay for the HF+ at `-L 40`:

- Idle: p99.9 0.88 ms, maximum about 5 ms. The long ones are wake-ups
  delayed by up to one 4 ms tick while another `SCHED_OTHER` task holds
  the CPU (in the traced case, the Claude Code process running the
  tests). Separately, HF+ USB blocks sometimes arrive up to 7 ms late.
- Full CPU load: maximum 9.4 ms, with 822 delays over 5 ms per hour.
- Network load (loopback TCP): maximum 15.3 ms.
- At the start of a load, the HF+ delivered no block for up to 48 ms.

**Q3, rates.** Audible gaps observed with the HF+, `-L 40` (or the
same 40 ms default through PipeWire), `SCHED_OTHER`, nice 0, unless
stated. The per-hour figures scale the observed counts to one hour, and
have wide error bars.

| Load | Gaps | Per hour | Gap length |
|---|---|---|---|
| idle (about 18 min of runs at `-L 40`) | 1 | about 3, from one event | 4.8 ms |
| L1 full CPU | 11 in 10 min | about 66 | 0.5 to 4 ms, 23.4 ms total |
| L5 network | 3 in 10 min | about 18 (2 in the first 2 s) | 0.8 to 6.7 ms |
| L4 build | 2 in 10 min | about 12 | 6.1 ms total |
| L2 memory | 1 in 10 min | about 6 | 4.8 ms |
| L3 storage | 0 in 10 min | below 6 | |
| L1 full CPU with `chrt -f 10` | 0 in 10 min | below 6 | |
| PipeWire, idle | 3 in 15 min | about 12 | 5.3 to 12.8 ms at the monitor; 18 ms in total at the loopback for the one event captured there |
| PipeWire, L1 full CPU (loopback side) | 116 in 257 s | about 1600 | 5.3 ms silences and 0.7 ms replays |

**Q4, counters.**

- `Trough=0.0ms` and `min ring fill=0.0ms` are false readings; the real
  minimum was 3.2 ms or more in clean runs.
- The `Ring=` capacity reads 46.8 ms for a 50 ms buffer.
- On aloop at `-L` 15 and above, `UF=` overcounts what a listener hears:
  52 UF for 11 gaps under full CPU load. Most UF events are an exactly
  empty ring at a pointer update, refilled before silence is played.
- At `-L` 8, `UF=` undercounts: 20 UF for 99 events, most of them
  repeats of stale audio.
- Through PipeWire, `UF=` reports only the program's own buffer. Under
  full CPU load, the program showed 0 UF while the loopback reader got
  116 gaps in 257 s from PipeWire's sink underruns.

**Q5, drift.** Yes. This HF+ runs 0.395 ppm slow against the Pi's clock.
At `-L 40` the ring empties after about 5 hours, independent of load.
After that, each missing frame becomes a one-frame click (about one
every 53 s), until a scheduling delay causes a backlog that refills the
ring. At `-L 43` the same deficit comes out as one gap of about 8 ms
every 7.5 hours.

**Q6, mitigations.**

| Mitigation | Effect measured | Cost |
|---|---|---|
| `chrt -f 10` (`SCHED_FIFO`) | Under full CPU load: 0 gaps, maximum delay 0.93 ms (from 9.4 ms) | Needs an `RLIMIT_RTPRIO` grant; a real-time thread stuck in a loop can take up to 95% of a CPU |
| `nice -n -10` | Under full CPU load: 2 gaps (from 11), maximum delay 7.3 ms | Needs `RLIMIT_NICE` or root |
| `--portaudio-prefill 30` | Under full CPU load: 0 gaps. Delays the first drift underflow from about 5 h to about 17 h | About 25 ms more latency. Lost after the first no-backlog underflow |
| `-L 43` instead of 40 | Turns the post-drift click train into one 8 ms gap per 7.5 h | Under 1 ms more latency |
| `-L 60` | No better at start than `-L 40`; one 32.8 ms gap at the start of a load | More latency only after a backlog refill |
| CPU governor `performance`, CPU pinning, `--portaudio-prefill-adapt` | Not tested | |

**Q7** belongs to the Ubuntu machine and was not tested here.

### Recommendations

These are for the user's decision; the acceptance target was left open
("decide after data").

1. **Run the decoder with `SCHED_FIFO`.** This gave the largest
   improvement measured. Without a code change: `chrt -f 10`, with an
   `RLIMIT_RTPRIO` grant (for example `CPUSchedulingPolicy=fifo` and
   `CPUSchedulingPriority=10` in a systemd unit, or an `rtprio` entry in
   `/etc/security/limits.d/`). With a code change: a Linux version of
   `--realtime` (Linux report section 6.3) could make only the decode
   and source threads real-time.
2. **Use `-L 43` or more with the HF+.** At its 512-frame blocks, the
   period (`L` × 12 frames at 48 kHz) must exceed one block. Otherwise a
   no-backlog underflow turns into a train of clicks.
3. **Drift needs a code change to remove.** No setting stops a slow
   SDR clock from emptying the ring; settings only change when and how
   the deficit comes out. Removing it needs the output rate to follow
   the ring level (closed-loop resampling). The static `-r` option
   cannot do this, because the drift changes with temperature.
4. **Protect the start.** Several gaps came in the first seconds of a
   run, where the margin is lowest. Prefill covers this until the first
   no-backlog underflow.
5. **Fix the telemetry** (Q4) before relying on field counters:
   `Trough=`, `min ring fill`, the `Ring=` capacity, and the UF meaning
   on aloop.
6. **Make a stalled output visible and recoverable** (Phase 2f). As
   described by the cpp-expert agent, not yet written:
   - A small watchdog thread checks a timestamp that the decode thread
     sets around each `Pa_WriteStream()` call. If one write takes more
     than about 3 s (longer for the first write, which includes the
     prefill), it logs the stall and calls `_exit(1)`. It must not call
     `exit()` or PortAudio, which can block on the stuck stream.
   - A grace timer of 3 to 5 s, armed when SIGINT or SIGTERM arrives,
     calls `_exit()` if the normal shutdown does not finish.
   - `main()` returns non-zero after a write error.
   - With a non-zero exit, a service manager (for example systemd
     `Restart=on-failure`) can restart the program.
7. **Keep the direct ALSA path for production.** Through PipeWire on this
   Pi, the sink itself underruns under load, the program's counters do
   not show it, and the latency grows without a limit. If PipeWire is
   needed, first give it real-time priority (Phase 2f) and repeat the
   `-dual` test.
8. Carry the corrections in section 8 of the plan into the Linux report.

### Limits of these results

- One HF+ unit, one Pi, one loopback card. Another SDR has another
  block size and drift.
- Most cells are one 10-minute run, so rates of a few per hour have
  wide error bars. No run lasted 24 hours.
- The Claude Code session that ran the tests was active on the Pi
  throughout, and it caused at least one of the idle delays.
- The CPU governor, CPU pinning, `--portaudio-prefill-adapt`, and
  `cyclictest` baselines were not tested.
- The direct ALSA runs measured the gaps at the loopback's capture side.
  The PipeWire runs measured them at the loopback only in the two
  5-minute `-dual` runs. PipeWire with real-time priority was not
  tested.
- `get_throttled` reported a soft temperature limit event during the
  `nice` variant run (Phase 2d variants). Runs after it may have run at
  a lower CPU clock for part of the time.
