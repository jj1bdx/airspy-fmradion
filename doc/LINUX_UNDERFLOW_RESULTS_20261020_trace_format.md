# T7 block trace format (2026-10-20)

Measurement-only instrumentation for the underflow measurement campaign
(`doc/LINUX_UNDERFLOW_MEASUREMENT_PLAN_20261020.md`, tool T7). The change
is `doc/LINUX_UNDERFLOW_RESULTS_20261020_trace.diff`, applied to `f236638`;
it is not part of the source tree. On the Pi it was built from a worktree
in `work-linux/wt-t7` into `work-linux/build-t7/`. Neither is in the
repository, and the build was deleted on 2026-10-20.

## Enabling

```sh
AFM_TRACE=/path/to/trace.csv build-t7/airspy-fmradion ...
```

- `AFM_TRACE` unset or empty: tracing is off. Audio output and stderr output
  are the same as in an unpatched build (verified byte for byte on
  `piano_iqtest.wav`, with and without `-v`).
- `AFM_TRACE` set: the file is created (truncated if it exists). If it can not
  be opened, the program prints an error and exits with status 1.
- With tracing on, stderr gets these extra lines: `AFM_TRACE: writing block
  trace to '<path>'` at start, the PortAudio facts (see "Header lines") after
  the stream is started, and `AFM_TRACE: <n> records written to '<path>',
  <m> dropped` at exit, after the `Summary:` line.

## Mechanism

- The decode thread (the main thread) stores one fixed-size record per block
  in a preallocated ring of 65536 records (about 7 MB). It does no I/O, no
  allocation, and takes no lock.
- `DataBuffer::push()` takes the push time under the lock it already holds,
  and keeps it in a parallel queue (source thread, only when tracing is on).
  `DataBuffer::pull()` pops the matching time under the same lock. A block
  dropped by the queue overflow path is removed from both queues.
- A writer thread wakes every 200 ms, appends the new records as CSV lines,
  and flushes. On Linux it raises its own nice value by 10 (capped at 19),
  using its own TID.
- If the ring is full, the new record is dropped and counted. At 190
  records/s the writer thread may stall for about 340 s before this happens.
  The count is in the `dropped` column and in the end lines.
- At normal exit (end of stream, SIGINT, SIGTERM) the file is completed with
  three `# end_...` and `# records_...` lines. After `exit(1)` paths or a
  crash, the last 200 ms of records can be missing, and the end lines may be
  missing.
- Size: about 130 bytes per record: about 88 MB per hour at 190 records/s
  (file source), and about 44 MB per hour at 94 records/s (HF+ here).
- Cost on the decode thread: about 0.8 microseconds per block (measured with
  `-O3`; two `CLOCK_THREAD_CPUTIME_ID` reads are the largest part).

## File layout

1. Header lines starting with `# ` (see below).
2. One CSV column line starting with `type,`, written before the first
   record.
3. Records, one per line, in the order they were stored (not sorted by time).
4. End lines starting with `# end_monotonic_ns=`, `# records_written=`, and
   `# records_dropped=`.

Parsers should skip every line that starts with `#`.

## Record types (column `type`)

| type | Meaning | Emitted |
|---|---|---|
| `B` | One decoded audio block and its output write | After the write returned |
| `P` | The prefill (`prime()`) silence write | Inside the write of the first block, before the first `B` |
| `A` | An adaptive prefill (`adapt()`) silence write | Inside the write of the block that underflowed, so before that block's `B` record |

`P` and `A` records exist only for PortAudio output with
`--portaudio-prefill` or `--portaudio-prefill-adapt`. The `block` column of
a `P` or `A` record is the block being written when the silence was written.
To get time order, sort by `t_write_start_ns` (stable).

Blocks that produce no audio (for example, a warm-up block) have no record,
so `block` can skip numbers. `block` is the loop counter of the decode loop in
`main.cpp`; it counts pulled blocks.

## Columns

All times are `CLOCK_MONOTONIC` nanoseconds (int64), except the two `cpu_*`
columns. A field that does not apply is `-1`.

