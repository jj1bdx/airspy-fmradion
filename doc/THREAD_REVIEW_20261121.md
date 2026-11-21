# Review: macOS and Linux behavior, with the focus on `--realtime` (2026-11-21)

Branch: `dev-macos-realtime-thread` at `fb10012`.
Method: a read-only source review by the cpp-expert agent. Nothing was
built, run, or changed. I checked these claims against the code: the
list of platform macros, the absence of Linux scheduling APIs, the
Linux stubs in `sfmbase/RealtimeThread.cpp`, the decode-thread start
in `main.cpp`, the computation value, `RealtimeThread::policy_held()`,
the start of `FileSource::run()`, the `tick()` call sites, and the two
CHANGES.md entries. The
other line references come from the agent and were not checked one by
one.

Related documents: doc/THREAD_OPTIMIZATION_20261114.md,
doc/UNDERFLOW_RESEARCH_20261019.md,
doc/LINUX_PORTAUDIO_RESEARCH_20261019.md.

## 1. Summary

- The only platform condition in the C++ sources is `#ifdef __APPLE__`
  (main.cpp:36, 294, 411; AudioOutput.cpp:29, 38, 283, 307, 529;
  StatusPrinter.cpp:21, 70; RealtimeThread.cpp:29, 41, with the
  `#else` at 636). There is no `__linux__`, `__MACH__`, `TARGET_OS_*`,
  or `_WIN32`. CMakeLists.txt uses `CMAKE_SYSTEM_NAME STREQUAL
  "Darwin"` and `if(APPLE)`.
- There is no Linux scheduling code: no `sched_setscheduler`,
  `SCHED_FIFO`, `SCHED_RR`, `pthread_setschedparam`, `setpriority`,
  `mlockall`, `prctl`, or rtkit anywhere in the repository.
- On Linux, `--realtime` is accepted and changes no scheduling. Its
  only effect is one warning, "WARNING: --realtime is supported on
  macOS only; ignored" (RealtimeThread.cpp:644-648), printed when the
  first block arrives.
- On macOS, `--realtime` applies the Mach time-constraint policy to
  the decode thread and to the SDR source threads. The decode thread
  also joins an interval workgroup when the output is PortAudio
  (`-P`). The PortAudio threads are not touched.
- README.md:149 ("ignored with a warning on other platforms") matches
  the code.

## 2. `--realtime` on macOS and Linux

