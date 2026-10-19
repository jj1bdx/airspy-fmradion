# PortAudio output underflows on Linux (2026-10-19)

Companion to `doc/UNDERFLOW_RESEARCH_20261019.md` (the "macOS report"),
which covers macOS. This report covers Linux: Ubuntu 26.04 LTS x86_64
and Raspberry Pi 5 with Raspberry Pi OS 64-bit Lite (Debian Trixie).

Scope and method:

- Source tree: branch `dev-macos-realtime-thread` at `f6dbde3`. The code
  analysis was done by the cpp-expert agent, reading only. I re-checked
  the key PortAudio claims and the Ubuntu packaging claims myself.
- PortAudio source read: v19.6.0 (tag `pa_stable_v190600_20161030`,
  commit `396fe4b`), v19.7.0 (`147dd72`), and master commit `e1b70d33`
  (2026-02-06), which is the snapshot Ubuntu 26.04 packages. Paths
  marked `A/` are `src/hostapi/alsa/pa_linux_alsa.c`; line numbers are
  v19.7.0 unless stated.
- Containers on this Mac (Colima, arm64): Debian Trixie 13.7 and Ubuntu
  26.04.1, used for package versions, host API probes, and a build and
  decode check of the branch (section 2). No audio hardware, no sound
  card, and no `-P` run of airspy-fmradion.

Evidence tags:

| Tag | Meaning |
|---|---|
| [C] | This tree's code (file:line). |
| [P] | PortAudio source (version, path:line). |
| [A] | Other package source or packaging (PipeWire, Debian, Ubuntu). |
| [M] | Measured in the containers for this report. |
| [I] | Inference, or general knowledge of alsa-lib, the kernel, glibc, rtkit, or Raspberry Pi defaults, not read here. A check is given in section 9. |

## 1. Summary

1. **On ALSA the stall tolerance is small by default and does not grow
   with `-L`.** The ALSA ring is the only buffer; PortAudio adds none
   [P `A/4457-4522`]. The stream starts empty and starts playing when
   one period has been written (`start_threshold` = one period,
   e1b70d33 `A/2169`) [P]. With the default 40 ms latency the period is
   10 ms (480 frames), so playback starts after two 256-frame Airspy HF+
   blocks. The fill then moves between one and two blocks, which leaves
   about 5 ms of stall tolerance [I, derived from P and C]. On macOS the
   ring starts full of zeros and the `-L` bucket sets the margin; on
   Linux `--portaudio-prefill` is the control that sets it.
2. **`-L` maps to ALSA as** period = `L/4`, buffer = `L` + one period (5
   periods), reported `outputLatency` ≈ `L` [P `A/2255-2262`,
   `A/2329-2349`, `A/2087-2113`].
3. **The new telemetry is wrong on ALSA.** The capacity fallback
   (`outputLatency * rate`) is 80% of the real buffer, and
   `raise_capacity()` then lifts it to the largest free space seen,
   which occurs at the trough. `Trough=` therefore reads 0.0 ms, and
   `Ring=` is the fill minus the trough fill [C `sfmbase/AudioOutput.cpp`,
   P]. The fix is small (section 8).
4. **Ubuntu 26.04's PortAudio probably never reports ALSA underflows.**
   In `e1b70d33`, `PaUtil_GetTime()` uses `CLOCK_MONOTONIC`
   (`src/os/unix/pa_unix_util.c:167-168`), while the xrun age subtracts
   an ALSA trigger timestamp (`snd_pcm_status_get_trigger_htstamp`,
   `A/3234-3239`, `A/3376`) [P]. ALSA timestamps default to the realtime
   clock [I], so the difference is hugely negative, and the report test
   `underrun > 0.0` (`A/4414`) fails. v19.6.0 and v19.7.0 use
   `CLOCK_REALTIME` for both [P]. If this holds, `UF=` stays 0 on Ubuntu
   26.04 even when gaps are heard. Section 9 has the test (U3).
5. **On Ubuntu 26.04, `-P` fails outright without a PulseAudio server.**
   Ubuntu builds PortAudio with the PulseAudio host API [A, M]. When no
   PulseAudio-compatible server is reachable, its initializer returns an
   error and `Pa_Initialize()` aborts for every host API
   (`src/common/pa_front.c` `InitializeHostApis`) [P]. This was measured
   in the container: `-9999 Unanticipated host error` without a server,
   success with a PulseAudio 17.0 null sink [M]. It is PortAudio issue
   #900 (open). Ubuntu's devel series fixed it in `0ubuntu2`
   (2026-10-08, `pulseaudio-no-server-not-fatal.patch`); 26.04 has
   `0ubuntu1` [A]. It affects headless use, ssh sessions without
   `XDG_RUNTIME_DIR`, and system services. Raspberry Pi OS (Debian
   PortAudio 19.6.0, no PulseAudio code) is not affected.
