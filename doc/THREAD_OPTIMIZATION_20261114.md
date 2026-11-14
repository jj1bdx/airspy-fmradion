# Plan: optimizing the `--realtime` thread code for macOS (2026-11-14)

Status: steps 1 to 3 and the document corrections of section 5 are
implemented in `43f0a26`, and step 4 in the commit after `852c880`
(option B with a fallback to option A; see the notes under step 4 and
the `dev 20261114` entry in CHANGES.md). Steps 5 and 6 are not
implemented. Apart from those notes and section 8, the text below is
the plan as written before `43f0a26`, against `1a9e26e`. Section 8
explains the timing summary printed at exit, as of `f14bf4c`.
Branch: `dev-macos-realtime-thread`.
Scope: `include/RealtimeThread.h`, `sfmbase/RealtimeThread.cpp`, the
decode loop in `main.cpp`, `include/DataBuffer.h`, `StatusPrinter`, and
the SDR source classes.

The analysis was done by the cpp-expert agent on 2026-11-14 on the Mac
mini (M2 Pro, 6 P and 4 E cores, Darwin 27.2). It read the SDK headers,
the XNU, libpthread, libusb, libairspyhf, libairspy, and librtlsdr
sources from GitHub, and ran probe programs that do not touch audio.
`-P` was not run, and `build/` was not touched.

## 1. Findings the plan is based on

Each item is marked as verified (source, SDK header, or probe),
inferred, or open (needs a `-P` field test or Instruments System Trace).

### 1.1 Time-constraint policy

- Verified: the kernel raises `computation` to `constraint / 2` when the
  request is smaller (XNU `osfmk/kern/thread_policy.c`, and the SDK
  header `mach/thread_policy.h`). With the shipped parameters for the
  Airspy HF+ (period and constraint 5.333 ms, computation requested
  1.333 ms), `thread_policy_get` returned 2.667 ms. So
  `computation_fraction = 0.25` never takes effect, and the startup line
  prints a value the kernel does not use.
- Verified: `preemptible` is stored but not used by the scheduler. The
  SDK header marks it "IGNORED".
- Verified: `period` is not used for scheduling. It is passed to the
  performance controller only. The deadline at each wake-up is wake time
  plus `constraint`.
- Verified: real-time threads of equal priority run earliest deadline
  first. The quantum is the declared computation. A thread that uses up
  its quantum stays real-time but sorts last among real-time threads
  until it blocks.
- Verified: there is no demotion for a computation overrun on macOS. The
  only fail-safe is for a thread that runs for about 1 s without
  blocking. The decode thread blocks in `DataBuffer::pull()` every
  block, so it cannot trigger this unless decoding is slower than real
  time.
- Verified: a constraint of 4 ms or less makes the kernel send extra
  wake-up interrupts for the thread. The shipped 5.333 ms constraint
  does not reach this threshold.
- Verified: a computation below 50 us or above 50 ms, or a constraint
  smaller than the computation, is rejected with
  `KERN_INVALID_ARGUMENT`.
- Verified: `thread_policy_set` works on another thread of the same task
  through its Mach port. `THREAD_EXTENDED_POLICY` with `timeshare=TRUE`
  returns a thread to normal scheduling.

### 1.2 Workgroup

- Verified: `AudioToolbox/AudioWorkInterval.h` says a thread that runs
  at a cadence independent of the audio hardware should create its own
  interval workgroup with `AudioWorkIntervalCreate()`. The decode thread
  is paced by SDR block arrival, so the current choice is the documented
  one. Joining the device IO-thread workgroup
  (`kAudioDevicePropertyIOThreadOSWorkgroup`) is for threads triggered
  from the IO thread, which this is not.
- Verified: `os_workgroup_interval_start()` called from a thread that
  has not joined the workgroup aborts the process (SIGTRAP). The current
  call sites cannot do this, because `Cycle` objects exist only on the
  thread that called `start()` after a successful join.
- Verified: on Darwin 27.2 `os_workgroup_join()` succeeds without a
  real-time policy. XNU `main` still contains the check, and section 7
  of doc/UNDERFLOW_RESEARCH_20261019.md says the join needs the policy
  first.
- Verified: one interval start and finish pair costs about 1.5 us.
- Probe result: the workgroup gave no measurable wake-latency benefit.
  Idle median latency was 8 to 10 us higher with it; the cause is
  unknown.