| Item | macOS | Linux |
|---|---|---|
| Option parsing | Long option `OPT_REALTIME`, no argument (main.cpp:320, 518); sets `realtime = true` (main.cpp:612). Help text: "experimental, macOS only; ignored on other platforms" (main.cpp:133-137). | The same; accepted, not rejected. |
| Decode (main) thread | `RealtimeThread::start()` runs on it when the first non-empty block arrives (main.cpp:1163-1170). | `start()` prints the warning and returns false; the object is released (main.cpp:1167-1169). |
| SDR source threads | A `ProducerPolicy`, created by `Source::make_producer` when `m_realtime` is set (Source.h:105-110). Airspy and Airspy HF: the two threads created inside `airspy_start_rx()` and `airspyhf_start()` (AirspySource.cpp:448-456, AirspyHFSource.cpp:382-390). RTL-SDR: the `run` thread that calls `rtlsdr_read_async` (RtlSdrSource.cpp:363-370). File source: its `run` thread (FileSource.cpp:415-420). | A `ProducerPolicy` is still created, but every method is an empty stub (RealtimeThread.cpp:660-686). |
| Threads not covered | The PortAudio blocking write (it runs on the decode thread, which has the policy), the status printer (QoS UTILITY, StatusPrinter.cpp:70-72), the signal thread (QoS UTILITY, main.cpp:294-297), and the libusb event thread of the Airspy libraries (README.md:149). | All threads use default scheduling. |
| Policy | `THREAD_TIME_CONSTRAINT_POLICY` with period = constraint = one block period, computation = period / 2 clamped to 50 µs to 50 ms, `preemptible = TRUE` (ignored by the kernel) (RealtimeThread.cpp:44-81). Periods above about 4.29e9 Mach ticks are rejected (RealtimeThread.cpp:45, 66-69). | None. |
| Period | Decode thread: first block size divided by the source rate (main.cpp:1163-1169); block sizes are 65536 (Airspy), 2048 (Airspy HF), 16384 (RTL-SDR), and 2048 (file source) (main.cpp:890-903). Source threads: 65536 / rate (AirspySource.cpp:430), `airspyhf_get_output_size()` / rate (AirspyHFSource.cpp:362-366), and the block length / rate (RtlSdrSource.cpp:324-325, FileSource.cpp:386-387). | — |
| Failure handling | The program never stops. An invalid period or timebase, or a failed `thread_policy_set`, prints a warning and decoding continues without the policy (RealtimeThread.cpp:168-188). A failed readback prints the requested values (191-195). Source threads: the failure message is stored and printed by `tick()` (428-436, 585-589). If the thread-list comparison does not find exactly 2 new threads, a warning is printed and the policy goes to the first callback thread only (466-516, 541-555). The ready wait is 2 s and only warns on timeout (Source.h:85-89, RealtimeThread.cpp:527-539). | `policy_held()` always returns true (RealtimeThread.cpp:654); `wait_ready()` returns true at once (RealtimeThread.cpp:678). |
| Later loss of the policy | The decode thread checks itself every 2 s (RealtimeThread.cpp:279-296); source threads in `tick()` and `check()`. Shown as `RT=LOST` in the `-v` status (StatusPrinter.cpp:107-113, 213-215), in the exit summary (main.cpp:398-403), or as a warning with `-q` (main.cpp:1436-1441). A failed query counts as held (RealtimeThread.cpp:295-296). | — |
| Workgroup | Only with `-P`: `AudioWorkIntervalCreate("airspy-fmradion decode", OS_CLOCK_MACH_ABSOLUTE_TIME)` and `os_workgroup_join` on the decode thread (main.cpp:1164-1166, RealtimeThread.cpp:200-224). Each block starts an interval when it is received and finishes it before the output write (main.cpp:1182, 1274). Errors are counted and printed at exit (RealtimeThread.cpp:141-146, 267-272). It is a workgroup of this program, not PortAudio's device workgroup. Source threads do not join it (RealtimeThread.h:98-100). | Nothing equivalent. |
| Messages | Startup: `realtime: kernel period ..., computation ..., constraint ..., workgroup joined / not joined / not used` (RealtimeThread.cpp:226-235), not printed with `-q`. Per source: `realtime: <name>: policy on N thread(s) (...)` (RealtimeThread.cpp:591-598). Exit: the decode-block summary with `realtime policy held` or `LOST` (main.cpp:395-403) and the producer-thread summary (RealtimeThread.cpp:626-632, main.cpp:1430-1434). `-v` shows `DecC=` and `RT=`. | Only the one warning, printed also with `-q`. No `DecC=` or `RT=` (the computation value is 0; main.cpp:1173, StatusPrinter.cpp:208-210). |
| Dependence on `-P` and the source | The workgroup is used only with `-P`; otherwise the startup line says `workgroup not used` (main.cpp:1164-1166). The decode-thread policy applies to every source and output. The source-thread method depends on the source type (see above). | None; always the same warning. |

## 3. Other macOS and Linux differences

- QoS of non-real-time threads: on macOS, `main()` sets
  `QOS_CLASS_USER_INTERACTIVE` before any other thread starts, so the
  later threads inherit it (main.cpp:411-418); the signal thread and
  the status printer are set to UTILITY. On Linux all threads use
  default scheduling.
- PortAudio host API: the code uses `Pa_GetDefaultOutputDevice()` or a
  device index and never selects a host API (AudioOutput.cpp:226-247);
  `hostApiSpecificStreamInfo` is NULL (AudioOutput.cpp:258-259). On
  Linux the host API is whatever PortAudio picks (ALSA, PulseAudio, or
  JACK).
- Output latency: the default `suggestedLatency` is
  `defaultHighOutputLatency`, raised to at least 40 ms, on both
  platforms (AudioOutput.cpp:254-265, AudioOutput.h:126-129); `-L`
  bypasses the floor. A header comment records 14.7 ms (macOS) and
  34.8 ms (Ubuntu) before the floor.
- `Ring=` capacity estimate: on macOS, when the device buffer size can
  be read, the estimate is PortAudio 19.7.0's ring formula
  `bit_ceil(max(latency * rate * 2 + 0.5, 3 * device_frames, 4))`
  (AudioOutput.cpp:49-54, 305-310); a comment says to recheck it when
  PortAudio is upgraded. On Linux it is `outputLatency * rate`
  (AudioOutput.cpp:298-302), which a code comment says overstates the
  ring (AudioOutput.cpp:296-297); observed free space raises it
  (AudioOutput.cpp:413-419).
- `--portaudio-prefill`: clamped to the capacity minus one block
  (AudioOutput.cpp:437-441). On Linux the overstated capacity makes the
  clamp weaker.
- `IOBuf=` and the "device IO buffer size changed" message: macOS only
  (AudioOutput.cpp:529-534, StatusPrinter.cpp:157-168).
- Clocks: both platforms use `gettimeofday`, `nanosleep`, and
  `std::chrono::steady_clock` (Utility.h:139-151, FileSource.cpp:441-463,
  DataBuffer.h:192, main.cpp:1180). Mach absolute time is used only in
  the macOS `--realtime` code.