6. **The tree sets no Linux scheduling.** All platform code is
   `__APPLE__`; `--realtime` prints "supported on macOS only; ignored" on
   Linux [C, M]. In blocking ALSA mode there is no PortAudio audio
   thread: the decode thread does the ALSA `poll()`, copy, and commit
   itself [P]. A desktop under browser or compositor load competes with
   it on equal terms (`SCHED_OTHER`).
7. **The branch builds and decodes correctly on both distributions** [M]:
   no warnings with gcc 14.2 (Debian) and gcc 15.2 (Ubuntu), and file
   decodes of `9570a05`, `f6dbde3`, and `f6dbde3 --realtime` are
   byte-identical (section 2).

Most useful actions, in order:

1. No code: add `--portaudio-prefill 30` (for example
   `-P - -L 40 --portaudio-prefill 30 -v`). Without it the cushion is
   about 5 ms.
2. No code: `chrt -f 10` (with `rtprio` granted through `limits.d`), the
   `performance` CPU governor, and on the Pi 5 the SDR and the DAC on
   different USB controllers.
3. Code: fix the ALSA capacity telemetry, add underflow detection that
   does not depend on PortAudio's flag, and add a Linux branch to
   `--realtime` (`SCHED_FIFO` at a low priority).

## 2. Build and decode check in containers

Images: `debian:trixie` and `ubuntu:26.04` with the README.md build
dependencies. A small C program (`pa_hostapis.c`) listed the PortAudio
host APIs. The container files and logs were not kept. The repository was
mounted read-only; each
commit was cloned and built inside the container, with
`r8brain-free-src` copied in.

Package versions [M]:

| | Debian Trixie 13.7 (base of Raspberry Pi OS) | Ubuntu 26.04.1 LTS |
|---|---|---|
| PortAudio | 19.6.0-1.2+b3 ("V19.6.0-devel, revision 396fe4b6", the 19.6.0 release commit) | 19.7.0+git20260206.e1b70d33-0ubuntu1 ("V19.7.0-devel, revision e1b70d33") |
| Host APIs compiled in | ALSA, OSS, JACK | ALSA, OSS, JACK, PulseAudio |
| gcc | 14.2.0 | 15.2.0 |
| VOLK | 3.2.0 | 3.3.0 |
| alsa-lib | 1.2.14 | 1.2.15.3 |
| libairspy / libairspyhf / librtlsdr | 1.0.10 / 1.6.8 / 2.0.2 | 1.0.10 / 1.6.8 / 2.0.2 |
| PipeWire ALSA plugin | not installed | pipewire-alsa 1.6.2 available |

Results [M]:

- Both `9570a05` and `f6dbde3` build with no warnings on both images.
  CMake found VOLK, libsndfile 1.2.2 with MP3 support, and PortAudio.
- Three real-time decodes of `test-files/piano_iqtest.wav`
  (`-m fm -t filesource -c filename=... -R <out>.raw`) on each image:
  `9570a05 -q`, `f6dbde3 -q`, and `f6dbde3 -v --realtime`. All outputs
  are 3839596 bytes and `cmp` reports them identical.
- `--realtime` prints `WARNING: --realtime is supported on macOS only;
  ignored` and the run continues. The `-v` status line works for a file
  output (`Q=`, `DBlk=`, `SDRdrop=`, no `UF=`).
- Host API probe (`pa_hostapis.c`, no sound card):
  - Debian: 0 ALSA, 1 OSS; default host API 0 (ALSA). JACK did not
    appear without a JACK server and did not stop initialization.
  - Ubuntu, no server running: `Pa_Initialize()` returned `-9999
    Unanticipated host error`.
  - Ubuntu, PulseAudio 17.0 with a null sink as the default: 0 ALSA (2
    devices), 1 OSS, 2 PulseAudio (4 devices); default host API 0
    (ALSA).

Not checked: Ubuntu x86_64 itself (the containers are arm64; the package
versions are the same), the Raspberry Pi archive's own packages (the
container is plain Debian), and anything with `-P`.

## 3. PortAudio ALSA blocking output

### 3.1 The write

`WriteStream` (`A/4457-4522`) [P]:

1. At entry, if the latched `underrun` value is above zero, return
   `paOutputUnderflowed` at the end of this call and clear the latch
   (`A/4473-4477`).
