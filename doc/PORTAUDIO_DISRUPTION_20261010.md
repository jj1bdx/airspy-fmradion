# PortAudio output disruption under host load on macOS (2026-10-10)

Review of why `-P` playback of a live FM broadcast crackles or drops out on
macOS when a large CPU-intensive process (a web browser) becomes active, what
can be done about it without code changes, and which source changes are worth
making. Source tree: branch `dev` at 3585c97. No source file was modified.

Evidence classes used throughout:

| Tag | Meaning |
|---|---|
| **[C]** | Read directly from the code in this tree (file:line given). |
| **[D]** | Documented in an earlier measurement in `doc/` (cited). Not re-measured here. |
| **[M]** | Measured in this review (section 9). |
| **[I]** | Inference or recollection of third-party behavior (PortAudio, CoreAudio, XNU). Not verified against source or a trace here; a verification step is given. |

## 1. Summary

1. **The decode thread is the only non-real-time link in the chain that has a
   deadline.** CoreAudio's IO thread (real time, owned by the HAL) drains
   PortAudio's internal ring; the program's single decode thread must refill
   it by blocking `Pa_WriteStream()` calls (`sfmbase/AudioOutput.cpp:305`). If
   that thread is not scheduled, or is blocked on something, for longer than
   the audio sitting in the ring, the output underruns. Nothing else in the
   program smooths this: no output thread, no jitter buffer, no priority or
   QoS request anywhere in the tree (a grep for `qos`, `thread_policy`,
   `taskpolicy` finds nothing) **[C]**.
2. **Underruns are invisible.** `paOutputUnderflowed` is swallowed as "benign"
   (`AudioOutput.cpp:309-311`) with no counter and no log **[C]**. The project
   cannot currently say whether a crackle was an output underrun, SDR sample
   loss, or something downstream of PortAudio. This is the biggest practical
   gap and the cheapest to close.
3. **The decoder has large CPU headroom, so the problem is scheduling
   latency, not throughput.** Plain FM at 384 kHz uses 3.3 % of one core and
   `-E 100` uses 8.2 % (Apple M2 Pro, FileSource, `-q`, 20 s) **[M]**. Block
   periods are 5.3 ms (HF+, 2048-sample blocks) to 14 ms (RTL-SDR). A thread
   this idle is a typical victim of being parked on an efficiency core,
   clock-limited, or preempted for tens of milliseconds **[I]**.
4. **Shallow `-L` settings remove the safety margin.** `-L 5` to `-L 7` land
   in the 1024-frame CoreAudio bucket, where the ring runs at or near empty
   (`LATENCY_CHECK_20260719.md` section 12) and real gaps were heard under
   elevated load (section 13.1) **[D]**. The 40 ms default and `-L 10` have
   more room, but it is not guaranteed: on a live SDR the deep buffers never
   fill (`LATENCY_MEASUREMENT_20260725.md` section 3) **[D]**, so the margin
   actually delivered is whatever the start-up history left.
5. **A stalled terminal can stall the decoder.** Status lines go to `stderr`
   from the decode thread (`main.cpp:1083-1109`). With the pty not drained,
   the decode thread blocked about 1.5 s after the reader stopped and caught
   up in a burst afterwards **[M]**. This needs a terminal stall of more than
   a second, so it is a less likely cause of short crackles, but it costs
   nothing to rule out (`-q`).
6. **A stall is never repaid.** After a stall the backlog drains from
   `DataBuffer` as fast as the CPU allows, but the DAC consumes at 1x, so
   the queued blocks stay queued: the audio stays late by the stall length
   (minus the ring depth) until a faster-than-DAC SDR clock or the 1024-block
   drop-oldest cap removes it **[C + I]** (section 5).

Recommended order (details in section 7):

| Rank | Proposal | Cost | Why |
|---|---|---|---|
| 1 | P1: count and report underruns; add fill-level and SDR-drop telemetry | small | Turns every other question into a measurement; no behavior change |
| 2 | P2: raise thread QoS on macOS (decode thread; source threads by inheritance) | small | One call; targets the E-core / low-QoS hypothesis directly |
| 3 | P3: move status printing off the decode thread | small | Removes a hard block from the audio path; failure mode measured |
| 4 | P4: explicit priming of the ring to a target fill | medium | Makes the margin deterministic instead of start-up luck |
| 5 | P5: backlog trimming after a stall | medium | Stops the latency ratchet |
| 6 | P6: recycle per-block vectors | small to medium | Removes malloc/page-fault noise; low expected gain |
| 7 | P7: time-constraint policy / audio workgroup | medium to large | Strongest scheduling class, but fits this thread poorly |
| 8 | P8: PortAudio callback + lock-free ring | large | Does not address the stall; history says no |
| rejected | `mlock`/`mlockall`, `-ffast-math` | | section 7.1 |

## 2. Producer to consumer path and threading model

```
 SDR hardware --USB--> libairspy / libairspyhf / librtlsdr thread(s)
                           |  rx_callback -> callback(): convert to IQSampleVector
                           v
                    DataBuffer<IQSample>   (mutex + condvar, std::queue, cap 1024 blocks)
                           |  pull() blocks on condvar when empty
                           v
        main thread: Fs/4 shift -> IfResampler (r8brain) -> FmDecoder/AmDecoder/...
                     -> AudioResampler (r8brain) -> gain -> PortAudioOutput::write()
                           |  copy to m_floatbuf, Pa_WriteStream() BLOCKS for ring room
                           v
        PortAudio blocking-I/O ring --> CoreAudio IO thread (RT) --> HAL --> DAC
```

Threads **[C]**:

| Thread | Created at | Work | Blocks on |
|---|---|---|---|
| Main / decode | process start | Everything from `pull()` to `Pa_WriteStream()`, plus status printing | `DataBuffer::pull()` condvar (`main.cpp:937`, `DataBuffer.h:94-106`); `Pa_WriteStream()` (`AudioOutput.cpp:305`); `stderr` writes (`main.cpp:1083-1109`) |
| Signal thread | `main.cpp:333` | `sigwait()`; sets `stop_flag` (`:275`) | signals only |
| Source thread (Airspy HF+) | `AirspyHFSource.cpp:361` (`std::thread`) | calls `airspyhf_start()` (`:374`), then sleeps in 100 ms steps (`:378`); samples arrive on libairspyhf's own USB thread via `rx_callback` (`:404-413`) | n/a |
| Source thread (Airspy R2) | `AirspySource.cpp:429` | same shape; callback at `:482-508` | n/a |
| Source thread (RTL-SDR) | `RtlSdrSource.cpp:322` | `rtlsdr_read_async()` runs the libusb event loop on this thread and calls `callback()` inline (`:356`, `:381-403`); 15 USB transfers in flight (`RTL_READ_ASYNC_20260713.md`) | libusb event loop |
| FileSource thread | `FileSource.cpp:384` | reads file, pushes, paces with `sleep_for` (`:440-455`) | `sleep_for` |
| CoreAudio IO thread | inside CoreAudio/PortAudio | real time; pulls from PortAudio's ring | not ours |

Block sizes and cadence (the "deadline" per `write()`):

| Source | Block | Period | Audio frames per `Pa_WriteStream()` at 48 kHz |
|---|---|---|---|
| FileSource at 384 kHz | 2048 samples (`FileSource.h:34`) | 5.33 ms | 256 |
| Airspy HF+ at 384 kHz | `main.cpp:716` nominal 2048; the true callback size is set by libairspyhf (not determined here) | about 5.3 ms if 2048 | about 256 |
| RTL-SDR 1.152 Msps | 16384 (`RtlSdrSource.h:33`) | 14.2 ms | 683 |
| Airspy R2 10 Msps | 65536 (`main.cpp:712`) | 6.55 ms | 314 |

Properties that matter for audio continuity:

- **One buffer between decoder and DAC**: PortAudio's ring, sized by the
  CoreAudio grant. There is no output thread (removed in 20231216-0, see
  `LATENCY_PLAN_20260713.md` section 2) and no application-level jitter
  buffer **[C, D]**.
- **`DataBuffer` is a backlog absorber, not a latency target.** `push()`
  never blocks and drops the oldest block only beyond 1024 blocks
  (`DataBuffer.h:36`, `:43-62`): 14.6 s at RTL defaults, about 6.7 s at Airspy
  R2 defaults (1024 x 6.55 ms), 5.5 s for 2048-sample blocks. A consumer stall
  therefore loses no SDR samples unless it approaches that bound; the drop is
  reported on `stderr` (`DataBuffer.h:55-59`) **[C]**.
- **Pacing is by `Pa_WriteStream()` alone.** The loop has no clock of its own.
  It runs as fast as input arrives, and ring back-pressure throttles it only
  when the ring is full **[C]**.
- **`write()` returning `false` is fatal** (`main.cpp:1046-1050`); only
  `paOutputUnderflowed` is excused (`AudioOutput.cpp:309`) **[C]**.
- **No timeout in `Pa_WriteStream()`.** If the stream stops consuming (device
  removed, system sleep, HAL hang) the main thread never returns to check
  `stop_flag` (polled only at the top of the loop, `main.cpp:927`), which is
  why SIGTERM does not stop a process stuck there: the signal thread only
  sets the flag **[C]**.
- **Per-write copy.** `write()` copies the block into `m_floatbuf`
  (`AudioOutput.cpp:300-303`); after the first call the `resize()` does not
  allocate unless the block size changes **[C]**.

## 3. How much buffered audio protects against a stall

What matters is the audio in the ring when the decode thread stops running
(its *occupancy*, not its capacity). The decoder wakes every 5 to 14 ms; any
stall longer than the occupancy at that instant is audible.

CoreAudio grant law from `LATENCY_CHECK_20260719.md` section 9 and
`LATENCY_OPTION_20260715.md` **[D]**: granted = B + R frames, with R the
request in frames at 48 kHz and B the smallest power of two >= 3R. Checked
arithmetically here for the cliffs (14 ms = 672 frames, 3R = 2016, B = 2048;
15 ms = 720 frames, 3R = 2160, B = 4096; 28 ms -> 4096, 29 ms -> 8192):

| `-L` | B (frames) | Granted | Host cycle (B / 48 kHz) | End-to-end, live HF+ + FiiO K7 **[D]** |
|---|---|---|---|---|
| 3 and below | 512 | 13.7 ms | 10.7 ms | never settles; unusable |
| 4 to 7 | 1024 | 25.3 to 28.3 ms | 21.3 ms | `-L 5`: 37.4 ms |
| 8 to 14 | 2048 | 50.7 to 56.7 ms | 42.7 ms | `-L 10`: 54.4 ms |
| 15 to 28 | 4096 | about 100 to 117 ms | 85.3 ms | `-L 20`: 91.6 ms |
| 29 to 40 | 8192 | about 200 to 210.7 ms | 170.7 ms | `-L 30`: 132.6 ms; default: 151.1 ms |

What this implies:

- **The host cycle B is CoreAudio's IO granularity.** The IO thread consumes
  in B-sized cycles and PortAudio's ring is topped up by the application
  **[I]**. In the 1024 bucket the whole cycle is 21 ms, about four HF+ blocks;
  one scheduling hiccup of that order is a dropout. This matches the audible
  gaps at `-L 7` under load and the starved-ring state observed at `-L 4`
  (`LATENCY_CHECK_20260719.md` sections 12 and 13.1) **[D]**.