- Open: whether the workgroup changes core placement or frequency under
  real load.

### 1.3 Producer side

- Verified (libairspyhf `airspyhf.c`): `airspyhf_start()` creates a
  transfer thread, which runs the libusb event loop, and a consumer
  thread, which converts samples and calls `rx_callback`. An 8-slot ring
  sits between them; a full ring drops the buffer and counts it.
- Grep only, not read in detail: libairspy has the same two-thread
  structure.
- Verified (librtlsdr): `rtlsdr_read_async()` runs the libusb event loop
  in the calling thread, so the `RtlSdrSource` thread is the callback
  thread.
- Verified (libusb `darwin_usb.c`): IOKit completions run on a private
  run-loop thread created at `libusb_init()`. The application has no
  direct handle to it.
- Result: for the HF+ the wake-up chain is the libusb run-loop thread,
  the transfer thread, the consumer thread (`rx_callback`,
  `DataBuffer::push()`), and then the decode thread. `--realtime` covers
  only the last one.
- Inferred: a late wake-up of a producer thread delays audio as much as
  a late decode thread. The 8-slot ring absorbs about 40 ms for the HF+
  before `SDRdrop` rises.

### 1.4 Locks and allocation

- Verified in libpthread source: a thread blocked on a pthread mutex
  raises the priority of the lock owner. Not confirmed: which of the two
  libpthread mutex implementations `std::mutex` uses by default; both
  were reported to behave this way. This contradicts the
  priority-inversion concern in section 4.8 of
  doc/UNDERFLOW_RESEARCH_20261019.md.
- Verified in code: `StatusPrinter::try_publish` uses `try_lock`. With
  `-v`, the printer thread takes the `DataBuffer` mutex three times per
  print, each for a few instructions.
- Inferred: per-block heap work on the decode thread (freeing the pulled
  block, `IfResampler` temporaries) is a larger risk than the mutexes,
  mainly for the 512 KB blocks of `AirspySource` and for RTL-SDR. It is
  low for the HF+ (16 KB blocks).
- Verified by probe: a timer sleep on a real-time thread is late by tens
  of microseconds, against about 1 ms (and occasionally several
  milliseconds) at user-interactive QoS. So PortAudio's `Pa_Sleep(5)`
  poll on a full ring is not a risk on the real-time thread.

### 1.5 Probe measurements

Wake latency from condition-variable notify to wake, 5.333 ms blocks:

| Load              | Mode                       | p99 (us) | max (us) |
| ----------------- | -------------------------- | -------- | -------- |
| none              | user-interactive QoS       | 27       | 59       |
| none              | real-time, as shipped      | 24       | 46       |
| none              | real-time plus workgroup   | 63       | 120      |
| 10 busy threads   | user-interactive QoS       | 79       | 420      |
| 10 busy threads   | real-time, as shipped      | 22       | 27       |
| 10 busy threads   | real-time plus workgroup   | 21       | 28       |
| 14 busy threads   | user-interactive QoS       | 115      | 185      |
| 14 busy threads   | real-time, as shipped      | 23       | 45       |
| 14 busy threads   | real-time plus workgroup   | 21       | 26       |

A constraint of 20 % of the period with a computation of 10 % gave the
same results as the shipped parameters.

All values are far below the ring tolerance of about 10 to 65 ms. The
synthetic load does not reproduce multi-millisecond stalls. The probe
shows that the policy works, not that it removes the reported
underflows.

## 2. Defects in the current code

1. `computation_fraction = 0.25` has no effect, and the startup line
   prints the requested computation, not the kernel's value.
2. The startup line and the header mention `preemptible`, which the
   kernel ignores.
3. The period is `if_blocksize / ifrate`. It is wrong for a FileSource
   with a custom `blklen` and for an RTL-SDR with a non-default block
   length, because `if_blocksize` in `main.cpp` does not follow the
   source's real block length.
4. `Impl::interval_error()` prints to stderr from the real-time thread.
   A stalled terminal could block it.
5. `stop()` returns early when no workgroup exists, which skips the
   error-count line. It also leaves the thread real-time through the
   exit sequence. This is harmless but unnecessary.
6. `to_abs` casts a double to `uint32_t` with no bound check. It wraps
   above about 178 s, which no realistic period reaches.

## 3. Plan

### Step 1: telemetry (first patch)

Purpose: get numbers that show whether `--realtime` changes anything,
with and without the option, and without `-P`.