2. Loop until all frames are written: `PaAlsaStream_WaitForFrames()`
   first calls `snd_pcm_avail_update()` and returns at once if space is
   free (`A/3772-3790`); otherwise it `poll()`s the PCM descriptors with
   a timeout of one period (`A/2220-2225`, `A/2764`). It then copies into
   the mmap area and commits (`snd_pcm_mmap_commit()`, or
   `snd_pcm_writei()` for non-mmap devices, `A/3466-3500`).
3. If the PCM is `PREPARED` and at least one period is queued, call
   `snd_pcm_start()` (`A/4503-4514`).

The writer sleeps in the kernel until a period is free. This is unlike
CoreAudio's 5 ms `Pa_Sleep()` poll. `Pa_WriteStream()` has no timeout:
after 2048 empty polls it tries recovery, and on a hung or suspended
device it keeps looping, so the decode thread can block forever
(`A/3857-3880`) [P]. As on macOS, SIGTERM only sets the stop flag, so a
process stuck there does not stop on SIGTERM [C, I].

### 3.2 What an underrun does

- The kernel puts the PCM into `XRUN` only when the ring is completely
  empty (`stop_threshold` = buffer size, `A/2121`). With
  `silence_threshold = 0` and `silence_size = boundary` (`A/2124-2130`),
  the unwritten part of an mmap buffer is kept silent on hardware PCMs,
  so a late writer produces silence, not repeated old data [P, I].
- PortAudio's `PaAlsaStream_HandleXrun()` (`A/3263-3320`) runs when
  `poll()` reports an error, when `avail_update()` returns `-EPIPE`, or
  inside `Pa_GetStreamWriteAvailable()` (`A/4554-4566`), so the
  program's own telemetry call can trigger the recovery. It records the
  xrun age, then for mmap devices calls `snd_pcm_drop()` and
  `snd_pcm_prepare()` (`A/3236-3246`, `A/2912-2935`). For non-mmap
  devices it calls `snd_pcm_recover()`.
- After recovery the ring is empty and the PCM restarts once the writer
  has queued one period again. The listener hears a hard cut to silence
  for the stall plus about one period, then a hard restart. There is no
  fade.
- The latch is a single value [P `A/3279`]. Several xruns between two
  writes count as one; an event is reported by the next write; the last
  event before the stream stops is never reported. This matches macOS.
  ALSA has no equivalent of CoreAudio's HAL overload, so on ALSA `UF=`
  counts only ring shortfall.

### 3.3 The clock problem in `e1b70d33` (Ubuntu 26.04)

Read from the source [P]:

- `PaUtil_GetTime()` uses `CLOCK_MONOTONIC` in `e1b70d33`
  (`pa_unix_util.c:161-168`) and `CLOCK_REALTIME` in v19.7.0
  (`pa_unix_util.c:161-163`).
- `HandleXrun` sets `self->underrun = (now - StatusToTime(st, 1, NULL))
  * 1000` (`A/3376`), and `StatusToTime()` reads
  `snd_pcm_status_get_trigger_htstamp()` (`A/3234-3239`). PortAudio sets
  no ALSA timestamp type.
- The report test is `stream->underrun > 0.0` (`A/4414`).

Inference [I]: alsa-lib's default timestamp type is the realtime clock.
"Monotonic now minus realtime trigger time" is then about −56 years in
milliseconds, the test fails, and `paOutputUnderflowed` is never
returned. A PCM plugin that leaves the trigger timestamp at zero would
give a positive value and still report. So the effect may differ
between `hw:`, `dmix`, and the PipeWire plugin. The same latch feeds the
callback status flags, so callback mode would be affected too. Debian's
19.6.0 (and so Raspberry Pi OS) is consistent and reports normally.

### 3.4 No application ring

In blocking ALSA mode the buffer processor only converts the sample
format. The only queue is the ALSA ring plus whatever the PCM plugin
adds (dmix, PipeWire) [P].

## 4. Latency mapping and what the telemetry shows

### 4.1 `-L` to ALSA sizes

The program passes `framesPerBuffer = paFramesPerBufferUnspecified`
[C]. PortAudio then sets period = `L*rate/4` (4 periods by default,
`A/589`) and buffer = `L*rate` + period (`A/2087`), each rounded to what
the device accepts; `outputLatency` = (buffer − period)/rate [P].

At 48 kHz, if the device grants the request:

