# PortAudio underflows under browser and graphics load on macOS (2026-10-19)

Status: the analysis was done on `dev` at `9570a05` before any code
change, and its file:line citations refer to that tree. No binary was
run with `-P`, and nothing was measured on hardware for this report.
Ranks 1 to 3 of section 7 were then implemented on branch
`dev-macos-realtime-thread` (section 7.1); that code is build-verified
only.

Question: when web browsers and other graphics-intensive programs run on
the same Mac mini 2023 (M2 Pro) as `airspy-fmradion -P`, the program
reports a few PortAudio underflows. What are the possible reasons, and
what can reduce the audible disruption?

Inputs:

- Source tree: branch `dev` at `9570a05` (release 20261017-0 plus a
  CHANGES.md entry). The code analysis was done by the cpp-expert agent,
  and the key PortAudio claims were re-checked by reading the source.
- PortAudio: Homebrew 19.7.0 (`/opt/homebrew/Cellar/portaudio/19.7.0`),
  the version the build links. Its source at tag `v19.7.0`
  (https://github.com/PortAudio/portaudio) was read, not built. Paths
  below marked `H/` are under `src/hostapi/coreaudio/` of that tree.
- Earlier work: `doc/PORTAUDIO_DISRUPTION_20261010.md` (the "old
  review"; mechanisms M1 to M10, proposals P1 to P8),
  `doc/BUGS_20261017.md`, `doc/LATENCY_CHECK_20260719.md`,
  `doc/LATENCY_MEASUREMENT_20260725.md`.
- Web sources (Apple documentation, PortAudio GitHub, developer forums),
  listed in section 11.

Evidence tags:

| Tag | Meaning |
|---|---|
| [C] | Read from this tree's code (file:line). |
| [P] | Read from the PortAudio 19.7.0 source (path:line). |
| [D] | Measured earlier and recorded in `doc/`. Not re-measured here. |
| [W] | From a web source in section 11. |
| [I] | Inference. Not verified; a check is given where possible. |

## 1. Summary

### 1.1 What an underflow is in the current code

1. PortAudio's blocking output keeps its own ring between
   `Pa_WriteStream()` and the CoreAudio IO callback. On every IO cycle
   the callback needs one device buffer (R frames, about `-L` ms). If
   the ring holds fewer than R frames, the callback plays what it has,
   fills the rest with zeros (`bzero`), and sets a latched underflow bit
   (`H/pa_mac_core_blocking.c:383-399`) [P]. The listener hears a hard
   cut to silence and a hard restart: two clicks around a gap. There is
   no fade.
2. The decode thread's stall tolerance at any moment is therefore
   "ring fill minus R", not "ring fill" [P]. The ring size is fixed by
   `-L` (section 3.2). At `-L 8` to `-L 14` the ring is 2048 frames
   (42.7 ms) and the tolerance is at most about 23 to 29 ms. A browser
   that delays the decode thread or the USB threads by more than that
   causes an underflow.
3. The `UF=` counter mixes three kinds of event [C, P]:
   - ring shortfall caused by a late writer (the case above);
   - CoreAudio IO-cycle overloads (`kAudioDeviceProcessorOverload`),
     which PortAudio ORs into the same bit
     (`H/pa_mac_core_utilities.c:722-756`,
     `H/pa_mac_core_blocking.c:354`);
   - one start-up underflow, which is counted when the first IQ block
     arrives after the ring has drained (`sfmbase/AudioOutput.cpp:330-335`).

   The bit is latched, so several events between two writes count as
   one, and an event is reported up to one write late. "A few
   underflows" may include one start-up event that has nothing to do
   with load.

### 1.2 Most likely reasons, in order

| Rank | Reason | Why |
|---|---|---|
| 1 | The decode thread, or the libairspyhf/libusb thread that feeds it, wakes late because it competes with browser and WindowServer threads of the same QoS class | The program asks for `QOS_CLASS_USER_INTERACTIVE` (`main.cpp:342-349`) [C]. That is the class browsers use for UI and rendering, so it equals them rather than beating them [I]. Per-block CPU time is small (0.18 to 0.44 ms of a 5.3 ms block) [D], so this is wake-up latency, not throughput. |
| 2 | The ring margin for the chosen `-L` is small | 23 to 29 ms at `-L 8` to `-L 14`, about 9 to 11 ms at `-L 4` to `-L 7` (section 3.2) [P, I]. |
| 3 | The CoreAudio IO thread itself misses its deadline (HAL overload), counted as an underflow | coreaudiod logs `HALS_OverloadMessage` / "skipping cycle due to overload" in such cases, and forum users tie these to memory pressure and USB topology [W]. |
| 4 | Memory pressure from the browser slows large per-block allocations | Airspy R2 allocates about 1.5 MB per block, RTL-SDR about 384 KB, Airspy HF+ 16 KB (section 4.5) [C, I]. |
| 5 | Start-up underflow counted in `UF` | Load-independent; one event [C]. |
| 6 | SDR-side sample loss (shown as `SDRdrop`, not `UF`) | Sounds the same; check `SDRdrop` when an event happens [C]. |
| 7 | Another CoreAudio client changes the device buffer size | PortAudio does not resize its ring when that happens (`H/pa_mac_core.c:1089-1125`) [P]. Whether the size is per-process on current macOS is not settled here [I]. Low likelihood. |

### 1.3 Workarounds, in order

No code change (details in section 6):

1. Choose `-L` by the ring buckets in section 3.2, not by the requested
   latency alone. Within a bucket the smallest `-L` gives the most
   tolerance. Avoid `-L 4` to `-L 7`. If gaps matter more than 50 ms of
   latency, use `-L 15` to `-L 20` (about 60 to 65 ms of tolerance) or
   the default latency (up to about 125 ms).
2. Add `--portaudio-prefill` sized to the ring, so the margin does not
   depend on how long the source took to start. For example, use
   `-L 8 --portaudio-prefill 30` or `-L 15 --portaudio-prefill 65`.
   These values are estimates; check them on the `Ring=` display.
3. Run once with nothing else active to count the start-up underflow,
   and watch coreaudiod's log for overload messages during a load
   event. This separates reason 3 from reasons 1 and 2.
4. Play directly to the DAC at a 48 kHz nominal rate. Avoid HAL
   plug-ins such as SoundSource, Loopback, or eqMac. Connect the SDR and
   the DAC to different ports, without a shared hub. Keep memory
   pressure out of the yellow range.

Code changes (details in section 7): fix the telemetry first
(start-up count, trough fill, HAL-overload versus ring-shortfall
separation). Then try a time-constraint scheduling policy for the
decode thread. Revive the reverted callback design (section 8) only to
replace the hard zero-fill with fades and to make the cushion
independent of `-L`. It reduces what is heard when an underflow
happens; it does not prevent the underflow.

## 2. What changed since the 2026-10-10 review

Implemented in 20261017-0 (`54fae37`, `17d1176`, `60d0050`, and the fixes
`1f36883`, `c68a702`, `b8e8bae`, `89fba8c`):

- P1 telemetry: `-v` shows `UF=` (underflows), `Ring=` (estimated ring
  fill), queue size and high-water mark, dropped queue blocks, and
  `SDRdrop`; a summary line is printed at exit.
- P2 QoS: `QOS_CLASS_USER_INTERACTIVE` for the main thread and threads
  created after it; the signal and status printer threads lower
  themselves to `QOS_CLASS_UTILITY`.
- P3 status printing moved to `StatusPrinter`; the decode thread uses
  `try_lock` and never waits on stderr during normal operation
  (`sfmbase/StatusPrinter.cpp:40-48`) [C].
- P4 experimental `--portaudio-prefill` and `--portaudio-prefill-adapt`.

Not implemented in 20261017-0: P5 (backlog trimming), P6 (vector
pools), P7 (time-constraint policy or workgroup), P8 (callback design).
P7 is now the experimental `--realtime` option (section 7.1). The field
test of the `-P` path planned in the handoff has not been done. The
underflows reported now are the first field data from the new
counters.

## 3. How the output path behaves now

### 3.1 PortAudio 19.7.0 blocking output on CoreAudio

Facts read from the PortAudio source [P]:

- **Wait method.** `PA_MAC_BLIO_MUTEX` is commented out
  (`H/pa_mac_core_blocking.h:73-77`, "not actively maintained"), so
  `WriteStream` polls. When the ring has no free frame it calls
  `Pa_Sleep(5)`, which is `nanosleep` (`H/pa_mac_core_blocking.c:522-539`,
  `src/os/unix/pa_unix_util.c:108-116`). When the ring has room, the
  write copies and returns without sleeping. This answers the old
  review's open question M3. The poll matters only when the ring is
  full; it does not delay a writer that is behind.
- **Ring size.** The next power of two of
  `max(2 * suggestedLatency * rate, 3 * deviceBufferFrames)`
  (`H/pa_mac_core_utilities.c:385-410`).
- **Device buffer.** `suggestedLatency * rate` minus the device's fixed
  latency, clipped to the device's range, written to
  `kAudioDevicePropertyBufferFrameSize` (`H/pa_mac_core.c:1384`,
  `:1654-1691`).
- **Reported latency.** `outputLatency` is the sum of the fixed device
  latency, device buffer, ring, and processor latency
  (`H/pa_mac_core.c:1844`, `:1917`, `:2008`, `:2051`). This is the
  "granted = R + B" law found by measurement in
  `doc/LATENCY_CHECK_20260719.md`. The old review called B the
  "CoreAudio host cycle"; B is PortAudio's ring, and the IO cycle is
  about R.
- **Start state.** The ring starts full of zeros
  (`H/pa_mac_core_blocking.c:267-278`). The stream starts in the
  `PortAudioOutput` constructor (`sfmbase/AudioOutput.cpp:253`, created at
  `main.cpp:736`), while the source starts later (`main.cpp:906`) [C]. The
  ring drains during that interval. If the first block arrives after the
  ring has drained, the first write reports an underflow.
- **Underflow.** See section 1.1.
- **Thread priority.** PortAudio 19.7.0 sets no thread policy, priority,
  or workgroup anywhere in `H/` (grep). The IO callback runs on
  CoreAudio's real-time IO thread.
- **Host-API flags.** The program passes no `PaMacCoreStreamInfo`
  (`sfmbase/AudioOutput.cpp:230`) [C], so the default `paMacCorePlayNice`
  applies. In 19.7.0, PlayNice versus Pro changes only whether
  PortAudio may switch the device's sample rate (`H/pa_mac_core.c:1388`);
  the `MinimizeCPU` flags are defined but unused. With a device not at
  48 kHz, AUHAL runs a converter inside the IO callback.

### 3.2 Ring margin by `-L` (48 kHz)

Estimates assume the device buffer R equals the request, so they are
[I] to within the device's fixed latency, which PortAudio subtracts from
the request. The tolerance column is the largest stall the decode thread
can survive when the ring is as full as it can be in steady state,
taken as ring − R − one block (256 frames for Airspy HF+). It is an
upper bound; the real margin depends on how full the ring is.

| `-L` (ms) | R (frames) | Ring (frames / ms) | Max stall tolerance (ms) | Measured end-to-end delay [D] |
|---|---|---|---|---|
| 5 | 240 | 1024 / 21.3 | about 11 | 37.4 ms |
| 7 | 336 | 1024 / 21.3 | about 9 | gaps heard under load |
| 8 | 384 | 2048 / 42.7 | about 29 | |
| 10 | 480 | 2048 / 42.7 | about 27 | 54.4 ms |
| 14 | 672 | 2048 / 42.7 | about 23 | |
| 15 | 720 | 4096 / 85.3 | about 65 | |
| 20 | 960 | 4096 / 85.3 | about 60 | 91.6 ms |
| 28 | 1344 | 4096 / 85.3 | about 52 | |
| 29 | 1392 | 8192 / 170.7 | about 136 | |
| default (40) | 1920 | 8192 / 170.7 | about 125 | 151.1 ms |

Points that follow from the table:

- Within a ring bucket, a smaller `-L` gives both lower latency and
  more tolerance. The useful choices are `-L 8`, `-L 15`, `-L 29`, and
  the default.
- At `-L 10` the measured delay of 54.4 ms implies a ring that was
  nearly full (about 38 of 42.7 ms) [D, I]. At that setting the ring
  size, not the start-up fill, limits the tolerance, so prefill adds
  little. To get more than about 30 ms of tolerance, `-L` has to move to
  the next bucket.
- At the default latency the measured delay implies a fill of about
  105 ms, or about 65 ms of tolerance [D, I]. Prefill can raise this
  toward 125 ms at the cost of up to about 65 ms more delay.

### 3.3 What happens after an underflow

Reasoning from the code [C, P, I]:

1. The decode thread stalls for S ms while the ring holds F ms. If S is
   less than about F − R, nothing is heard.
2. Otherwise the callback outputs zeros for about S − (F − R) ms.
3. When the thread runs again, `DataBuffer` holds S ms of IQ blocks. The
   decoder is many times faster than real time, so it writes until the
   ring is full or the backlog is gone. The ring fill after recovery is
   min(ring, S), which is at least what it was before the stall. One
   underflow does not leave the next stall with a smaller margin.
4. The audio is now later by the length of the gap. Nothing removes
   this delay except clock drift or the 1024-block queue cap (the old
   review's "latency ratchet", P5).
5. With `--portaudio-prefill-adapt`, `adapt()` runs right after the
   write that reported the underflow. At that moment the ring is nearly
   empty, so it writes 10 ms of zeros (`sfmbase/AudioOutput.cpp:425-445`)
   [C]. That adds a second gap right after the first one and 10 ms of
   permanent delay. The backlog refill in step 3 would have refilled the
   ring anyway, so the extra margin from adapt matters only when the
   stall was shorter than the ring. At `-L 8` to `-L 14`, where the ring
   is the limit, it mostly adds delay.

## 4. Possible reasons in detail

### 4.1 R1: late wake-up of the decode or USB threads

The wake chain for an Airspy HF+ block is: USB completion, then the
libusb/libairspyhf thread calls `rx_callback`, then
`DataBuffer::push()` notifies the condition variable, then the decode
thread wakes, decodes, and calls `Pa_WriteStream()`. Each step is a
non-real-time thread wake-up.

QoS as implemented [C]:

| Thread | QoS |
|---|---|
| main / decode | USER_INTERACTIVE (`main.cpp:342-349`) |
| signal thread | UTILITY (`main.cpp:284-289`) |
| StatusPrinter | UTILITY (`sfmbase/StatusPrinter.cpp:67-70`) |
| source `std::thread` | inherits USER_INTERACTIVE [I] |
| libairspyhf 1.8.1 / libairspy 1.0.12 threads, which call `rx_callback` | created with default attributes from the source thread, so they inherit USER_INTERACTIVE [I] |
| libusb event thread | created during device open from the main thread; inherits [I] |
| CoreAudio IO thread | real-time, owned by CoreAudio |

Inheritance into library threads is not verified. The binary was not
run.

Why browser and graphics load matters [I]:

- Browser UI, compositor, and GPU-process threads, and WindowServer,
  run at USER_INTERACTIVE or higher. Raising this program to the same
  class removes the gap to them but gives it no priority over them.
- On a busy SoC, GPU-heavy work also uses memory bandwidth and changes
  CPU frequency and power states. That lengthens the time from "thread
  runnable" to "thread running".
- E-core placement is less likely at USER_INTERACTIVE, but Blue Cat
  Audio reports that on Apple Silicon since macOS Ventura, high-priority
  audio threads that are not in a workgroup can be placed on efficiency
  cores and miss deadlines. It reports this even under low CPU load
  [W]. That report concerns parallel render threads in plug-in hosts, so
  how far it applies here is [I].

Check: Instruments System Trace on the running process during a load
burst. Look at the decode thread and the libairspyhf thread for
runnable-but-not-running intervals longer than the section 3.2
tolerance, and for the core type each ran on. Check the per-thread QoS
there as well.

### 4.2 R2: margin too small for the chosen `-L`

See section 3.2. The user's `-L` setting is not recorded in this
report. If it is in `-L 4` to `-L 7`, a stall of about 10 ms is enough.
That is a common scheduling delay under heavy load.

### 4.3 R3: CoreAudio IO-cycle overload

`kAudioDeviceProcessorOverload` is "sent when the AudioDevice detects
that an IO cycle has run past its deadline" (SDK `AudioHardware.h:921-925`).
PortAudio turns it into the same underflow bit (section 1.1) [P]. In
this case the IO thread itself was late, not this program's writer. The
IO callback runs in this process, but it only copies from the ring, so
its own cost is small [I]. Users on the Apple developer forums report
coreaudiod messages such as "skipping cycle due to overload" and
`HALS_OverloadMessage` with a client IO duration and scheduler latency.
They link these to memory pressure, USB hubs and cables, and other
devices on the same bus [W]. A larger ring does not help with this kind
of event.

Check: run `log stream --process coreaudiod --info` (or filter for
"overload") during a session, and compare the timestamps with `UF=`
increments on the `-v` line. This command has not been tried here; the
message wording may differ between macOS versions.

### 4.4 R5: the start-up underflow

The ring starts full of zeros at stream start and drains while the
source opens and starts. Airspy HF+ start-up can take longer than 21 or
43 ms. The first write then reports an underflow, and `write()` counts
it (`sfmbase/AudioOutput.cpp:330-335`) [C]. Only `adapt()` skips it
(`:352`). Check: a run on an idle machine shows `UF=1` (or 0) shortly
after start, and no increase afterward.

### 4.5 R4: allocations and page faults under memory pressure

Per-block heap work [C]:

| | Airspy HF+ | Airspy R2 | RTL-SDR |
|---|---|---|---|
| IQ block | 2048 samples, 16 KB | 65536, 512 KB | 16384, 128 KB |
| Period | 5.3 ms | 6.55 ms | 14.2 ms |
| New source vector per callback | `AirspyHFSource.cpp:419-431` | `AirspySource.cpp:497-509` | `RtlSdrSource.cpp:390-401` |
| `IfResampler` temporaries (`IfResampler.cpp:46-49`) | none (no resampling at 384 kHz) | 2 x 512 KB | 2 x 128 KB |
| Audio frames per write | about 256 | about 314 | about 683 |

The R2 also frees the previous 512 KB block every iteration
(`main.cpp:1076`). macOS serves allocations of this size from fresh
zeroed pages, so each costs page faults [I]. Under memory pressure (a
browser with many tabs) these can take longer [I]. Forum users report
crackling tied to memory pressure in the yellow range [W]. Exposure is
highest for the R2, moderate for RTL-SDR, and low for the HF+.

### 4.6 R6: SDR-side loss

USB data that arrives late at the device side is reported as `SDRdrop`
(the `dropped_samples` field of libairspy/libairspyhf) [C]. It sounds
like an underflow but is a different counter. Whether libairspy reports
a per-transfer count or a running total is still unconfirmed
(`doc/BUGS_20261017.md`). RTL-SDR reports no drops.

### 4.7 R7: device buffer size changed by another client

PortAudio sets `kAudioDevicePropertyBufferFrameSize` on the device and
listens for changes only to update timestamps; it never resizes its ring
(`H/pa_mac_core.c:1089-1125`) [P]. If a browser playing audio on the same
device could enlarge R for this process, ring − R would shrink without
notice. The CoreAudio header does not say whether this property is per
process (`AudioHardware.h:945-946`), and this report did not settle it.
Low likelihood. Check: read the property for the stream's device
(`PaMacCore_GetStreamOutputDevice()` plus `AudioObjectGetPropertyData`)
before and after starting audio in a browser.

### 4.8 Others, unchanged from the old review

- HAL plug-ins (SoundSource and others) added 28 to 44 ms and spurious
  steps [D].
- A DAC whose nominal rate is not 48 kHz makes AUHAL insert a converter
  [D, P].
- SDR and DAC clock drift (+3.6 to +4.1 ppm) is load-independent and
  slow [D].
- Priority inversion: with `-v` only, the UTILITY-class printer takes
  the `DataBuffer` mutex three times per print
  (`sfmbase/StatusPrinter.cpp:124-125`) [C]. Each hold is a few
  instructions, so the risk is low.

## 5. Telling the reasons apart with the current build

No code change is needed for these steps:

1. Run with `-v` on an idle machine for 10 minutes. Record `UF=` at
   start and at the end. Any increase on an idle machine is not caused
   by load.
2. Run the same configuration while using the browser. Each time an
   audible event happens, note `UF=`, `Ring=`, the queue size, and
   `SDRdrop`.
   - `UF` rises, the queue size jumps by several blocks, and the ring
     drops: the writer was late (R1/R2).
   - `UF` rises, no queue jump, and the ring is not low: suspect a HAL
     overload (R3). Confirm with the coreaudiod log (section 4.3).
   - `SDRdrop` rises: SDR-side loss (R6).

   `Ring=` overstates the fill by about R plus the device latency,
   because the capacity estimate is `outputLatency * rate`
   (`sfmbase/AudioOutput.cpp:259-265`), which includes more than the ring
   [C, P]. It is also sampled right after each write, so the maximum it
   reports is the peak and the trough is not shown [C].
3. Keep Activity Monitor's memory pressure graph visible during the
   test (R4).
4. For a definitive answer, record an Instruments System Trace during a
   load burst (section 4.1).

## 6. Workarounds without code changes

| Workaround | Effect | Cost | Evidence |
|---|---|---|---|
| Use `-L 8` instead of `-L 4` to `-L 7`, and `-L 15` instead of `-L 9` to `-L 14` | Largest tolerance per bucket (section 3.2) | `-L 8` adds about 17 ms of delay over `-L 5` | [P, D, I] |
| `-L 15` to `-L 20` when 30 ms of tolerance is not enough | About 60 to 65 ms of tolerance | About 90 to 100 ms total delay | [P, D, I] |
| Default latency when gaps matter most | About 65 ms of tolerance measured as delivered, up to about 125 ms | About 150 ms delay | [D, I] |
| `--portaudio-prefill` set to about ring − R − one block (30 ms at `-L 8`, 65 ms at `-L 15`, up to 100 at the default) | Makes the margin independent of source start-up time | Adds the prefill to the delay, up to the ring limit | [C, I]; not field-tested |
| Do not rely on `--portaudio-prefill-adapt` at `-L 8` to `-L 14` | Avoids an extra 10 ms gap and 10 ms of delay per underflow (section 3.3) | none | [C, I] |
| Play directly to the DAC; quit HAL plug-ins | Removes extra buffering and the plug-in's own scheduling | Loses the plug-in's features | [D] |
| Set the DAC's nominal rate to 48 kHz in Audio MIDI Setup | No converter in the IO callback | none | [D, P] |
| Connect the SDR and the DAC to separate ports, without a shared hub; try another cable | Addresses USB-side overloads | none | [W] |
| Keep memory pressure low (fewer tabs; quit memory-heavy apps) | Addresses R3 and R4 | Habit | [W, I] |
| Turn off hardware acceleration in the browser, as a test | Moves browser load from the GPU to the CPU; reported to help in one anecdote | Browser performance | [W], anecdotal; may make CPU contention worse |
| `taskpolicy -b -p <pid>` on a heavy background process (not the browser in use) | Puts that process into the Darwin background band (E-cores, throttled) | That process slows down; undo with `-B` | man page; [I] for the effect here |
| `caffeinate -i` | Rules out idle-sleep effects | Mac stays awake | [I] |

`sudo nice` is a weak lever next to QoS on Apple Silicon and is not
recommended [I]. Raising the program above USER_INTERACTIVE is not
possible with QoS. The next step up is the time-constraint policy
(section 7), which needs code.

## 7. Code-level mitigations

None of these was implemented in 20261017-0; ranks 1 to 3 have since
been implemented (section 7.1). Ranked by expected benefit against cost. "mac" marks macOS-only code,
which goes under `#ifdef __APPLE__`. None of them touches
`OPTIMIZATION_FLAGS`.

| Rank | Change | Benefit | Cost | Notes |
|---|---|---|---|---|
| 1 | Telemetry fixes: (a) count the start-up underflow separately; (b) sample free space before each write and report the minimum fill (the trough); (c) compute capacity from PortAudio's ring formula instead of `outputLatency`; (d) mac: log the device buffer size once a second (R7) | Turns the field test into a diagnosis; makes the prefill clamp correct | Small, mostly portable | Callback mode is the only way to separate HAL overloads from ring shortfall exactly |
| 2 | mac: `THREAD_TIME_CONSTRAINT_POLICY` for the decode thread, with period = block period, computation about 1 ms, constraint about one period | The only scheduling class that ranks above USER_INTERACTIVE browser threads [I]; targets R1 directly | About 20 lines; values depend on the source, block size, and `-E` | The kernel demotes a thread that exceeds its declared computation, so the values need margin. Blue Cat Audio found QoS and real-time attributes alone ineffective for its parallel render threads [W]; that case differs (threads tied to the IO cycle), but it is a reason to measure rather than assume. The USB-side thread may need the same treatment, since it is part of the wake chain. |
| 3 | mac: join a workgroup after rank 2. Either the device's IO workgroup (`PaMacCore_GetStreamOutputDevice()` → `AudioObjectGetPropertyData(kAudioDevicePropertyIOThreadOSWorkgroup)` → `os_workgroup_join`), or, since the decode deadline is not the IO cycle, an interval workgroup (`AudioWorkIntervalCreate`, `os_workgroup_interval_start`/`finish` per block) | Apple's documented way to tell the scheduler about real-time audio threads on Apple Silicon [W] | Medium; links CoreAudio.framework; joining needs the time-constraint policy first (EINVAL otherwise, per a JUCE forum report) [W] | PortAudio master has `PaMacCore_GetOSWorkgroup()` (PR #1049, merged 2025-07-31), but 19.7.0 does not [W]. The old review's claim that the device ID is not exposed was wrong: `PaMacCore_GetStreamOutputDevice()` exists in 19.7.0 (`pa_mac_core.h:109`). |
| 4 | Callback output with an application ring, fades, and exact counting (revival of the reverted code, section 8) | Replaces two clicks per underflow with a short fade-out/fade-in; cushion independent of `-L`; exact counts | About 250 lines; real-time code; needs testing on every platform | Reduces what is heard, not how often the stall happens |
| 5 | Adaptive prefill fixes: skip the silence step when the backlog refill will fill the ring anyway; optionally shrink slowly after a quiet period | Avoids the extra 10 ms gap (section 3.3) | Small | Worth doing only if the feature is kept |
| 6 | mac: set QoS inside `rx_callback` on its first call | Covers the case where library threads did not inherit USER_INTERACTIVE | Trivial | Only if a trace shows they did not inherit it |
| 7 | Atomic copies of the `DataBuffer` counters for the printer | Removes the only lock shared with a UTILITY thread | Small | `-v` only; low benefit |
| 8 | Preallocated IQ block pool and hoisted `IfResampler`/`AudioResampler` scratch buffers | Removes per-block large allocations (R4) | Medium; changes `DataBuffer` and all sources | Mainly for the Airspy R2 and RTL-SDR |
| 9 | Backlog trimming (old P5) | Bounds the delay that accumulates after stalls | Medium; a second discontinuity | Does not prevent underflows |

Rejected again: `mlock`/`mlockall`, `-ffast-math` (forbidden), smaller
write sizes, and a separate output thread (it helps only with a
stronger scheduling class than the decode thread, which rank 2 gives
directly).

### 7.1 Implementation of ranks 1 to 3

Implemented on branch `dev-macos-realtime-thread` (CHANGES.md entry dev
20261019). Decoded audio is unchanged: file decodes of
`test-files/piano_iqtest.wav` are byte-identical to `9570a05` with and
without `--realtime`. The `-P` path is build-verified only.

- Rank 1:
  - start-up underflows are counted separately (`UF=n(+s)` on the `-v`
    line, `(+s at start)` in the exit summary);
  - the fill before each write is sampled, and its minimum is shown as
    `Trough=` and `min ring fill=`;
  - on macOS the capacity follows PortAudio 19.7.0's ring formula, using
    the device buffer read with `PaMacCore_GetStreamOutputDevice()` and
    `AudioObjectGetPropertyData()`;
  - on macOS the device IO buffer size is shown as `IOBuf=`, with a
    one-time warning if it changes.
- Ranks 2 and 3: the long option `--realtime` (macOS only, default off).
  - The decode thread gets `THREAD_TIME_CONSTRAINT_POLICY` with period
    and constraint equal to one IF block and computation a quarter of
    it (1.333 ms of 5.333 ms for 2048 samples at 384 kHz), preemptible.
  - It then joins an interval workgroup made with
    `AudioWorkIntervalCreate()`. One option covers both, since the join
    needs the policy.
  - Each interval starts when a block is received and finishes before
    the output write, not after it: the write can sleep in PortAudio's
    5 ms poll while the ring is full, which is not computation.
  - On a file source, the policy and the join succeeded with no interval
    errors.

Not covered: the USB-side thread does not get the policy, and for a
file source with a custom `blklen` the declared period is wrong. The
corrected capacity formula predicts that `-L 8` may give a 1024-frame
ring instead of the 2048 assumed in section 3.2, since PortAudio
subtracts the device's fixed latency from the request; the earlier
measurements point to 2048 on the FiiO K7. `IOBuf=` and `Ring=` on a
field run will show which applies.

## 8. Reviving the reverted callback code (`39a4cff`)

### 8.1 What it was

`39a4cff` (2025-09-06) reverts the merge `2bea2e0` (PR #87, "Use our own
callback code for PortAudio output"; branch history `5a88090` to
`cb6b4fa`). It removed `pa_ringbuffer/` (PortAudio's
`pa_ringbuffer.c/.h` and `pa_memorybarrier.h`), the callback in
`sfmbase/AudioOutput.cpp`, and related CMake, README, and CHANGES text:
9 files, 770 lines removed.

Final design at `2bea2e0` (`git show 2bea2e0:sfmbase/AudioOutput.cpp`) [C]:

- Ring: `PaUtilRingBuffer` of 65536 frames x channels (1.365 s), empty at
  start.
- Latency: `defaultHighOutputLatency`, or `defaultLowOutputLatency` with
  the `PA_LOW_LATENCY` build flag. The 0.1 s floor from `419bc66` was
  removed again. There was no `-L` option then. `framesPerBuffer`
  unspecified, `paClipOff`.
- Callback: copies `min(available, needed)` samples and zeroes the rest
  (`memset`; the zeroing bug was fixed in `ca371d4`). No underflow
  count, no fade.
- `write()`: writes the block if it fits; otherwise it records
  "PaUtil_WriteRingBuffer() overflow" and returns `false`, which ends
  the program.
- Close: stops and closes the stream without draining the ring.
  `m_closed` is a plain `bool` read by the callback thread, a minor data
  race.

The old review (section 7, P8) described this attempt partly from
`367e281` ("`suggestedLatency` fixed at 0.2 s"). The final state differs
as listed above.

### 8.2 Why it clicked and did not reduce latency

[I], from reading the code:

1. **No cushion was ever built.** The ring started empty. The writer
   added one block per source block, and the callback took one device
   buffer per IO cycle. Production equaled consumption, so at callback
   time the fill was between zero and about one block. That is often
   less than one device buffer, so many callbacks zero-filled part of
   their output. Small, frequent underflows with hard edges would sound
   like clicks. The blocking path has no such problem only because
   PortAudio fills its ring with zeros at start.
2. Every shortfall was a hard cut to zero, with no fade.
3. Latency did not drop because the device buffer
   (`defaultHighOutputLatency`) still set the delay. The app ring added
   nothing useful, since it had no target fill.
4. A long stall followed by a burst could fill the 1.365 s ring, and
   `write()` then failed fatally instead of waiting.

### 8.3 What a revival needs

The 2025 design is not usable as it was. A revival would keep the idea
(a callback stream fed from an application ring) and change the rest:

- **Stream.** Callback mode, with `suggestedLatency` = `-L` setting only
  the device buffer. PortAudio adds no blocking ring in callback mode, so
  `-L` no longer sets the cushion.
- **Ring.** A small C++ single-producer, single-consumer ring of floats
  with acquire/release atomics, sized independently of the device
  buffer, for example max(target + 4 blocks, 200 ms). This avoids
  copying `pa_ringbuffer` back in. That code is under PortAudio's
  MIT-style license and could be reused with its notice, but about 30
  lines of `std::atomic` code do the same job.
- **Target fill T** in milliseconds, as an option (it could replace
  `--portaudio-prefill`).
- **Callback states:**
  - Priming: output silence until the fill reaches T. Not counted as an
    underflow.
  - Running: copy. When the fill is short, fade out over the last 2 to
    5 ms that are available, zero the rest, count one underflow, record
    the missing frames, and switch to Underrun.
  - Underrun: output silence until the fill reaches T (or T/2), then
    fade in over 2 to 5 ms and return to Running.
- **`write()` waits, never fails, when the ring is full.** A 1 ms sleep
  loop is the simplest real-time-safe choice, because the callback must
  not signal a condition variable. It checks a stop flag so SIGTERM
  still works.
- **Slow drain of excess fill.** If the fill stays above T plus a
  hysteresis for more than a second, adjust the audio resampler ratio
  by a few tens of ppm to remove it without a discontinuity. This also
  solves the old P5 latency ratchet and the clock drift.
- **Adaptive T.** Raise T by 10 ms after an underflow, up to a cap, and
  lower it slowly after a quiet period.
- **Exact telemetry.** Fill before and after each write, minimum fill,
  underflow count, and missing frames. The callback's `statusFlags`
  include HAL overloads, so the two event kinds can be separated: if the
  flag is set while the ring had enough data, it was a HAL overload.
- **Close.** Set a flag, wait for the callback to acknowledge it, and
  optionally drain up to T.
- **Channel count** from the stream (the 2023 attempt hard-coded 2).

Interaction with today's code [C]: the telemetry fields
(`min_free_frames`, `capacity_frames`), `prime()`, and `adapt()` are
built around `Pa_WriteStream()` and `Pa_GetStreamWriteAvailable()`, and
would be rewritten against the app ring. The meaning of `-L` changes,
and the prefill options change or go away. QoS, StatusPrinter, and the
`AudioOutput` interface (`write()`, `telemetry()`) can stay as they are.

### 8.4 Would it reduce the disruption?

- **Underflow frequency:** no, by itself. In both designs the decode
  thread must refill the ring before it runs short, and the tolerance is
  the fill minus the device buffer. A callback design does not make the
  thread wake sooner. Its one structural gain is that the cushion is not
  tied to `-L`. For example, it can hold a 60 ms cushion on an 8 ms
  device buffer, about 70 ms in total. The blocking path needs `-L 15`
  and about 90 to 100 ms for the same tolerance (section 3.2).
- **What is heard:** yes. A 2 to 5 ms fade on each side of a gap is
  much less audible than two hard edges. This is the main reason to do
  it.
- **Diagnosis:** yes. Exact counts and the HAL-overload separation come
  for free.
- **Cost and risk:** about 250 lines of real-time-sensitive code. The
  new path would replace PortAudio's tested blocking layer on all
  platforms (Linux ALSA and PulseAudio behave differently), and there
  are no automated tests, so it can only be verified on hardware.

Recommendation: rank 4 in section 7. Do it after the telemetry fixes
and the time-constraint trial, if the field data show that underflows
still happen at an acceptable `-L`. If it is done, make it a new
selectable output mode (for example, keep `-P` blocking and add an
option for callback mode) for one release, so the two can be compared
on the same hardware.

## 9. Corrections to the 2026-10-10 review

1. Section 3 and section 4.2: B is PortAudio's application-side ring, not
   the CoreAudio host cycle; the IO cycle is about R. The blocking write
   polls with a 5 ms sleep only when the ring is full.
2. Section 3: occupancy is not only start-up luck. After a stall long
   enough to cause an underflow, the backlog refills the ring (section
   3.3).
3. Section 7, P7: PortAudio 19.7.0 does expose the device ID
   (`PaMacCore_GetStreamOutputDevice()`), and PortAudio master has a
   workgroup accessor.
4. Section 7, P8: the final reverted design used
   `defaultHighOutputLatency` with no floor, not a fixed 0.2 s.

## 10. Open questions

1. Which `-L` and prefill options were in use when the underflows were
   seen, and which SDR (HF+, R2, or RTL-SDR)?
2. Does `UF=` rise on an idle machine (start-up event, drift), or only
   under load?
3. Do coreaudiod overload messages coincide with the `UF=` increments?
4. What QoS and core type do the decode thread and the libairspyhf
   thread actually run at (System Trace)?
5. Does the device buffer size seen by this process change when a
   browser starts audio on the same device?
6. Does libairspy report `dropped_samples` per transfer or as a running
   total?

## 11. Sources

PortAudio source and project:

- PortAudio v19.7.0, `src/hostapi/coreaudio/pa_mac_core_blocking.c`,
  `pa_mac_core_blocking.h`, `pa_mac_core_utilities.c`, `pa_mac_core.c`
  (tag `v19.7.0`, https://github.com/PortAudio/portaudio).
- PortAudio PR #1049, "Add function PaMacCore_GetOSWorkgroup()", merged
  2025-07-31: https://github.com/PortAudio/portaudio/pull/1049
- macOS SDK `CoreAudio.framework/Headers/AudioHardware.h`
  (`kAudioDeviceProcessorOverload`, `kAudioDevicePropertyBufferFrameSize`,
  `kAudioDevicePropertyIOThreadOSWorkgroup`).

Apple documentation:

- Porting your audio code to Apple silicon (section "Prioritize Realtime
  Threads Using Workgroups"):
  https://developer.apple.com/documentation/apple-silicon/porting-your-audio-code-to-apple-silicon
- Adding Parallel Real-Time Threads to Audio Workgroups:
  https://developer.apple.com/documentation/audiotoolbox/adding-parallel-real-time-threads-to-audio-workgroups
- Workgroup Management:
  https://developer.apple.com/documentation/audiotoolbox/workgroup-management
- WWDC 2020, Meet Audio Workgroups:
  https://developer.apple.com/videos/play/wwdc2020/10224/

Developer reports (anecdotal):

- Blue Cat Audio, "Realtime Audio Multicore Issues for Apple Silicon:
  End of the Story?":
  https://www.bluecataudio.com/Blog/preview/realtime-audio-multicore-issues-for-apple-silicon-end-of-the-story/
- JUCE forum, "Os_workgroup_join consistently returns EINVAL":
  https://forum.juce.com/t/os-workgroup-join-consistently-returns-einval-cant-join-audio-workgroup/54240
- Apple Developer Forums, "What causes 'issue_type = overload' in
  coreaudiod with USB audio interface?":
  https://developer.apple.com/forums/thread/677383
- Apple Developer Forums, "How to Fix Cracking and Popping Sound?":
  https://developer.apple.com/forums/thread/132423
- Apple Community, "Dropouts of Audio via USB to a DAC":
  https://discussions.apple.com/thread/255750846
- Spotify Community, recurring CoreAudio client timeouts:
  https://community.spotify.com/t5/Desktop-Mac/Spotify-desktop-app-causes-recurring-CoreAudio-client-timeouts/td-p/7521434
- Damus post on browser hardware acceleration and coreaudiod:
  https://damus.io/nevent1qqs27tx2hpjeazye5wp4d79q8g3d37rcnzpfdrqdlr5l6njg4le3y2c58z8nn