| Column | Unit | `B` | `P`, `A` |
|---|---|---|---|
| `type` | | `B` | `P` or `A` |
| `block` | count | decode loop block index | block being written |
| `t_push_ns` | ns | Time the source thread pushed this block into `DataBuffer` (inside `push()`, under its lock) | -1 |
| `t_pull_ns` | ns | Time `DataBuffer::pull()` returned to the decode thread | -1 |
| `t_decode_end_ns` | ns | Time decoding finished, just before the output write call | -1 |
| `t_write_start_ns` | ns | PortAudio: just before `Pa_WriteStream()` of the audio block. Other outputs: the same as `t_decode_end_ns` | just before `Pa_WriteStream()` of the silence |
| `t_write_end_ns` | ns | PortAudio: just after `Pa_WriteStream()` returned. Other outputs: after `AudioOutput::write()` returned | just after `Pa_WriteStream()` returned |
| `cpu_pull_ns` | ns | Decode thread CPU time (`CLOCK_THREAD_CPUTIME_ID`) at `t_pull_ns` | -1 |
| `cpu_decode_end_ns` | ns | Decode thread CPU time at `t_decode_end_ns` | -1 |
| `queue_len` | blocks | `DataBuffer` queue size at pull, including the pulled block (backlog = `queue_len` - 1) | -1 |
| `frames` | frames | Audio frames written | Silence frames written |
| `pre_avail` | frames | `Pa_GetStreamWriteAvailable()` just before the write; -1 for the first block (the program does not call it there), for non-PortAudio output, or if the call failed | Same call, made only when tracing is on |
| `post_avail` | frames | `Pa_GetStreamWriteAvailable()` just after the write (the value the program already computes) | Same call, made only when tracing is on |
| `underflow` | 0 or 1 | 1 if `Pa_WriteStream()` returned `paOutputUnderflowed` for this write | Same |
| `dropped` | records | Number of records dropped by a full ring before this record was stored (cumulative) | Same |

Notes:

- For the first block, `pre_avail` is -1. If a prefill is configured, the `P`
  record is expected to have `pre_avail` equal to the ring size B and
  `post_avail` equal to B minus the prefill frames.
- `pre_avail` and `post_avail` are raw frames from PortAudio, not the
  program's capacity-corrected values. They are -1, not an error code, when
  the call failed.
- The `underflow` flag of the first block can be 1 at start-up. The program
  counts that one as a start-up underflow (`(+n at start)` in the `Summary:`
  line), not as a stall.
- `t_write_start_ns` is read after the audio samples were copied to
  PortAudio's float buffer and after the `pre_avail` call. The time from
  `t_decode_end_ns` to `t_write_start_ns` is therefore a few microseconds
  for PortAudio output.

## Header lines

All lines start with `# ` and have the form `# key=value`. Common lines:

| Key | Content |
|---|---|
| (first line) | `airspy-fmradion block trace, format version 1` |
| `clock_realtime_ns` | `CLOCK_REALTIME` at start (ns since the Unix epoch) |
| `clock_monotonic_ns` | `CLOCK_MONOTONIC` at the same moment (ns) |
| `clock_pair_uncertainty_ns` | Time between the two `CLOCK_MONOTONIC` reads around the `CLOCK_REALTIME` read |
| `git` | Commit SHA1 of the build tree, with `with uncommitted changes` when dirty (it is always dirty for this build) |
| `command_line` | The program's argv joined by spaces |
| `pid`, `decode_tid` | Process ID, and the decode thread's TID (equal on Linux; use the TID with the T4 stall injector) |
| `ring_records` | Ring size in records |
| `drain_interval_ms` | Writer thread interval |

Wall clock of a record: `realtime = clock_realtime_ns + (t - clock_monotonic_ns)`.

PortAudio output only, written after `Pa_StartStream()` (they are also
printed to stderr with the prefix `AFM_TRACE: `):

| Key | Content |
|---|---|
| `pa_version` | `Pa_GetVersionInfo()->versionText` |
| `pa_host_api` | Host API name (`Pa_GetHostApiInfo`), for example `ALSA` |
| `pa_host_api_type` | `PaHostApiTypeId` number (ALSA is 8, CoreAudio is 5) |
| `pa_device_index`, `pa_device_name` | Device used |
| `pa_device_default_low_output_latency_ms`, `pa_device_default_high_output_latency_ms` | From `Pa_GetDeviceInfo` |
| `pa_suggested_latency_ms` | Requested `suggestedLatency` (the `-L` value or the default) |
| `pa_output_latency_ms` | Granted `outputLatency` from `Pa_GetStreamInfo` |
| `pa_requested_sample_rate_hz`, `pa_stream_sample_rate_hz` | Rate requested and rate in `Pa_GetStreamInfo` |
| `pa_channels` | Channel count |
| `pa_write_available_after_start_frames` | `Pa_GetStreamWriteAvailable()` right after `Pa_StartStream()`. The ring is empty then on ALSA, so this is the buffer size **B** in frames. |