| `-L` (ms) | Period (frames / ms) | Buffer (frames / ms) | `outputLatency` | Current capacity estimate (frames) |
|---|---|---|---|---|
| 8 | 96 / 2.0 | 480 / 10 | 8 ms | 384 (80%) |
| 15 | 180 / 3.75 | 900 / 18.75 | 15 ms | 720 (80%) |
| 40 (default floor) | 480 / 10 | 2400 / 50 | 40 ms | 1920 (80%) |
| 100 | 1200 / 25 | 6000 / 125 | 100 ms | 4800 (80%) |

Environment variables PortAudio's ALSA code reads [P]:

- `PA_ALSA_PERIODSIZE` sets the period in frames. It is honored only
  when `framesPerBuffer` is unspecified, which is this program's case.
- `PA_ALSA_PLUGHW` uses `plughw:` instead of `hw:`.
- `PA_ALSA_INITIALIZE_BLOCK`.
- `PA_ALSA_IGNORE_ALL_PLUGINS` (19.7.0 and later).

The period count changes only through `PaAlsa_SetNumPeriods()`, which
this program does not call.

### 4.2 Fill and stall tolerance

[I], derived from [P] and [C], for the Airspy HF+ (256 audio frames
every 5.33 ms):

- With a period larger than one block (`-L` above about 21 ms), playback
  starts after two blocks with 512 frames (10.7 ms) queued. The fill
  then moves between 256 and 512 frames, so a stall of more than about
  5 ms underflows.
- With a period of one block or less (`-L` up to about 21 ms), playback
  starts after the first block and runs dry before the second arrives.
  This is reported on the second write and counted as a normal `UF`,
  not as a start-up underflow.
- With `--portaudio-prefill P`: the fill starts at P + one block, and
  the tolerance is about P + 5 ms, up to the buffer size. `prime()` runs
  at the first block, not at stream start, and the ALSA stream does not
  drain before it starts, so the prefill is fully effective
  (`AudioOutput.cpp:360-369`) [C, P].
- After an underrun, the backlog in `DataBuffer` is decoded faster than
  real time and refills the ring to min(buffer, stall length). A few
  `UF` events early in a session, followed by quiet, fit this. The
  audio is then later by the gap length, as on macOS.
- End-to-end delay on ALSA is the fill plus device and plugin latency,
  not `-L`. The README latency figures are macOS measurements.

### 4.3 The telemetry on ALSA

From [C] `sfmbase/AudioOutput.cpp` and [P]:

- The capacity estimate on Linux is `outputLatency * rate`, which is the
  buffer minus one period: 80% of the buffer.
- `raise_capacity()` lifts the estimate to any larger free space seen.
  The pre-write free space is buffer − fill, and its maximum is
  buffer − minimum fill. Since the minimum fill is small on ALSA, the
  estimate becomes buffer − minimum fill.
- `Trough=` = capacity − maximum pre-write free space, which then reads
  0.0 ms: the quantity of interest is hidden. `Ring=` values are all
  reduced by the true minimum fill.
- The `prime()` clamp uses the 80% estimate (prime runs before any
  raise): at `-L 40` the limit is 34.7 ms instead of 44.7 ms. It is
  conservative by one period, and its warning states the wrong
  capacity.
- `adapt()` has the same flaw as on macOS (macOS report section 3.3):
  10 ms of silence right after the restart, on top of the backlog
  refill.
- `IOBuf=` and the device-buffer warning are macOS only and absent on
  Linux.
- Resolution: `snd_pcm_avail_update()` follows the hardware pointer,
  which advances per period interrupt or per USB packet on hardware;
  through `dmix` per dmix period; through the PipeWire plugin only in
  the PipeWire process callback, once per server quantum [I, A].
- On the PulseAudio host API, `Pa_GetStreamWriteAvailable()` returns an
  error, so `Ring=` is not shown. The API counts underflows internally
  but never reports them, so `Pa_WriteStream()` never returns
  `paOutputUnderflowed` and `UF=` stays 0 (`pa_linux_pulseaudio.c:530-554`,
  field never read). The JACK blocking write also never reports
  underflows (`pa_jack.c`) [P].

## 5. Audio stacks on the two systems

### 5.1 Ubuntu 26.04 desktop

- Host API order in `e1b70d33` is ALSA, OSS, JACK, PulseAudio; the
  default host API is ALSA (`pa_unix_hostapis.c`, `pa_front.c`) [P, M].
  `-P -` opens the ALSA "default" device. On a desktop with
  `pipewire-alsa`, that device is the PipeWire ALSA plugin [A, I].
  `-P <index>` can select a PulseAudio host API device explicitly; ALSA
  devices come first in the index order.