- **Capacity is not margin on a live SDR.** A real-time source never produces
  the start-up burst that fills a deep ring: delivered delay at `-L 20/30/40`
  was only 0.87/0.66/0.72 of the granted capacity **[D]**. Occupancy is set by
  start-up history, so even the default has no guaranteed stall tolerance.
  That is the argument for explicit priming (P4).
- **"Deeper is safer" is unproven.** `-L 20` and `-L 30` showed no measured
  robustness gain over `-L 10` but cost 37 and 41 ms
  (`LATENCY_MEASUREMENT_20260725.md` section 8) **[D]**. The only load
  evidence is one `-L 7` episode; no systematic dropout vs `-L` vs load sweep
  exists. Section 8 proposes one.
- **Which `-L` was in use when `CHANGES.md:30` was written is not recorded**
  (the note predates the `-L` option; the ring was then the 40 ms-request
  default). Not determinable from the tree.

## 4. Mechanisms for disruption under host load

Likelihood is a judgment (H/M/L) for "can produce the reported symptom on an
Apple Silicon Mac with a browser active", not a measurement.

| ID | Mechanism | Evidence | Where | Likelihood |
|---|---|---|---|---|
| M1 | Decode thread descheduled or slowed (QoS, E-core placement, frequency, preemption by higher-QoS browser threads) | [I]; the code shows nothing requests priority | no QoS call anywhere; decode loop `main.cpp:927-1112` | **H** |
| M2 | Wake-up latency of the consumer after `DataBuffer::push()` (condvar notify to thread running) | [C] structure, [I] magnitude | `DataBuffer.h:60`, `:94-106` | M |
| M3 | Blocking write's wait path subject to timer leeway / coalescing | [I]; verify in PortAudio 19.7.0 `pa_mac_core_blocking.c` (source not on this machine) | `AudioOutput.cpp:305` | M |
| M4 | Ring too shallow for the stall; live-SDR occupancy is below capacity | [D] | `AudioOutput.cpp:220-232`; `LATENCY_MEASUREMENT_20260725.md` section 3 | **H** at `-L <= 7`, M above |
| M5 | `stderr` write blocks when the terminal does not drain its pty | **[M]** blocked after about 1.5 s of undrained pty | `main.cpp:1083-1109`, `:1064`, `:1071` | L to M (needs a terminal stall > 1.5 s) |
| M6 | Heap allocation and page faults on the audio path | [C] allocations exist; [I] impact | section 4.4 | L (small blocks), M (Airspy R2) |
| M7 | SDR-side sample loss: USB/libairspy thread starved, device overflows | [C] counters ignored; [I] occurrence | `AirspyHFSource.cpp:404-413`, `AirspySource.cpp:482-491` | M, **unobservable today** |
| M8 | `DataBuffer` overflow | [C]; reported if it happens | `DataBuffer.h:44-59` | L (needs seconds of stall) |
| M9 | CoreAudio side disturbed: HAL plug-ins, hidden sample-rate conversion, USB DAC contention | [D]: SoundSource added 28 to 44 ms and spurious steps; `AudioConverter` inserted silently when the rate differs (`OFFSET_MEASUREMENT_20260725.md`) | outside the program | M |
| M10 | Clock drift between SDR and DAC | [D] +3.6 to +4.1 ppm, about 13 ms/hour | n/a | not load related |

### 4.1 M1: decode thread scheduling (the leading candidate)

What the code shows **[C]**: the decode loop runs on the process's initial
thread; source threads are created with `std::thread` (or inside the SDR
libraries) with default attributes. No QoS class, scheduling policy, thread
policy, `os_workgroup`, power assertion, or `NSProcessInfo` activity exists in
the repository.

What is inference **[I]** (each is a hypothesis to check with Instruments
System Trace, section 8.5):

- **QoS inheritance.** A command-line tool started from a shell inherits the
  shell's/terminal's QoS (typically default or user-initiated), not
  user-interactive. Threads created by `pthread_create`/`std::thread` inherit
  the creator's QoS by default on Darwin, so setting QoS early in `main()`
  would propagate to the source threads and to threads libairspyhf/libusb
  spawn from them.
- **Apple Silicon core selection.** The scheduler prefers performance cores
  for high-QoS work and efficiency cores for lower classes under contention.
  A thread that wakes every 5 ms for 0.2 ms of work is the kind that ends up on
  an E-core at low frequency, and when a browser bursts it can wait behind
  higher-QoS runnable threads. Per-block cost is 0.18 ms (FM) to 0.44 ms
  (`-E 100`) on a P-core **[M]**; several times that on an E-core is still
  small against the period, but the latency to *start running* is what
  matters, and it is unbounded for a non-real-time thread under contention.
- **App Nap / timer coalescing.** App Nap applies to app-level processes that
  are occluded or idle; a tool run from Terminal is attributed to the terminal
  app, which may or may not trigger it. Timer coalescing affects timed sleeps
  of low-QoS threads (M3). `Utility::millisleep` (`Utility.h:140`) is used only
  by the source threads to poll `is_streaming`, not on the data path. Neither
  effect is established; `caffeinate -i` and a QoS raise are cheap tests.
- **Priority inversion on the `DataBuffer` mutex.** The consumer holds
  `m_mutex` for a pop (`DataBuffer.h:94-106`) and the producer for a push
  (`:43-62`); inversion needs a preempted holder, rare but possible under heavy
  contention **[I]**.

### 4.2 M3: how the blocking write waits (needs verification)