## Computing the quantities

Take `B = pa_write_available_after_start_frames` from the header, and the
sample rate `fs` from `pa_stream_sample_rate_hz`. Use `B` from the header, not
the `Ring=` value of the program's status line, which is an estimate.

**(a) Scheduling delay of a block** (`B` records):

```
sched_delay_ns = t_write_start_ns - t_push_ns
```

This is the time from the source thread pushing the block to the audio write
starting. Split it with:

- `t_pull_ns - t_push_ns`: wait in `DataBuffer` (wake-up latency of the decode
  thread when `queue_len` is 1, plus queueing behind earlier blocks when it is
  larger);
- `t_decode_end_ns - t_pull_ns`: decode wall time;
- `cpu_decode_end_ns - cpu_pull_ns`: decode CPU time; wall minus CPU is the
  time the decode thread was runnable or blocked and not on a CPU;
- `t_write_end_ns - t_write_start_ns`: time in `Pa_WriteStream()` (near zero
  when the ring has room; it grows when the call waits for free space).

The nominal arrival period is `t_push_ns[n+1] - t_push_ns[n]`: 5.333 ms for
the file source (256 frames), 10.667 ms for the HF+ on this Pi (512 frames).
Lateness of block n against a fixed grid: fit `t_push_ns` against `block` and
use the residual, or compare with the median interval.

**(b) Ring fill in frames** (PortAudio, `pre_avail` or `post_avail` not -1):

```
fill_before_write = B - pre_avail      (the ring's trough for this block)
fill_after_write  = B - post_avail     (the ring's peak for this block)
fill_ms           = 1000 * fill / fs
```

For `P` and `A` records the same formulas apply to the silence write. If a
fill is negative, the ring is larger than `B`; use the largest `pre_avail` or
`post_avail` seen as `B`.

**(c) Ring-empty detection:**

```
ring_empty_before_write = (pre_avail >= B)
```

`pre_avail >= B` means the ring was empty when this block arrived. After the
first block, the device had consumed everything, so the stream underflowed
before this write. Expect `underflow = 1` on the same record; whether
PortAudio 19.6.0 reports it that way on ALSA is one of the questions these
data answer, so compare the two. A `pre_avail` slightly below `B` is a nearly
empty ring; `B - pre_avail` is the fill left, and `1000 * (B - pre_avail) /
fs` is the time in milliseconds until the ring would have emptied. The test is
not available for the first block (`pre_avail` is -1).

## Loading the trace (Python, standard library only)

```python
import csv

def load(path):
    header, rows = {}, []
    with open(path) as f:
        data = []
        for line in f:
            if line.startswith("#"):
                key, _, value = line[2:].strip().partition("=")
                header[key] = value
            else:
                data.append(line)
    for r in csv.DictReader(data):
        rows.append({k: (v if k == "type" else int(v)) for k, v in r.items()})
    return header, rows

header, rows = load("trace.csv")
B = int(header["pa_write_available_after_start_frames"])
blocks = [r for r in rows if r["type"] == "B"]
delay_ms = [(r["t_write_start_ns"] - r["t_push_ns"]) / 1e6 for r in blocks]
empty = [r for r in blocks if r["pre_avail"] >= B]
```

## Limits

- The PortAudio path (`-P`) has been compiled and reviewed but not run, since
  no run with `-P` was allowed while the code was written. The first `-P` run
  should be checked for: the `pa_*` header lines, `pre_avail` and
  `post_avail` between 0 and B, `post_avail` smaller than `pre_avail` after a
  write, and `P` or `A` records when a prefill is configured.
- The extra `Pa_GetStreamWriteAvailable()` calls (one right after
  `Pa_StartStream()`, two around each `P` or `A` silence write) happen only
  when tracing is on. They are rare events and add no call to the block path.
- `t_push_ns` is the time the block entered `DataBuffer`, which is after the
  source library delivered it. For the HF+, the delay inside the library
  callback thread before the push is not measured.
- Thread sanitizer could not be run on this kernel (unsupported address
  space layout). Address and undefined-behavior sanitizers ran clean on a
  stand-alone test of the ring, the writer thread, and the `DataBuffer`
  overflow path.