- The PipeWire ALSA plugin (`pcm_pipewire.c`, PipeWire 1.6.2) [A]:
  - It runs a `pw_stream` whose process callback runs on a PipeWire
    data-loop thread inside this process. libpipewire asks for real-time
    scheduling for that thread [I].
  - On prepare it sets `node.latency` to PortAudio's period (`:545`,
    `:822-827`), so this client requests a quantum of `L/4` (10 ms at the
    default, 2 ms at `-L 8`). That rewrite overrides `PIPEWIRE_ALSA`
    settings and probably `PIPEWIRE_LATENCY` [I].
  - The graph runs at the smallest quantum any client requests, within
    the server limits. Other clients, such as a browser, change it [I].
  - When the ring is short while running, the plugin fills the PipeWire
    buffer with silence and reports `-EPIPE` to PortAudio
    (`:316-330`, `:181-183`), which then does its drop and restart.
  - Practical controls: `-L` or `PA_ALSA_PERIODSIZE` on the client side;
    on the server side `pw-metadata -n settings 0 clock.force-quantum
    <n>`. `pw-top` shows the quantum, timing, and an error (xrun) count
    per client.
- Through the PulseAudio host API, `suggestedLatency` becomes the Pulse
  `tlength`, and the blocking write is a `usleep(100)` poll. It loses
  `UF=` and `Ring=` (section 4.3). Use it only to compare what is heard.

### 5.2 Raspberry Pi OS Lite (Trixie)

- Debian's PortAudio 19.6.0 (ALSA and JACK, no PulseAudio) unless the
  Raspberry Pi archive overrides it [A, M].
- Lite images likely run no sound server by default [I]. The ALSA
  "default" device is then the alsa-lib chain for card 0, which can
  include `plug` and `dmix`. With `dmix`, the period and buffer are set
  by dmix (in stock alsa-lib about 1024 and 4096 frames), so `-L` may not
  change them [I]. `aplay -L` and `aplay -D default --dump-hw-params`
  show which applies.
- `hw:` or `plughw:` devices give the most predictable result: no extra
  layer, period and buffer follow `-L`. The cost is exclusive access (no
  other program can play) and no shared mixer. `plughw:` converts the
  format and rate when the DAC needs it; `PA_ALSA_PLUGHW=1` switches
  PortAudio's `hw:` entries to `plughw:`.
- The Pi 5 has no analog output, so audio goes through HDMI or a USB
  DAC.

## 6. Scheduling on Linux

### 6.1 Threads

| Thread | Work |
|---|---|
| main / decode | `pull()`, decode, `Pa_WriteStream()` including the ALSA `poll()`, copy, and commit |
| signal | `sigwait()` only |
| StatusPrinter | prints; with `-v` takes the `DataBuffer` mutex |
| source `std::thread` | HF+ and R2: start the library, then sleep; RTL-SDR: runs the libusb event loop and the callback |
| libairspyhf / libairspy threads | call `rx_callback` and allocate the IQ vector |
| libusb event thread | USB completions |
| PipeWire data-loop thread | only with the PipeWire ALSA plugin; real-time [I] |

All of these run at `SCHED_OTHER` today [C]. Threads inherit the
creator's policy and priority by default, so a policy set early in
`main()`, before the source starts, also covers the library threads.
The macOS `--realtime` is applied after the source starts
(`main.cpp:1060`), so on macOS it covers the decode thread only.

### 6.2 Options without code changes

| Option | Notes |
|---|---|
| `chrt -f 10 airspy-fmradion ...` | All threads become `SCHED_FIFO` 10 and preempt all normal load. Needs root, or `rtprio` granted in `/etc/security/limits.d/` (for example `@audio - rtprio 20`, effective at the next login; check `ulimit -r`). For a systemd service: `LimitRTPRIO=`. |
| `nice -n -10` | Only changes the `SCHED_OTHER` weight; cannot preempt the way RT can. A cheap first test. |
| CPU governor `performance` | Removes frequency ramp delays: `cpupower frequency-set -g performance`, or write to `/sys/devices/system/cpu/cpu*/cpufreq/scaling_governor`. |
| `taskset` / `isolcpus` | Pin the decode and source threads to their own cores (Pi: `isolcpus=` in `/boot/firmware/cmdline.txt`). High effort; last. |
| PREEMPT_RT kernel | In mainline since Linux 6.12. Not the default on Ubuntu 26.04 or Raspberry Pi OS [I]. On an RT kernel the threaded IRQs run at `SCHED_FIFO` 50, so keep this program below that. |

Safety [I]: `kernel.sched_rt_runtime_us` (950000 of 1000000 by default)
limits all real-time threads to 95% of CPU time, so a runaway
`SCHED_FIFO` thread cannot lock up the machine completely. The decode
loop blocks in `DataBuffer::pull()` every block and in `poll()` when the
ring is full, so the risk is low unless `-E` makes a block take longer
than its period.