On CoreAudio, `Pa_WriteStream()` goes through PortAudio's blocking-I/O
adapter, which keeps a ring between the application and the IO callback and
latches an underflow flag when the callback finds it empty, returned as
`paOutputUnderflowed` by a later call **[I]**. My recollection is that the
wait for ring space is a short timed poll rather than a kernel semaphore. If
so, wake-up latency after the IO thread frees space is subject to the calling
thread's timer leeway, and lower QoS gets more leeway. **Check this in the
PortAudio 19.7.0 source; it is not available here.** If true, the effect is
extra delay between "room available" and "write issued", eroding margin
exactly when the system is busy.

### 4.3 M5: the `stderr` path (measured)

`fmt::print(stderr, "\r...")` runs on the decode thread every `stat_rate`
blocks (`main.cpp:728-729`, `:1053-1112`); `stat_rate` = ifrate / blocksize /
9, so about 9.4 lines per second at 384 kHz with 2048-sample blocks. `stderr`
is unbuffered and a pty has a small output queue. Newline events ("got/lost
stereo signal", `main.cpp:1064`, `:1071`) add to it.

Measured **[M]** (section 9): with `stderr` on a pty that was not drained, the
output file stopped growing about 1.5 s later, stayed flat for the rest of the
12 s stall, then resumed with a catch-up burst of about 3x real time when the
pty was drained. With `-q` there was no stall. Hence any blocking `stderr`
write stalls audio. Terminal emulators are GUI processes and can fall behind
under load, but a stall of more than about 1.5 s (this machine, this pty) is
needed. Inference: this explains long dropouts when the terminal is the
victim, not few-millisecond crackles.

### 4.4 M6: allocation on the audio path

Per block **[C]**:

| Allocation | Location | Size at HF+ / R2 |
|---|---|---|
| Source callback creates a new `IQSampleVector` | `AirspyHFSource.cpp:419`, `AirspySource.cpp:497`, `RtlSdrSource.cpp:390` | 16 KB / 512 KB |
| Two `DoubleVector`s in `IfResampler::process` | `IfResampler.cpp:46-50` | 2 x 16 KB / 2 x 512 KB |
| `DoubleVector input_double` in `AudioResampler::process` | `AudioResampler.cpp:48` | about 2 KB |
| `DataBuffer` queue growth (deque chunks) | `DataBuffer.h:48` | small |
| `m_floatbuf.resize()` | `AudioOutput.cpp:300` | none after the first call |

Decoders keep their buffers across blocks (`MAIN_CPP_SWAP_20260924.md`).
Small blocks go through malloc fast paths and cost microseconds. The Airspy
R2's 512 KB blocks are large enough that macOS malloc serves them from fresh,
zeroed pages, costing page faults and VM calls per block **[I]**, and the VM
system is where a memory-hungry browser contends. Exposure is highest for the
Airspy R2 and lowest for HF+. Not measured; Instruments (System Calls, VM
Tracker) during a dropout would show it.

### 4.5 M7: sample loss the program never sees

`airspyhf_transfer_t` carries a `dropped_samples` field
(`/opt/homebrew/Cellar/airspyhf/1.6.8/include/libairspyhf/airspyhf.h:108`) **[C]**;
`airspy_transfer_t` in libairspy has the same field by my recollection (header
not installed here) **[I]**. Neither `rx_callback` reads it. A starved USB
thread that lets the device overflow therefore produces a click
indistinguishable from an output underrun and leaves no trace. RTL-SDR gives
no drop count through `rtlsdr_read_async()`; with 15 transfers in flight
about 213 ms of margin exists at default settings **[D]**
(`LATENCY_PLAN_20260713.md`), so a loss there needs a long stall.

### 4.6 M8: DataBuffer overflow

Needs the consumer stalled for 1024 blocks (5.5 to 14.6 s). The warning is
printed from the producer thread (`DataBuffer.h:55-59`), a `stderr` write
inside a USB callback; harmless in practice because it happens only in
overload.

### 4.7 M10: clock drift is a different thing

The SDR and DAC clocks are independent: +3.6 to +4.1 ppm, 13 ms per hour
**[D]**. When the SDR is slower the ring slowly drains and eventually
underruns, in principle once per many minutes; when faster, the surplus
accumulates in `DataBuffer`. This is load-independent and extremely slow.
To tell it apart: it appears on an idle machine, and the fill-level telemetry
of P1 would show a monotonic ramp rather than a sudden drop coincident with a
load burst. On the clean (non-SoundSource) path no underrun-recovery up-steps
were seen in 30 loopback runs **[D]** (`LATENCY_CHECK_20260719.md` section
16).

## 5. How a stall turns into a gap, and what remains afterward

1. The decode thread stalls for S ms; the ring holds F ms at that instant.
2. If S <= F: nothing is heard; the ring is simply lower afterward. This is
   why deep buffers "absorb" stalls and the delivered latency drops by S
   **[D]** (`LATENCY_CHECK_20260719.md` section 8.2, run `l20_2`: a 33 ms
   producer stall lowered latency 162.4 to 129.6 ms with no audible effect).
3. If S > F: the IO thread finds the ring empty and outputs silence, a hard
   zero-fill in mid-waveform, heard as a crackle or gap; PortAudio latches an
   xrun, returned as `paOutputUnderflowed` by a later write **[I]**.
4. When the decode thread runs again, `DataBuffer` holds about S ms of blocks.
   It decodes them back to back (it is 3 to 8 % busy, so it has plenty of
   speed) and writes them. The ring refills; blocks that do not fit stay
   queued because the DAC drains at exactly 1x. Measured analogue for the
   file case in section 9 (catch-up at about 3x real time, no data lost).
5. Result **[C + I]**: the audio is late by the gap plus the residual queue,
   and the program has nothing that drains the backlog other than a
   faster-than-DAC SDR clock or the 1024-block overflow (which drops blocks).
   Repeated stalls ratchet latency up; the ppm creep is the only repayment.
   Seen indirectly in the earlier work as permanent +28 ms steps on the
   SoundSource path **[D]**; the magnitude on a clean path under field load is
   not measured.

## 6. User-side workarounds (no code change)

Ordered by expected usefulness. Only the `-q` row has any measurement behind
it in this review.

| Workaround | Effect | Trade-off | Evidence |
|---|---|---|---|
| Use `-q` | Removes all periodic `stderr` writes from the decode thread; a stalled terminal can no longer block audio | Loses the status line (turn it on only to diagnose) | **[M]** |
| Move up to `-L 8` to `-L 10` (2048 bucket, 42.7 ms host cycle) from `-L 5` to `-L 7` | Doubles the host cycle; the documented sweet spot | 54.4 ms vs 37.4 ms end-to-end | **[D]** |
| Leave `-L` unset (40 ms request, 210.7 ms granted, 151 ms delivered) when gap-free playback matters more than latency | Largest ring | 151 ms delay; occupancy still depends on start-up | **[D]** |
| Avoid `-L 3` and below | These never settle | none | **[D]** |
| `caffeinate -i ./airspy-fmradion ...` | Prevents idle sleep and, if App Nap is a factor, keeps the process out of it | Keeps the Mac awake for the session | [I] |
| `sudo nice -n -10 ./airspy-fmradion ...` | Raises BSD priority of all threads; on Apple Silicon this is a weak lever next to QoS, so the benefit may be small | Needs root; also raises libusb threads; untested | [I] |
| `taskpolicy -c <class>` can only lower (clamp) the QoS of a process; use it on the *competing* job (`-c background`) in tests, not on airspy-fmradion | Narrow: helps only if you control the heavy process | n/a | [I] |
| Keep the terminal visible and in the foreground; prefer a light terminal | Reduces M5 and any occlusion-related App Nap | Habit | [I] |
| Quit or bypass CoreAudio HAL plug-ins (Rogue Amoeba SoundSource/ACE, Loopback, eqMac) and play directly to the DAC | Removes extra buffering and its own scheduling; SoundSource added 28 to 44 ms and spurious steps | Loses the plug-in's features | **[D]** |
| Set the DAC's nominal rate to 48000 Hz (Audio MIDI Setup) so no `AudioConverter` is inserted | Avoids a hidden resampler stage | none | **[D]** (`OFFSET_MEASUREMENT_20260725.md`) |
| Omit `-E` when not needed | Cuts per-block decode time about 2.5x (0.44 to 0.18 ms) | Loses multipath mitigation | **[M]** |

For recorded output (`-W` etc.) none of this applies: file writes have no
real-time sink.

## 7. Source-code proposals

All are sketches only; nothing is applied. macOS calls go under
`#ifdef __APPLE__` so Linux and Raspberry Pi builds are unchanged. No
proposal touches `OPTIMIZATION_FLAGS`; `-ffast-math` is not proposed.

### P1. Underrun counting, fill-level monitoring, SDR drop counters (rank 1)

*Rationale.* The program cannot distinguish its failure modes (section 4);
`paOutputUnderflowed` is returned and discarded (`AudioOutput.cpp:309`).
Without telemetry every other fix is guesswork.

*Sketch.*

```cpp
// PortAudioOutput members
std::uint64_t m_underflows = 0;
long m_min_write_avail = LONG_MAX;   // lowest free space seen => ring high-water

// write():
const long avail = Pa_GetStreamWriteAvailable(m_stream);
if (avail >= 0) m_min_write_avail = std::min(m_min_write_avail, avail);
...
} else if (m_paerror == paOutputUnderflowed) {
  ++m_underflows;                    // was silently ignored
  return true;
}
```

In `main.cpp` print `underflows`, the ring fill estimate (capacity from
`Pa_GetStreamInfo()->outputLatency * rate`, minus `min_write_avail`),
`source_buffer.queue_size()` and `dropped_blocks()` on the status line under
a new option (for example `-v`), and always as one summary line at exit. For
SDR drops, accumulate `dropped_samples` in `AirspyHFSource::rx_callback`
(`:404-413`) and `AirspySource::rx_callback` (`:482-491`) and expose it as an
atomic counter through the `Source` interface.

*Expected benefit.* Separates output underrun (M1/M4), SDR loss (M7), queue
overflow (M8) and drift (M10) in one run; supplies the data for P4 and P5.

*Risks/costs.* One `Pa_GetStreamWriteAvailable()` call per block (a ring
query on blocking streams, believed lock-free **[I]**). Accuracy caveats:
several xruns can coalesce into one flag and an xrun can surface one write
late, so the count is a lower bound **[I]**. `Pa_GetStreamCpuLoad()` is not
useful for blocking streams **[I]**. Portable.

*Verify.* With no load the counter stays 0 over 10 minutes; with `-L 7`
under the section 8 loads it rises and correlates with the loopback-detected
gaps.

### P2. Thread QoS on macOS (rank 2)

*Rationale.* M1. QoS is the supported way for a non-real-time thread to ask
the scheduler for P-core placement and priority.

*Sketch.*

```cpp
#ifdef __APPLE__
#include <pthread/qos.h>
// First thing in main(), before any thread is created, so threads created
// afterwards (std::thread, and library threads started from them) inherit it.
pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
```

Fall back to `QOS_CLASS_USER_INITIATED` if user-interactive proves too
aggressive. The signal thread only waits and can drop to
`QOS_CLASS_UTILITY` after creation. Do not set QoS inside library-owned
threads; rely on inheritance and verify it.

*Expected benefit.* Possibly large against M1 if the thread is now on an
E-core or a default band **[I]**; none against M4/M9. Browser render/UI
threads also run at high QoS, so this narrows the gap to them rather than
guaranteeing priority over them.

*Risks/costs.* No latency cost; marginally higher power use; macOS only.
Unknown whether inheritance reaches threads created inside libairspyhf and
libusb.

*Verify.* Instruments System Trace (QoS, core type, runnable-to-running
delay) before and after; then repeat the load matrix with the P1 counters.

### P3. Keep `stderr` off the audio thread (rank 3)

*Rationale.* M5 is a deterministic hard block (measured). Hand the status
line to a small low-priority thread: the decode loop updates a struct of
`std::atomic` fields (block, ppm, IF dB, AF dB, pilot), a `StatusPrinter`
thread prints every 100 ms. Route the "got/lost stereo signal" messages
(`main.cpp:1064`, `:1071`) the same way. After start-up the decode loop never
touches `stderr`.

*Expected benefit.* Removes M5 entirely and makes `-q` unnecessary as a
workaround.

*Risks/costs.* Output becomes asynchronous (up to 100 ms stale, possible
field tearing unless one snapshot is used); exit ordering needs care (join
before the final `fmt::println(stderr, "")` at `main.cpp:1168`). Portable;
modest code.

*Verify.* Re-run the pty stall test (section 9) without `-q`: output must
keep growing during the stall.

### P4. Explicit ring priming with a target fill (rank 4)

*Rationale.* On a live SDR the ring fills only as fast as the source
produces, so occupancy is start-up luck (section 3). Writing T ms of silence
before the first real block makes the cushion a parameter: delay becomes T
plus the path constant, and stall tolerance becomes about T.

*Sketch.* New option (for example `--pa-prefill ms`, default 0 to preserve
behavior), applied after `Pa_StartStream()` (`AudioOutput.cpp:250`) or lazily
on the first `write()`:

```cpp
std::vector<float> silence(frames_for(prefill_ms) * m_nchannels, 0.0f);
Pa_WriteStream(m_stream, silence.data(), silence.size() / m_nchannels);
```

Adaptive variant (needs P1): on each `paOutputUnderflowed` write one more
silence quantum (for example 10 ms), up to a cap, so a system with repeated
stalls converges to a stable fill. Shrinking needs sample dropping and is out
of scope.

*Expected benefit.* The only way to guarantee a specific stall tolerance on a
live source. The adaptive form reproduces, deliberately, the "up-step after
underrun" seen incidentally on the SoundSource path **[D]**.

*Risks/costs.* Adds exactly T ms of latency (the point). T must be below the
granted capacity minus one block: with `-L 5` (26 ms granted) there is little
room, so priming belongs with `-L 8` to `-L 10`. Start-up silence is
inaudible; adaptive increases are gaps that already happened. Does nothing
for M7/M9. Portable.

*Verify.* Sampled-loopback delay (`LATENCY_MEASUREMENT_20260725.md`) must
equal the prediction within the method's +-0.1 ms; stall-injection tolerance
(section 8.3) must match T.

### P5. Backlog trimming after a stall (rank 5)

*Rationale.* Section 5: after a stall the queue never drains. Policy: if
`source_buffer.queue_size()` exceeds a threshold (for example 4 blocks) after
a detected stall, discard whole IQ blocks at the decoder input, preferably
while the audio is squelched.

*Sketch.* After `pull()` (`main.cpp:937`):
`while (source_buffer.queue_size() > keep) (void)source_buffer.pull();`,
guarded so it fires on a stall event only.

*Expected benefit.* Bounds latency over hours; recovers the stall residue and
the 13 ms/hour creep.

*Risks/costs.* A second discontinuity (ideally masked by the stall that
caused it); the PLL, AGC and multipath filter see a jump (pilot lock takes
about 0.2 s to re-establish, `MF_PLL_DIFFERENCE_20260728.md`); thresholds are
hard to tune. Do it only if P1 shows a persistent backlog in practice.

*Verify.* Inject a 300 ms stall (`SIGSTOP`/`SIGCONT`); latency must return to
its pre-stall value and the extra click count must be at most one.

### P6. Recycle per-block vectors (rank 6)

*Rationale.* M6. Keep a small pool of preallocated `IQSampleVector`s
between the sources and the consumer (spent vectors returned through a second
queue in `DataBuffer`), and hoist the `DoubleVector`s of
`IfResampler::process` (`IfResampler.cpp:46-50`) and `AudioResampler::process`
(`AudioResampler.cpp:48`) to members.

*Expected benefit.* Removes per-block malloc and, for the Airspy R2,
large-allocation page faults. Gain unknown; HF+ and RTL blocks are small.

*Risks/costs.* Changes the `DataBuffer` interface and all four sources; the
project has no test suite (CLAUDE.md). Measure first (Instruments
Allocations); proceed only if allocation shows up in a trace during a
dropout.

### P7. Time-constraint policy or audio workgroup (rank 7)

*Rationale.* `THREAD_TIME_CONSTRAINT_POLICY` is the strongest class (what the
CoreAudio IO thread uses); `kAudioDevicePropertyIOThreadOSWorkgroup` plus
`os_workgroup_join()` tells the scheduler about the IO deadline.

*Assessment: poor fit, do last.*

- The decode thread is woken by SDR block arrivals (5 to 14 ms) and then
  throttled by `Pa_WriteStream()`; its work is not tied to the IO cycle. A
  workgroup is meant for threads producing audio per IO cycle, and members
  are expected to be real-time.
- A time-constraint thread needs a declared period, computation and
  constraint (for example 5.33 / 0.5 / 5.33 ms for HF+); the kernel demotes
  threads that overrun their computation budget. Cost varies with `-E`, the
  block size and the source, so the numbers must be derived per configuration.
- PortAudio does not expose the `AudioDeviceID` it uses, so the program would
  need its own CoreAudio lookup (the Python doc tools already do this,
  `doc/coreaudio_rate.py`) and link CoreAudio.framework directly.
- macOS-only and untestable elsewhere.

Try it only if P1 and P2 data show the thread is still preempted at the
highest QoS. Then the time-constraint policy first (smaller change), the
workgroup after.

### P8. PortAudio callback with a lock-free ring (rank 8, not recommended)

*What the earlier attempts did.*

- `doc/20230908-portaudio-callback-test.diff` (2023): callback-driven output;
  a `moodycamel::BlockingReaderWriterCircularBuffer<float>` of
  `samplerate * 4` floats; the callback calls `wait_dequeue()` once **per
  float** and `write()` calls `wait_enqueue()` per float; frames-per-buffer
  computed from `defaultHighOutputLatency`; channel count fixed at 2 in the
  callback. The file's own header says it did not work on Linux using
  gstreamer RTP and was reverted; `CHANGES.md:169` records "low latency
  setting for buffering-based PortAudio didn't work well" (20230923). Design
  problems visible in the diff: a blocking dequeue inside a real-time
  callback (an underrun blocks the audio thread instead of producing silence)
  and per-sample semaphore/atomic cost. Why it was dropped beyond the
  recorded text is not determinable.
- 2025 (`367e281`, 2025-09-02, "clicky but working implementation with
  callbacks"): `pa_ringbuffer`, `suggestedLatency` fixed at 0.2 s, the
  callback zero-fills when the ring is short, and `write()` returned `false`
  when the ring was full, which `main.cpp:1046-1050` treats as fatal. The
  commit message states latency was **not** improved ("a simple overkill"), and
  `39a4cff` (2025-09-06) reverted it. Determinable reasons: no latency gain
  (the CoreAudio grant dominates, not PortAudio's blocking layer, consistent
  with `LATENCY_PLAN_20260713.md` item 5), and clicks. The click cause is
  inferred from the diff (hard zero-fill on underrun plus a failing write),
  not tested.

*Assessment.* A callback model changes who waits, not who must be on time:
the decode thread still has to refill the ring before it empties, so it does
not fix M1 or M4. Its real advantages (exact underrun detection inside the
callback, ring sized independently of the grant, fade-out/in around an
underrun instead of a hard zero-fill) can mostly be had from P1 and P4 within
the blocking API. Reconsider only if fades around underruns become a
requirement, and then with `pa_ringbuffer`, a non-blocking callback, a write
that waits rather than fails, and the channel count taken from the stream.

### 7.1 Rejected

| Idea | Why |
|---|---|
| `mlock`/`mlockall` | The audio path's pages are touched every 5 to 14 ms and will not be compressed or paged out; `RLIMIT_MEMLOCK` and privilege issues; it does not prevent page faults on fresh allocations (that is P6). |
| `-ffast-math` | Forbidden (breaks `MultipathFilter` NaN detection); decode speed is not the limiter anyway. |
| Smaller write/block sizes to "improve smoothness" | `LATENCY_PLAN_20260713.md` rated decoupling block size as high complexity and worse for scheduling hiccups (more wake-ups); the `-L 3` run suggests small pieces average slower than real time (`LATENCY_MEASUREMENT_20260725.md` section 6.4, inferred there). |
| Logging from SDR callbacks | Adds `stderr` writes in the USB thread; collect counters and print elsewhere (P1, P3). |

### 7.2 Recommended order

1. **P1** (measure); judge everything below by its counters.
2. **P2** and **P3** together: both small, independent mechanisms (M1, M5).
3. **P4** with `-L 8` to `-L 10` as the supported low-latency configuration,
   if P1 shows underruns still occur.
4. **P5** if P1 shows a persistent queue after stalls.
5. **P6**, **P7**, **P8** only on specific evidence from traces.

## 8. Verification plan

### 8.1 Goal and caveat

The aim is a repeatable dropout count per (configuration, load) cell, from
two independent observers: the program's own counters (after P1) and an
external recording of what the DAC would play.

**FileSource caveat** **[C]**: `FileSource::run` paces with `sleep_for` and,
if it falls behind, discards the debt (`FileSource.cpp:453-455`). A delayed
source thread therefore leaves a hole in the IQ stream that is never caught
up, unlike a live SDR whose USB transfers queue and arrive in a burst. A
FileSource test is *pessimistic* for source-thread stalls and *representative*
for decode-thread stalls (the quantity of interest). Treat source-side stalls
found in file tests with skepticism and confirm on a real SDR.

### 8.2 Deterministic input

Use a constant, machine-checkable signal so any interruption is detectable:

- Generate a 384 kHz complex float WAV holding a plain FM carrier (no pilot,
  mono, 1 kHz tone, 40 kHz deviation, 60 s) with a short numpy + soundfile
  script, and play it with
  `-t filesource -c freq=<any>,srate=384000,filename=tone.wav -P <dev>`.
- The `test-files/*.wav` recordings (all 384 kHz IQ) work for a coarse,
  realistic comparison; music is hard to check for gaps by machine.

### 8.3 Synthetic load

| Load | Command | What it exercises |
|---|---|---|
| CPU saturation, default QoS | `for i in $(seq $(sysctl -n hw.ncpu)); do yes > /dev/null & done` (stop with `pkill yes`) | Scheduler contention at default QoS |
| CPU saturation, high QoS | a small C (or Python `ctypes`) program whose N busy threads call `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)` | Closest to browser render threads; the only load that can beat a QoS-raised decode thread |
| Memory pressure | allocate and touch 70 to 90 % of RAM in a loop, or `memory_pressure` | VM contention, page faults, compressor (M6) |
| Bursty mix | alternate 2 s idle / 2 s full load in a shell loop | Looks like browser activity; shows recovery |
| Real browser | 20 tabs of a WebGL/video benchmark | Final confirmation |
| Terminal stall | run under `script -q /dev/null ...` and `kill -STOP` the `script` process for 10 s | M5 (section 9) |
| Process stall | `kill -STOP <pid>; sleep 0.1; kill -CONT <pid>` | Injects a stall of exact length S: the cleanest way to measure ring tolerance |

`stress-ng` is not installed here and has weak macOS support; `yes` plus the
QoS-raised busy threads cover the cases.

### 8.4 Objective dropout measurement

In order of preference:

1. **Program counters (after P1):** `underflows`, minimum ring fill,
   `dropped_blocks`, SDR `dropped_samples`, compared with a no-load run of the
   same configuration. Automatic and works with the real DAC.
2. **Loopback recording.** Send `-P` to a virtual loopback device (or the
   physical DAC to an ADC) and record with `sounddevice`, reusing the capture
   machinery of `doc/loopback_clock_cal.py`. With the 1 kHz tone:
   - Hilbert transform; flag envelope below 10 % of the median for more than
     0.5 ms (zero-fill) and instantaneous frequency outside 1 kHz +-1 %.
   - Phase continuity: fit tone phase over sliding 1 s windows; a jump above
     0.2 rad is a gap or a latency step (the method that showed the capture
     "provably gapless" in `LATENCY_CHECK_20260719.md`).
   Report events and total silence per minute.
3. **Delay trace** (sampled loopback, `LATENCY_MEASUREMENT_20260725.md`): sees
   gaps and latency steps; use it for P4 and P5.

Suggested matrix (one hour for a first pass):

| Axis | Values |
|---|---|
| `-L` | 5, 7, 10, unset (40) |
| Load | none; `yes` x ncpu; QoS-raised busy threads; memory pressure; stall injection of 20/50/100/200 ms |
| Build | baseline; +P2; +P2+P3; +P4 |
| Repeats | at least 3 per cell, 10 minutes each |

Primary outputs: dropout events per 10 minutes per cell, and the largest
injected stall with no event per `-L` (the direct measure of ring margin,
section 3).

### 8.5 Scheduler traces

Instruments System Trace (or Time Profiler with thread states) on the decode
thread during a dropout. Look for runnable-but-not-running intervals (M1),
core type (E vs P), QoS class, a blocked state in the write path (M3) or in
`write(2)` on `stderr` (M5). `log stream --predicate 'subsystem ==
"com.apple.coreaudio"'` may show HAL overload messages (not tried).

## 9. Experiments run for this review

Machine: Apple M2 Pro, 10 logical CPUs, macOS (Darwin 27.0.0). Binary: the
existing `build/airspy-fmradion` (not rebuilt; no scratch build directory was
created; no repo file other than this one was written). Input:
`test-files/joakfm-20260715045930z-iq.wav`, 384 kHz, FileSource (real-time
paced), `-t filesource -c freq=82500000,srate=384000,...`, foreground runs.
The harness scripts were kept in the session scratchpad, not in the repo.

**E1: decode CPU cost [M].** `-q -F /dev/null`, 20 s wall: user 0.65 s + sys
0.07 s (FM) and user 1.63 s + sys 0.08 s (`-E 100`), i.e. 3.3 % and 8.2 % of
one core (user time), or about 0.18 ms and 0.44 ms per 5.33 ms block. One run
each.

**E2: `stderr` backpressure [M].** The program wrote `-F` float output to a
file with its `stderr` on a pty whose master end a Python harness drained,
except from t = 8 s to t = 20 s. Output file size sampled every 0.5 s:

| Interval | Output rate |
|---|---|
| t = 1 to 8 s | 385 to 389 KB/s (real time: 48 kHz x 2 x 4 B = 384 KB/s) |
| t = 8 to about 9.5 s | pty absorbs the status lines; rate falls to zero at about 9.5 s |
| t = 10 to 20 s | 0 B/s |
| t = 21 to 23 s (after drain) | 1.23 MB/s, 1.27 MB/s, then 0.76 MB/s (catch-up at about 3x real time) |
| t = 24 s onward | 387 KB/s, steady |

Control with `-q` under the identical schedule: 387 KB/s throughout, no
stall. The catch-up burst shows the decoder is fast and that the backlog sat
in `DataBuffer` with nothing lost. The deficit (about 10.5 s) then remains
as permanent delay, which for PortAudio would be latency (section 5). One run
each; the 1.5 s absorption depends on the pty queue size and line length.

**Not run:** any PortAudio (`-P`) test, load test, QoS test, Instruments
trace, or listening test. Those need the audio hardware and, for the load
matrix, the instrumented build (P1).

## 10. Open questions the review could not settle

1. Whether the reported crackles are output underruns, SDR loss or
   something else: no telemetry exists (P1 resolves it).
2. Which QoS class and core type the decode thread actually has, and
   whether QoS is inherited by library-created threads (System Trace).
3. How PortAudio 19.7.0's blocking write waits (timed poll or semaphore) and
   how `paOutputUnderflowed` is latched (read `pa_mac_core_blocking.c`).
4. Whether libairspy's `airspy_transfer_t` has `dropped_samples` in the
   installed version (HF+ header confirmed; R2 header not on this machine).
5. Which `-L` was in use when the `CHANGES.md` known limitation was written.
6. How large the latency ratchet is on a clean path under heavy field load
   (no up-steps in 30 idle-host loopback runs).
7. The pty queue size and terminal stall frequency on the author's setup under
   browser load.