Changes, shown on the `-v` status line and in the exit summary:

- `include/DataBuffer.h`:
  - Store a `mach_absolute_time()` value (or the existing
    `Utility::get_time()`) with each pushed block, and note whether the
    consumer was blocked in the wait at that moment.
  - In `pull()`, when the consumer was waiting, record return time minus
    push time as wake latency: maximum, counts above 1, 2, 5, 10, and
    20 ms, and a fixed log2 histogram of about 12 buckets.
  - When the queue was not empty, record the value separately as backlog
    age.
  - Record the gap between consecutive pushes: maximum, and counts above
    1.5 and 3 times the nominal block period. This shows stalls in the
    USB and vendor-library threads.
- `main.cpp` and `RealtimeThread`:
  - Record decode time per block (from after `pull()` to before the
    output write): maximum, mean, and counts above the kernel's
    computation value and above the period.
  - Every few seconds, call `thread_policy_get` for
    `THREAD_TIME_CONSTRAINT_POLICY`. If `get_default` is true, the
    thread is no longer real-time; report it once and at exit.
- `sfmbase/StatusPrinter.cpp`: print the new fields with `-v`.
- Optional: `os_signpost` intervals around each decode cycle, so that
  Instruments System Trace can align them with scheduling events.

Cost: a few clock reads (about 5 ns each) and one array increment per
block.

Risk: low. The counters must be readable from the printer thread without
adding a lock to the decode path; use atomics as `AudioOutput` telemetry
does.

### Step 2: parameter and reporting fixes (same patch as step 1)

- `sfmbase/RealtimeThread.cpp`:
  - Request `computation = constraint / 2` explicitly, so the code
    states what the kernel uses. Keep `constraint = period`.
  - After `thread_policy_set`, read the policy back with
    `thread_policy_get` and print those values in the startup line.
  - Remove `preemptible` from the startup line.
  - Add a bound check in `to_abs` (reject a period above 100 s).
- `include/RealtimeThread.h`: remove `computation_fraction` or replace
  its comment with the half-of-constraint rule.
- Do not reduce the computation or tighten the constraint yet. A
  computation smaller than the real decode time makes the thread sort
  last among real-time threads when the load is high, and the probe
  showed no gain from a tighter constraint. Revisit only if step 1 shows
  a wake-latency tail with `--realtime` on. In that case, try a
  constraint of 4 ms or less with the computation set from the measured
  decode time (for example twice the observed maximum), keeping
  `constraint >= 2 * computation`.

### Step 3: robustness fixes (same patch as step 1)

- Period: take it from the source's real block length. Either add a
  `Source` accessor for the block length, or compute the period from the
  first pulled block and apply the policy then.
- `interval_error()`: count in a member and print at exit only.
- `stop()`: print the error count before the early return, and restore
  normal scheduling with `THREAD_EXTENDED_POLICY`, `timeshare=TRUE`.
- `begin_cycle()`: add a comment or a debug assertion that it must run
  on the thread that called `start()`, because a call from another
  thread aborts the process.

### Step 4: producer threads (second patch, after step 1 data)

Do this when the push-gap counter or `SDRdrop` shows producer-side
stalls, or together with the first field test if the user prefers one
test round.

- `sfmbase/RtlSdrSource.cpp`: apply the time-constraint policy at the
  start of the source thread, before `rtlsdr_read_async()`.
- `sfmbase/AirspyHFSource.cpp` and `sfmbase/AirspySource.cpp`, two
  options:
  - A: apply the policy on the first `rx_callback` call. This covers the
    consumer thread only. Small change.
  - B: list the task's threads with `task_threads()` before and after
    the library's start call, and apply the policy to the new threads
    through their Mach ports. This also covers the transfer thread.
  - Recommendation: B, with A as the fallback if thread enumeration
    proves unreliable.
- Parameters for producer threads: constraint equal to the block period,
  computation at the kernel minimum of half the constraint. Do not join
  the workgroup from these threads; the probe showed no benefit, and
  each thread would have to join and leave by itself.
- The libusb run-loop thread stays uncovered. Record this as a known
  limit.
- A policy failure on any producer thread is a warning only.
- This needs a way to pass the `--realtime` setting and the block period
  to the source classes, for example through the existing `Source`
  configuration path.

Implementation notes (2026-11-14, added with the implementation):