### 6.3 A Linux `--realtime` (sketch)

Add a `#elif defined(__linux__)` branch to `sfmbase/RealtimeThread.cpp`;
the per-block `begin_cycle()`/`end_cycle()` stay no-ops, since Linux has
no workgroups:

```cpp
bool RealtimeThread::start() {
  // The kernel signals the process if this thread runs 200 ms
  // without blocking; a decode block takes a few ms.
  rlimit rt{200000, 200000}; // RLIMIT_RTTIME, microseconds
  setrlimit(RLIMIT_RTTIME, &rt);
  sched_param sp{};
  sp.sched_priority = 10;
  const int err = pthread_setschedparam(pthread_self(), SCHED_FIFO, &sp);
  if (err != 0) {
    fmt::println(stderr, "WARNING: --realtime: SCHED_FIFO failed ({}); "
                         "see limits.d rtprio or chrt", strerror(err));
    return false;
  }
  return true;
}
```

- `pthread_setschedparam()` returns the error number; `EPERM` is the
  expected failure without privileges.
- To cover the library threads, call it early in `main()`, before the
  source starts. That means creating it before the block period is
  known, which is fine on Linux because `SCHED_FIFO` takes no period.
- Choose the priority from what runs (`ps -eLo pid,tid,cls,rtprio,comm`),
  below the PipeWire data thread (typically 20 with rtkit) and below IRQ
  threads on RT kernels.
- rtkit (D-Bus) is the alternative for desktop sessions without
  `limits.d` changes, but it adds a D-Bus dependency and caps the
  priority [I]. It is not worth it here.