- Build: on Darwin, CMake looks for `.dylib` files with `/usr/local` and
  `/opt/homebrew` hints; elsewhere for `.so` (CMakeLists.txt:51-174).
  Only macOS links `-framework CoreAudio` and `AudioToolbox`
  (CMakeLists.txt:338-341). Compiler flags are the same on both
  (`-Wall -std=c++20 -O3 -ftree-vectorize`, no `-ffast-math`). The
  VOLK check (MultipathFilter.cpp:183) and the MP3 detection
  (libsndfile ≥ 1.1) do not depend on the platform.
  `RealtimeThread.cpp` is compiled on both; on Linux only its stubs
  remain.
- On Linux, `ProducerPolicy::on_callback()` calls the empty
  `first_callback()` on every block, because `m_callback_seen` is never
  set (RealtimeThread.h, inline). The cost is one call and one atomic
  load per block.

## 4. Risks

Ranked by severity. None is fixed.

1. Medium, macOS, Airspy and Airspy HF: the policy can be applied to
   the wrong thread (AirspySource.cpp:448-456, AirspyHFSource.cpp:382-390,
   RealtimeThread.cpp:445-516). The library threads are found by
   listing the task's threads before and after `airspy_start_rx()` /
   `airspyhf_start()` and expecting exactly 2 new ones. A thread
   created by something else during that call can be counted, and it
   would then get the time-constraint policy without a warning. A count
   other than 2 only gives a warning and the fallback to the callback
   thread. The PortAudio stream is created before the source starts
   (main.cpp:811 and 983), which lowers the chance. CHANGES.md:57 says
   the Airspy and RTL-SDR paths were tested only with an imitation of
   the library threads, not on a device.
2. Medium, macOS, RTL-SDR and the file source: a lost source-thread
   policy is reported only at the end of the run. RtlSdrSource.cpp:365-368
   calls `tick()` once before it blocks in `rtlsdr_read_async`, so the
   next check is `check()` in `stop()`. The file source calls `tick()`
   once before its read loop (FileSource.cpp:415-420) and again with
   `check()` only when the thread ends (FileSource.cpp:480-484). The
   Airspy sources call `tick()` every 100 ms from their run loops
   (AirspySource.cpp:462-466, AirspyHFSource.cpp:399).
3. Medium, Linux: `--realtime` does nothing for scheduling
   (RealtimeThread.cpp:636-688). A user who expects `SCHED_FIFO` gets
   default scheduling, and a later underflow can be blamed on the wrong
   cause. The warning appears once, at the first block, and with `-v`
   it is easy to miss under the status line; there is no `RT=` field.
4. Low, macOS, file source: `FileSource::run()` returns before
   `mark_ready()` when `m_this` is null (FileSource.cpp:407-410). The
   main thread then waits the full 2 s and prints a timeout warning.
   RtlSdrSource.cpp:355-360 handles the same case correctly.
5. Low, macOS: after a partial `apply_new` failure, the fallback in
   `first_callback()` calls `apply_self` on the callback thread, which
   may already be in the list (RealtimeThread.cpp:497-504, 546-549).
   The summary then counts it twice.
6. Low, macOS: a failed policy query counts as held
   (RealtimeThread.cpp:295-296). This is intended (the comment says "a
   failed query is unknown, not a loss"), but a loss can go unreported
   while queries fail.
7. Low, macOS: the computation is half the period, the kernel's
   minimum (RealtimeThread.cpp:71-75). What the kernel does when a block
   takes longer is not stated in the code; doc/THREAD_OPTIMIZATION_20261114.md
   has the measurements.
8. Low, Linux: the `Ring=` and prefill clamp rely on an estimate
   (AudioOutput.cpp:296-302, 437-441). With `--portaudio-prefill 50` on
   a device whose real ring is smaller than `outputLatency * rate`, the
   first write can block until the device drains, which stalls the
   decode thread at startup. Not run.
9. Information, Linux: no equivalent of the macOS QoS settings for the
   status printer and the signal thread.

No `#ifdef __APPLE__` block lacks a needed `#else`: the one in
`RealtimeThread.cpp` has stubs (line 636), and the other blocks are
optional. No code outside the Apple-only headers compiles on only one
platform.

## 5. Documentation notes

- CHANGES.md:64 (the dev 20261019 entry) says the computation is a
  quarter of the constraint; CHANGES.md:53 (the dev 20261114 entry)
  says it is now half, which matches the code (RealtimeThread.cpp:74-75).
  The older entry describes the code of its time.
- The handoff's open items list a Linux `--realtime` implementation as
  not done; the code agrees.