- Implemented: B, gated and with A as the fallback. `ProducerPolicy` in
  `RealtimeThread.h` takes a `task_threads()` snapshot before and after
  the start call and applies the policy to the new threads only if there
  are exactly two (libairspyhf and libairspy each create a consumer and
  a transfer thread with `pthread_create()` before the start call
  returns). Any other count applies the policy to none of them; the first
  `rx_callback` then applies it to the consumer thread only (option A).
- The start call itself takes milliseconds before it creates the
  threads (`airspyhf_start()` and `airspy_start_rx()` first do USB
  control transfers). With `--realtime`, `main()` therefore calls
  `Source::wait_realtime_ready()` after a successful `start()` and waits
  at most 2 s for the source thread to finish deciding, so that it
  creates no thread (`StatusPrinter`) inside the snapshot window. Every
  source thread calls `ProducerPolicy::mark_ready()` after its decision,
  also when the start call or the policy failed. A timeout only prints a
  warning. Without `--realtime` there is no wait.
- `Source::set_realtime()` passes the flag before `start()`. Each source
  computes its own block period: block length divided by sample rate.
- FileSource also gets the policy, for timing tests (the blocks marked
  `--realtime` in `FileSource.cpp` can be deleted).
- Known limits: the libusb run-loop thread is not covered. A thread that
  an unrelated component creates in the same window makes the count
  differ and selects the fallback (for example CoreAudio or libdispatch
  threads with `-P`).

### Step 5: workgroup scope (small, with step 4 or later)

- Create and join the workgroup only when the output is PortAudio. For
  file output keep the time-constraint policy alone.
- Keep the workgroup as it is for `-P` until a System Trace shows
  whether it changes core placement. If it shows nothing, consider
  removing it.

### Step 6: deferred

- Block-vector recycling in `DataBuffer` and hoisting the `IfResampler`
  temporaries. Medium effort. Do it only if field data shows stalls with
  `AirspySource` or RTL-SDR.
- Replacing the printer's `DataBuffer` lock with atomic counters. Safe
  and small, low value.
- No change to the mutex type. `os_unfair_lock` gives nothing that could
  be verified over `std::mutex`.

## 4. Verification

For every patch:

- Build with no new warnings; `clang-format --dry-run --Werror` on
  changed files. Rebuilding `build/` replaces the binary the user may be
  running, so ask first or build in a separate directory.
- Make a baseline before editing, then compare byte for byte:
  `airspy-fmradion -m fm -t filesource -c
  filename=test-files/piano_iqtest.wav -q -R <out>.raw` writes 3839596
  bytes. Run it with and without `--realtime`.
- With `-v` on a file decode, check that the new fields print plausible
  values, that the startup line shows the kernel's computation value,
  and that a custom `blklen` gives the matching period.

Field test by the user (needs `-P`):

- Same SDR, same `-L`, same load, with and without `--realtime`:
  compare `UF=`, `Trough=`, the wake-latency maximum and tail counts,
  the push-gap counts, and `SDRdrop`.
- Instruments System Trace on the decode thread and the libairspyhf
  consumer thread: look for runnable-but-not-running intervals longer
  than the ring tolerance, and for the core type.
- During an audible gap, check the `coreaudiod` log for overload
  messages, to separate HAL overloads from a late writer.

## 5. Document corrections to make with the first patch

In doc/UNDERFLOW_RESEARCH_20261019.md:

- Section 7.1: the computation is half the constraint, not 0.25 of the
  period, and `preemptible` has no effect.
- Section 7, rank 3: on Darwin 27.2 the workgroup join succeeds without
  the real-time policy.
- Section 4.8: the kernel raises the priority of a pthread mutex owner
  when a higher-priority thread waits, so the printer lock is a smaller
  risk than stated.

## 6. Open questions

1. Does `--realtime` reduce `UF=` under real browser and graphics load?
2. Does the producer-side policy change `SDRdrop` and the wake-latency
   tail?
3. Does the workgroup change core placement or frequency?
4. Are some underflows caused by the CoreAudio IO thread itself?
5. Does a constraint of 4 ms or less improve the tail under real load?
6. Is the small idle latency cost of the workgroup real?
7. Does the kernel on Darwin 27.2 match XNU `main` on GitHub for the
   rules in section 1.1? The computation floor and the invalid-argument
   cases were confirmed by probe; the scheduler ordering rules were read
   from source only.

## 7. Supporting material