`PaAlsa_EnableRealtimeScheduling()` does not help: it applies only to
callback streams (PortAudio's own thread, `SCHED_FIFO` priority 1) and
is never read for blocking streams (`A/2998-3002`, `A/4584-4588`,
`pa_unix_util.c:245-266`) [P].

## 7. Load-related causes on Linux

1. **Desktop load (Ubuntu)** [I]: the compositor and browser GPU
   processes compete for CPU time and memory bandwidth. With no QoS
   classes, a `SCHED_OTHER` decode thread competes on equal terms under
   the EEVDF scheduler. The PipeWire data thread is real-time, so the
   server side is protected and this program is not.
2. **USB on the Pi 5** [I]: the RP1 chip has two xHCI controllers, each
   serving one USB 3.0 and one USB 2.0 port. An SDR and a USB DAC on the
   same controller share its scheduling and interrupts. `lsusb -t`
   shows the bus layout. HDMI audio avoids a second USB device.
3. **Thermal and supply throttling on the Pi 5** [I]: check
   `vcgencmd get_throttled` (expect `0x0`), `vcgencmd measure_temp`, and
   `vcgencmd measure_clock arm`.
4. **Memory pressure**: swap on an SD card is slow (`free -m`, `vmstat
   1`).
5. **Allocations with glibc malloc** [I]: per-block sizes are as in the
   macOS report section 4.5 (HF+ 16 KB; RTL-SDR 128 KB plus two 128 KB
   temporaries; R2 512 KB plus two 512 KB temporaries). glibc's
   `mmap` threshold starts at 128 KiB but rises automatically after the
   first free of a larger mmapped chunk, so steady per-block
   `mmap`/`munmap` is less likely than it looks. Heap trimming can still
   return pages that then fault again. The library thread allocates and
   the main thread frees, which crosses malloc arenas. Measure first:
   `strace -f -c -e trace=mmap,munmap,brk,madvise -p <pid>`. If there is
   churn, the no-code fix is
   `GLIBC_TUNABLES=glibc.malloc.mmap_threshold=67108864:glibc.malloc.trim_threshold=268435456:glibc.malloc.arena_max=1`.
   An explicit `mallopt()` turns off the automatic adjustment, so any
   value set in code must be large.
6. **Airspy R2 on the Pi 5**: the R2 path (10 Msps input, about 1.5 MB of
   heap per block) runs on much slower cores than the M2. Per-block CPU
   time should be measured before relying on it.

## 8. Mitigations, ranked

Without code changes:

| Rank | Action | Benefit / cost |
|---|---|---|
| 1 | `--portaudio-prefill 30` (for example `-L 40 --portaudio-prefill 30`; limit about 34 ms at `-L 40` because of the 80% estimate; `-L 60 --portaudio-prefill 45` for more) | Raises the cushion from about 5 ms to about 35 ms; adds the prefill to the delay |
| 2 | `chrt -f 10` with `rtprio` from `limits.d` | Decode and USB threads preempt all normal load; needs a one-time privilege setup |
| 3 | `plughw:`/`hw:` device when nothing else needs the card; `performance` governor | Removes PipeWire or dmix layers; exclusive access |
| 4 | Pi 5: SDR and DAC on different USB controllers (`lsusb -t`), or HDMI audio | Removes shared-controller delays |
| 5 | Avoid `-L` at or below about 21 ms with the HF+ (period no larger than one block) | Avoids the underflow right after start |
| 6 | PipeWire: watch `pw-top` errors and quantum; test `clock.force-quantum 256`; close other audio clients | Confirms or changes the server quantum |
| 7 | `GLIBC_TUNABLES` (section 7), for the R2 and RTL-SDR only, after `strace` shows churn | Removes possible allocation page faults |
| 8 | Pi 5: active cooler and a 5 V / 5 A supply | Removes throttling |
| 9 | `taskset` / `isolcpus` | Last; high effort |

Ubuntu 26.04 only, for the `Pa_Initialize()` failure: run in the user's
login session (so `XDG_RUNTIME_DIR` and the PulseAudio socket exist),
set `PULSE_SERVER=unix:/run/user/<uid>/pulse/native` for a session
started elsewhere, keep a user server running (`loginctl enable-linger
<user>` and `systemctl --user start pipewire-pulse`), or wait for the
`0ubuntu2` fix to reach 26.04. Choosing an `hw:` device does not help,
because the failure happens before a device is chosen.

Code changes:

| Rank | Change | Benefit / cost |
|---|---|---|
| 1 | ALSA telemetry: when the host API is `paALSA`, set the capacity from `Pa_GetStreamWriteAvailable()` right after `Pa_StartStream()` (the PCM is prepared and empty, so this is the buffer size); skip `Ring=` on PulseAudio and JACK; print the host API name and the granted `outputLatency` at start-up | Correct `Ring=`/`Trough=` and prefill clamp; small, portable |
| 2 | Underflow detection that does not rely on PortAudio's flag: count a write whose pre-write free space equals the capacity (the ring was empty) | Works around the `e1b70d33` clock problem and the PulseAudio/JACK APIs that never report; small |
| 3 | Linux branch of `--realtime` (section 6.3), applied before the source starts | Targets the main cause on a loaded desktop; about 30 lines; needs privileges |
| 4 | A default ALSA prefill when `-L` is absent (for example 30 ms) | Fixes the 5 ms default cushion; changes the default delay, so decide after a field test |
| 5 | Clearer error for the Ubuntu `Pa_Initialize()` failure, naming the PulseAudio server and `XDG_RUNTIME_DIR` | The failure itself cannot be avoided in this tree |
| 6 | `mallopt()` at start-up or buffer pools | Only if `strace` shows churn |
| 7 | Callback output with an application ring (macOS report section 8) | Fades and exact counts only; on ALSA the cushion is already independent of `-L` through prefill, and the `e1b70d33` clock problem affects callback flags too |

What carries over from the macOS report, and what does not:

- Carries over: decode-thread wake-up latency as the main cause,
  allocation sizes, SDR-side loss (`SDRdrop`), the `adapt()` flaw, the
  backlog refill after a stall, and no timeout in `Pa_WriteStream()`.
- Does not carry over: the power-of-two ring table, the zero-filled
  start ring, HAL overload counting, QoS classes, workgroups, the 5 ms
  poll, and `IOBuf=`.
- New on Linux: the empty start with a 5 ms cushion, the `e1b70d33`
  clock problem, the plugin layers (dmix, PipeWire, PulseAudio), and the
  Ubuntu `Pa_Initialize()` failure.

## 9. Verification plan

On the Raspberry Pi 5 and the Ubuntu machine:

1. Record the setup: `uname -a; uname -v; ulimit -r -l -e;
   cat /proc/sys/kernel/sched_rt_runtime_us;
   cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor; aplay -l;
   aplay -L | head -40; dpkg -l | grep -i portaudio`. On the Pi, also
   `apt-cache policy libportaudio2` to see whether the Raspberry Pi
   archive overrides Debian's package.
2. Run `airspy-fmradion -P - -v -L 40` and note the latency line, `UF=`,
   `Ring=`, `Trough=`, and `Q=`. Expect `Trough=0.0ms` from the
   telemetry bias (section 4.3).
3. Prefill comparison on the same station, 10 minutes each, idle and
   under load: `-L 40`, then `-L 40 --portaudio-prefill 30`.
4. Load: `stress-ng --cpu 4 --vm 2 --vm-bytes 75% --timeout 60s`; on
   Ubuntu a browser with video and WebGL; on the Pi a USB storage copy
   on the same controller as the SDR.
5. Scheduling comparison under the same load: normal, `nice -n -10`,
   `chrt -f 10`, and `chrt -f 10` with `taskset -c 2,3`. Record `UF=` and
   `SDRdrop`. `sudo perf sched record -- sleep 60` followed by
   `perf sched latency` gives the decode thread's maximum wake-up delay.
6. PipeWire (Ubuntu): `pw-top` during playback (quantum, error count)
   and `pw-metadata -n settings`.
7. Allocation (R2 or RTL-SDR): `strace -f -c -e
   trace=mmap,munmap,brk,madvise -p <pid>` and `perf stat -e
   page-faults -p <pid> -- sleep 30`, then the same with
   `GLIBC_TUNABLES`.
8. Pi health: `watch -n1 'vcgencmd measure_temp; vcgencmd measure_clock
   arm; vcgencmd get_throttled'`.
9. **U3, the Ubuntu 26.04 clock problem:** force a stall during
   playback with `kill -STOP <pid>; sleep 0.3; kill -CONT <pid>`. If the
   gap is heard but `UF=` stays 0 on Ubuntu 26.04, while the same test
   increments `UF=` on the Pi (19.6.0), section 3.3 is confirmed. Run it
   both with `-P -` (through PipeWire) and with an `hw:` device index,
   since the result may depend on the PCM.

On this Mac in a Linux container (software only; not done yet):

- With a PulseAudio or PipeWire null sink in the container, a small
  PortAudio test program (or airspy-fmradion `-P` with a file source)
  can check the host API paths, the granted latency, the latch behavior
  on a `SIGSTOP` stall, and the `Ring=`/`Trough=` bias. The null sink
  runs on the VM's timer, so latency and load results there are not
  meaningful. This runs `-P` inside the container only, never on the
  Mac's audio devices.
- Only real hardware can check USB behavior and RP1 controller sharing,
  thermal throttling, scheduling delays under desktop load, DAC buffer
  limits, and the PipeWire quantum with a hardware sink.

## 10. Open questions

1. Does the `e1b70d33` clock mismatch suppress `paOutputUnderflowed`
   with a hardware PCM, and with the PipeWire plugin (U3)?
2. What period and buffer does the Pi's "default" device grant (dmix or
   `hw:`), and what `outputLatency` does each system report?
3. Does the Raspberry Pi archive override Debian's `portaudio19`?
4. What are the actual defaults on both machines for `ulimit -r`,
   kernel preemption, and the CPU governor?
5. What is the decode thread's wake-up delay under load on each machine
   (`perf sched latency`)?
6. Will Ubuntu backport the `0ubuntu2` PulseAudio fix to 26.04?

## 11. Sources

- PortAudio: https://github.com/PortAudio/portaudio — tags
  `pa_stable_v190600_20161030` (`396fe4b`), `v19.7.0`, and commit
  `e1b70d33`; files `src/hostapi/alsa/pa_linux_alsa.c`,
  `src/os/unix/pa_unix_util.c`, `src/os/unix/pa_unix_hostapis.c`,
  `src/common/pa_front.c`, `src/common/pa_stream.c`,
  `src/hostapi/jack/pa_jack.c`, `src/hostapi/pulseaudio/*.c`,
  `include/pa_linux_alsa.h`. The PulseAudio host API was added in commit
  `0dce0e9` (2023-09-30, PR #336) and is not in v19.7.0.
- PortAudio issue #900, "CoreAudio + PulseAudio support => no devices at
  all if the PulseAudio daemon isn't running" (open since 2024-04-28):
  https://github.com/PortAudio/portaudio/issues/900
- Ubuntu `portaudio19` packaging, changelog entry for
  `19.7.0+git20260206.e1b70d33-0ubuntu2` (2026-10-08):
  https://git.launchpad.net/ubuntu/+source/portaudio19/plain/debian/changelog?h=ubuntu/devel
- Debian `portaudio19` packaging: https://sources.debian.org/src/portaudio19/
- PipeWire 1.6.2, `pipewire-alsa/alsa-plugins/pcm_pipewire.c`:
  https://gitlab.freedesktop.org/pipewire/pipewire
- This tree: `main.cpp`, `sfmbase/AudioOutput.cpp`,
  `include/AudioOutput.h`, `sfmbase/RealtimeThread.cpp`,
  `sfmbase/StatusPrinter.cpp`, `CHANGES.md`, `README.md`,
  `doc/UNDERFLOW_RESEARCH_20261019.md`.