The analysis used four probe programs (`probe.cpp`, `prod.cpp`,
`t3.cpp`, `t4.cpp`), built with
`clang++ -std=c++17 -O2 <file> -framework AudioToolbox`. `probe` takes
`<busy thread count> <seconds per mode>` and gave the table in section
1.5; `prod` measured timer lateness; `t3` shows the abort of
`os_workgroup_interval_start()` from a thread outside the workgroup;
`t4` checks the policy calls. They are not in the repository.

The sources read were the GitHub `main` or `master` branches of
`apple-oss-distributions/xnu`, `apple-oss-distributions/libpthread`,
`libusb/libusb`, `airspy/airspyhf`, `airspy/airspyone_host`, and
librtlsdr, as of 2026-11-14. No copy is kept.

## 8. Reading the timing summary printed at exit

At exit the program prints `Summary:` lines to stderr unless `-q` is
given; `-v` is not needed. The first line (PortAudio underflows, ring
fill, `DataBuffer` queue, dropped blocks and samples) existed before
this work. The lines below were added by steps 1 and 4. The first three
are printed with and without `--realtime`, so a run with the option can
be compared with a run without it.

Example, from a file decode of `test-files/piano_iqtest.wav` with
`-v --realtime` on the Mac mini (M2 Pro) at `f14bf4c`:

```
Summary: DataBuffer queue=0 max=15 dropped blocks=0; SDR dropped samples=0
Summary: wake latency n=3733 max=82us >1ms=0 >2ms=0 >5ms=0 >10ms=0 >20ms=0; histogram [us): 2-4:37 4-8:399 8-16:917 16-32:1821 32-64:542 64-128:17
Summary: backlog pulls=17 max age=79.6ms; push gap max=5.6ms >1.5x period=0 >3x period=0
Summary: decode blocks=3750 period=5.3ms mean=0.3ms max=3.3ms >period=0 >computation(2.7ms)=1; realtime policy held
Summary: FileSource producer threads: time-constraint policy on 1, held at last check=1, lost=0, not checked=0
```

Times are shown in microseconds (`us`, rounded) below 0.1 ms and in
milliseconds with one decimal otherwise. All times are wall-clock times
from `std::chrono::steady_clock`. All counts cover the whole run.

### 8.1 `wake latency`

This line covers the blocks that arrived while the decode thread was
blocked in `DataBuffer::pull()` on an empty queue. For each such block
the latency is the time from the source thread's `push()` call to the
moment `pull()` is about to return the block. It contains the
condition-variable wake-up and the time the decode thread waited to be
scheduled. This is the delay that `--realtime` on the decode thread is
meant to shorten.

| Field | Meaning |
| ----- | ------- |
| `n` | Number of blocks measured this way |
| `max` | Longest wake latency |
| `>1ms` to `>20ms` | Number of blocks whose latency exceeded 1, 2, 5, 10, and 20 ms. Each count includes the ones after it |
| `histogram [us)` | Number of blocks per latency range in microseconds, as `low-high:count`. Each range includes its lower limit and excludes its upper limit, and is twice as wide as the one before. Only ranges with at least one block are printed. The first range is `0-1`, and the last is `16384+` (16.4 ms and above) |

How to read it: on an idle machine the values are tens of microseconds.
A late wake-up takes time from the output ring, whose tolerance is about
10 to 65 ms depending on `-L` (section 3.2 of
doc/UNDERFLOW_RESEARCH_20261019.md). Counts in `>5ms` and above that
appear together with `UF=` increases point to a late decode thread. If
`UF=` rises while these counts stay at zero, the cause is elsewhere:
the source side (section 8.2) or the CoreAudio IO thread.

### 8.2 `backlog pulls` and `push gap`

| Field | Meaning |
| ----- | ------- |
| `backlog pulls` | Number of blocks that were pushed while the decode thread was not waiting, so they sat in the queue. These blocks are not in the wake latency line |
| `max age` | Longest time such a block spent between `push()` and the return of `pull()` |
| `push gap max` | Longest time between two consecutive `push()` calls of the source |
| `>1.5x period` | Number of gaps longer than 1.5 block periods |
| `>3x period` | Number of gaps longer than 3 block periods |

The block period is the length of the first received block divided by
the device sample rate; it is the `period` value on the decode line.
The two gap counters start when the decode thread has received the
first block. `push gap max` covers all pushes.

How to read it:

- A backlog means the decode thread was busy or stalled when blocks
  arrived. With `--realtime` a backlog of 15 to 30 blocks and a `max
  age` of 80 to 150 ms appear at start, because the first block sets up
  the thread policy and the workgroup while the source keeps producing.
  This start-up effect also shows as `max=` on the `DataBuffer` line.
  Backlog pulls that keep increasing during a run (visible only by
  comparing runs of different length, since `-v` does not show this
  field) mean stalls after the start.
- The push gap shows the source side: the SDR, the USB transfer, and
  the vendor library threads, or the pacing sleep of the file source. A
  source that delivers evenly has a `push gap max` close to one period.
  Gaps above 1.5 or 3 periods mean the source threads ran late or the
  device delivered late; with an Airspy device, compare with `SDR
  dropped samples`. Step 4 gives these threads the policy to reduce
  such gaps. For the file source, two runs each gave 8.0 to 12.0 ms
  without `--realtime` and 5.4 ms with it.

### 8.3 `decode`

The decode time of a block runs from the return of `pull()` to the
point just before the audio output write. It covers the same span as
the workgroup interval. It is wall-clock time, so it includes any time
during which the decode thread was preempted. The line is omitted if no
block was decoded.

| Field | Meaning |
| ----- | ------- |
| `blocks` | Number of decoded blocks |
| `period` | Block period, as defined in section 8.2 |
| `mean`, `max` | Mean and longest decode time per block |
| `>period` | Number of blocks whose decode time exceeded one block period |
| `>computation(X)` | Only with `--realtime`. Number of blocks whose decode time exceeded X, the computation value of the thread policy as read back from the kernel (half the constraint) |
| `realtime policy held` or `LOST (noticed at block N)` | Only with `--realtime`. Result of the check, made every 2 seconds on the decode thread, that the thread still has the time-constraint policy |

How to read it:

- `mean` against `period` is the load of the decode thread: 0.3 ms of
  5.3 ms is about 6 %.
- `>period` above zero means decoding was slower than real time for
  those blocks, either from load (for example a long multipath filter
  on a slow machine) or from preemption.
- A thread that uses more CPU time than its computation value within
  one wake-up is ranked last among real-time threads until it blocks
  (section 1.1). `>computation` counts wall-clock time, so it is an
  upper limit for the number of such events. One count per run has
  appeared in every file decode so far and is probably the first block;
  this is not confirmed. A count that grows during the run means the
  computation value is too small for the decode settings.
- `LOST` means the kernel removed the policy. This has not been
  observed. The status printer also prints one warning when it happens,
  and with `-q` the warning is printed at exit.

### 8.4 `<source> producer threads`

Printed only with `--realtime`, and only if at least one source thread
received the policy. The name is `Airspy HF`, `Airspy`, `RTL-SDR`, or
`FileSource`.

| Field | Meaning |
| ----- | ------- |
| `time-constraint policy on N` | Number of source threads that received the policy. Expected: 2 for the Airspy HF+ and Airspy R2/Mini (the library's consumer and transfer threads), 1 for RTL-SDR and the file source |
| `held at last check` | Number of those threads that still had the policy at the last check. Checks run every 2 seconds and once more just before the source stops |
| `lost` | Number of those threads found without the policy |
| `not checked` | Number of those threads that were never checked successfully, for example because the thread had already ended |
| `WARNING: the callback thread is not among them` | Printed only if the thread that runs the data callback is not one of the N threads. It means the thread enumeration selected the wrong threads |

How to read it: for an Airspy device, `policy on 1` means the fallback
was used and only the consumer thread has the policy; the startup line
then says `(callback thread, fallback)`, and a warning at start gives
the number of new threads that was found. `held` should equal N and
`lost` should be 0.

### 8.5 Other lines at exit

- `realtime: N workgroup interval errors (first: ...)`: printed before
  the summary, unless `-q`, if a workgroup interval call failed during
  the run. The decode thread only counts these errors.
- With `-q` no summary is printed. Only the warning about a lost decode
  thread policy is printed, if that happened.

### 8.6 The same values during the run

With `-v` the status line shows a subset: `Wake=max(n)` with the count
above 1 ms, `Gap=max(n)` with the count above 1.5 periods,
`Dec=mean/max(n)` with the count above one period, `DecC=n` (above the
computation value, `--realtime` only), and `RT=LOST`. The histogram,
the other threshold counts, the backlog fields, and the producer thread
line appear only in the summary.
