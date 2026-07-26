# RtlSdrSource: rtlsdr_read_sync → rtlsdr_read_async — airspy-fmradion

**Date:** 2026-07-13 (revised the same day: SIGINT shutdown fix)
**Author:** Claude Code (claude-fable-5), analysis by the cpp-expert subagent
**Scope:** Rewrite of `RtlSdrSource` streaming from the blocking
`rtlsdr_read_sync()` loop to the callback-based `rtlsdr_read_async()` API,
following the async pattern already used by `AirspySource` /
`AirspyHFSource`, plus a follow-up fix for a SIGINT shutdown hang found in
the first revision.

## Summary

`RtlSdrSource` previously ran a source thread that looped on
`rtlsdr_read_sync()`, one blocking USB read per block
(`get_samples()`). It now calls `rtlsdr_read_async()` once from the
source thread; librtlsdr keeps multiple USB transfer buffers in flight
inside its libusb event loop and delivers each filled buffer to a
callback, which converts the offset-binary 8-bit I/Q bytes to
`IQSampleVector` and pushes it into the `DataBuffer`. `stop()`
unblocks the source thread with `rtlsdr_cancel_async()` before
joining, mirroring `AirspySource::stop()`.

Verified with a full incremental build (`cmake --build build --target
all`, clean compile) and `clang-format`. There are no automated tests;
runtime behavior should be confirmed against an RTL-SDR device
(e.g. `airspy-fmradion -t rtlsdr -q ...`), specifically including
SIGINT (Ctrl-C), SIGQUIT, and SIGTERM shutdown.

## Revision 2: SIGINT shutdown hang and cleanup fixes

### The hang

The first revision made `callback()` return early (discarding data)
once `*m_stop_flag` was set. This deadlocked shutdown:

1. The signal-handling thread in `main.cpp` (`process_signals()`)
   catches SIGINT/SIGQUIT/SIGTERM and only sets `stop_flag`.
2. The main processing loop is normally blocked in
   `DataBuffer::pull()` (`main.cpp:912`), which waits on a condition
   variable until either data is pushed or `push_end()` is called.
3. With the callback discarding all data after `stop_flag` was set, no
   further `push()` occurred, so the main thread never woke up, never
   left the processing loop, and never reached
   `up_srcsdr->stop()` (`main.cpp:1159`) — the only place
   `rtlsdr_cancel_async()` was called. The async loop therefore also
   never ended: a deterministic circular wait.

The sibling `AirspySource::callback()` does **not** check the stop
flag, for exactly this reason: the data flow itself is what wakes the
consumer so it can observe `stop_flag` and initiate `stop()`.

### The fixes

- **`callback()` no longer checks `m_stop_flag`.** Samples keep
  flowing into the `DataBuffer` until the async loop is actually
  canceled; the next `push()` wakes the main loop, which observes
  `stop_flag`, exits, and calls `stop()`. (The bounded queue in
  `DataBuffer::push()` caps memory if shutdown is ever delayed.)
- **`run()` calls `m_buf->push_end()` after `rtlsdr_read_async()`
  returns** — on cancellation, device failure, or an immediate async
  start error. This guarantees a consumer blocked in `pull()` wakes up
  even when no more data will ever arrive (e.g. device unplugged):
  `pull()` returns an empty vector, the main loop `continue`s, sees
  `pull_end_reached()`, sets `stop_flag`, and terminates cleanly. The
  previous sync implementation could hang the same way on device
  failure (thread exit without `push_end()`); the async version now
  cannot.
- **`stop()` is idempotent.** All work is guarded by `if (m_thread)`:
  `rtlsdr_cancel_async()` is only issued when the source thread has
  not been joined yet, then `join()`/`reset()`. A second call is a
  no-op. The cancel's return value is intentionally not reported —
  a negative value only means the async loop already ended (librtlsdr
  returns −2 when not streaming), which is expected when `run()`
  terminated on its own.
- **`~RtlSdrSource()` calls `stop()` before `rtlsdr_close()`.** Even
  if a caller forgets `stop()`, destruction cancels and joins the
  source thread first, so `rtlsdr_close()` can never race a live
  `rtlsdr_read_async()` call. (This was flagged as a latent hazard in
  the original analysis; the RTL-SDR source no longer has it.)

### Shutdown sequence after the fix (SIGINT)

```
SIGINT → process_signals() sets stop_flag
       → callback keeps pushing; main loop wakes from pull()
       → loop condition !stop_flag fails → loop exits
       → main.cpp:1159 up_srcsdr->stop()
            → rtlsdr_cancel_async() → rtlsdr_read_async() returns in run()
            → run() calls push_end() (harmless now) and returns
            → join() completes
       → ~RtlSdrSource(): stop() is a no-op, rtlsdr_close()
```

## API semantics (librtlsdr 2.0.2, `/opt/homebrew/include/rtl-sdr.h`)

- `rtlsdr_read_async(dev, cb, ctx, buf_num, buf_len)` **blocks the
  calling thread** until `rtlsdr_cancel_async(dev)` is called from
  another thread. The callback runs on that same (source) thread,
  inside librtlsdr's libusb event loop — no additional thread is
  created by this rewrite.
- `buf_num = 0` selects the library default of 15 in-flight buffers.
- `buf_len` must be a multiple of 512 bytes (librtlsdr recommends a
  multiple of 16384, the URB size). It is set to `2 * m_block_length`
  bytes — since `configure()` forces `m_block_length` to a multiple of
  4096 samples in [4096, 65536], `buf_len` is always a multiple of
  8192, satisfying the hard 512-byte constraint. Only the minimum
  `blklen=4096` misses the *recommended* 16384 multiple; this is
  accepted by the library and left as-is to preserve the existing
  configuration range.
- Callback signature is `void (unsigned char *buf, uint32_t len, void
  *ctx)`. During cancellation an in-flight transfer may deliver a
  short or empty buffer; this is expected, not a device error.
- `rtlsdr_reset_buffer()` is still required before streaming; the
  existing call at the end of `configure()` is unchanged and correctly
  placed.

## Changes

### `include/RtlSdrSource.h`

- Removed `static bool get_samples(IQSampleVector *samples)`.
- Added `void callback(const unsigned char *buf, std::size_t len)`
  (conversion + push) and the static trampoline
  `static void rx_callback(unsigned char *buf, std::uint32_t len,
  void *ctx)` matching `rtlsdr_read_async_cb_t` exactly.
- `run()` now takes `(struct rtlsdr_dev *dev, std::atomic_bool
  *stop_flag)`, matching the `AirspySource::run(dev, stop_flag)`
  idiom.
- Added `<cstddef>` / `<cstdint>` includes for `std::size_t` /
  `std::uint32_t`.

### `sfmbase/RtlSdrSource.cpp`

- `~RtlSdrSource()` calls `stop()` before `rtlsdr_close()` (revision
  2, see above).
- `start()` spawns the thread as `std::thread(run, m_dev, stop_flag)`.
- `run()` calls `rtlsdr_read_async(dev, rx_callback, nullptr, 0,
  2 * m_block_length)` and blocks until canceled. A negative return
  is reported via `fmt::println(stderr, ...)` and `m_error` **only
  when the stop flag is not set**, so the error return produced by a
  normal `rtlsdr_cancel_async()` shutdown is not misreported. After
  the async call returns, `run()` always calls `m_buf->push_end()`
  (revision 2, see above).
- `stop()` cancels the async loop and joins the thread, guarded by
  `if (m_thread)` for idempotence (revision 2, see above).
- `rx_callback()` resolves the instance through the existing
  `static std::atomic<RtlSdrSource *> m_this` trampoline (house style
  shared with `AirspySource`; the `ctx` pointer is deliberately
  unused).
- `callback()` preserves the exact sample conversion from
  `get_samples()` — offset-binary `uint8` (128 = DC zero) to
  `std::complex<float>` scaled by 1/128 — and pushes with
  `m_buf->push(std::move(iqsamples))`. It returns early only when
  `len < 2` (empty/short canceled transfer); it deliberately does
  **not** check the stop flag (revision 2, see above).
  `m_buf->push()` is thread-safe (mutex + condition variable) and
  non-blocking, so calling it from the libusb event loop cannot stall
  USB transfer reaping.
- Added a comment where `m_block_length` is clamped, noting the
  8192-byte alignment consequence for the async buffer size.

## Behavioral differences vs. the sync implementation

1. **Short reads are no longer fatal.** The sync code treated
   `n_read != 2 * m_block_length` as `"short read, samples lost"` and
   terminated the source thread. With async delivery a short/empty
   buffer occurs only during cancellation and is silently ignored; the
   async callback has no error-return channel, and the correctness
   backstop is `stop()`'s explicit cancel.
2. **Lower overrun risk.** With 15 buffers in flight, USB data keeps
   flowing while the previous block is converted and queued; the sync
   version could drop samples inside the kernel/libusb between
   consecutive `rtlsdr_read_sync()` calls.
3. **End-of-stream is now signaled.** The sync `run()` exited on
   error without `push_end()`, which could strand the main loop in
   `pull()` on device failure. The async `run()` always calls
   `push_end()` when streaming ends, so the main loop terminates and
   the normal cleanup path runs.
4. **Same threading shape.** One source thread as before; the callback
   executes on it. `m_buf` / `m_stop_flag` are set in `start()` before
   the thread exists and only read afterwards. `m_error` is written
   from the source thread only on async-start failure and read by the
   main thread after `stop()` joins — the same (benign) pattern as the
   other sources.
5. **Slow-consumer policy unchanged.** `DataBuffer::push()` still
   bounds the queue at `max_queue_blocks = 1024`, dropping the oldest
   block with a rate-limited warning.

## Estimated performance impact

These are **analytical estimates, not measurements** (the project has
no automated benchmarks; measure with e.g. `top -pid $(pgrep
airspy-fmradion)` against a live device to confirm).

### CPU usage: essentially unchanged (well under 0.1 % of one core saved)

The per-sample work — offset-binary `uint8` to `complex<float>`
conversion, 1/128 scaling, one `IQSampleVector` allocation per block,
one `DataBuffer::push()` — is identical in both versions. At the
maximum sample rate of 3.2 MS/s this conversion loop is on the order
of 1 % of one modern core, and it neither grew nor shrank.

What the async version actually eliminates per block:

- **One heap allocation + zero-fill of the staging buffer.** The sync
  `get_samples()` created `std::vector<uint8_t> buf(2 * m_block_length)`
  — value-initialized, i.e. memset to zero — before every read, up to
  131072 bytes per block. The async callback receives librtlsdr's own
  transfer buffer directly, so this staging copy target is gone.
  Worst case (3.2 MS/s) that is 6.4 MB/s of zero-fill plus 49–195
  allocations/s against memory bandwidth in the tens of GB/s: well
  under 0.1 % of one core.
- **One blocking `libusb_bulk_transfer()` round-trip per block**
  (libusb implements sync transfers as submit-then-wait on top of its
  async core). The async loop instead reaps completions from
  persistent in-flight transfers. Wakeup and event-handling rates are
  comparable (one per buffer either way, 49–195/s), so this is a wash.

Net: expect no visible change in `top`. This modification was not a
CPU optimization and should not be justified as one.

### Throughput: identical by construction

Throughput is fixed by the configured sample rate (0.9–3.2 MS/s); both
versions must move exactly `2 × srate` bytes/s off the USB bus. The
async API does not make the radio faster — it changes *when* data can
be lost, not how much arrives.

### Dropout robustness: the real improvement (~1–2 orders of magnitude)

- **Sync:** a USB transfer was pending only *while* `rtlsdr_read_sync()`
  was executing. During the gap between reads (conversion + push time,
  plus any scheduler preemption of the source thread), incoming samples
  had only the RTL2832U's small on-chip FIFO (a few KiB ≈ **under
  ~1 ms** at typical rates) before data loss — and any resulting short
  read was **fatal**: `"short read, samples lost"` terminated the
  stream.
- **Async:** librtlsdr keeps `buf_num = 15` buffers of
  `2 × m_block_length` bytes in flight, so the kernel/libusb can absorb
  a stalled or preempted source thread for roughly:

  | Sample rate | blklen (samples) | Bytes in flight | Jitter tolerance |
  |---|---|---|---|
  | 1.152 MS/s (default) | 16384 (default) | 491,520 | ≈ 213 ms |
  | 3.2 MS/s | 16384 | 491,520 | ≈ 77 ms |
  | 3.2 MS/s | 65536 (max) | 1,966,080 | ≈ 307 ms |

  versus the sync version's sub-millisecond to low-millisecond window.
  On top of that, a short buffer is now benign instead of terminating
  the program.

In practice this means: same CPU load, same throughput, but scheduling
hiccups (other processes, laptop power management, USB bus contention)
that previously caused sample loss — or killed the stream outright —
are now absorbed silently up to ~0.1–0.3 s. Downstream of the driver,
`DataBuffer`'s 1024-block queue already provided seconds of buffering;
the async change closes the gap that existed *upstream* of it.

## Pitfalls considered (from the cpp-expert analysis)

- **`rtlsdr_read_async()` failing immediately** (device claim error):
  `run()` reports the error, calls `push_end()` so the main loop ends,
  and returns; `stop()` still works because `rtlsdr_cancel_async()` on
  a non-streaming device just returns non-zero, which is ignored.
- **Never gate the data path on `stop_flag`.** The consumer wakes only
  on `push()`/`push_end()`; discarding data on the producer side once
  the flag is set deadlocks shutdown (this was revision 1's bug —
  see "Revision 2" above). `stop()` owning `rtlsdr_cancel_async()` is
  the one and only cancellation path.
- **Integer width:** `2 * m_block_length ≤ 131072` fits comfortably in
  `int`/`uint32_t`; the product is cast explicitly to `uint32_t` at
  the call site.
- **`m_this` vs. `ctx`:** passing `this` via `ctx` would be slightly
  more idiomatic and multi-instance-safe, but the whole codebase
  (including the previous sync code) uses the single-instance
  `m_this` atomic; consistency was chosen. Noted as a possible future
  cleanup if multiple simultaneous sources are ever needed.
- **Known residual corner:** if the device delivers no USB data at all
  *and* `rtlsdr_read_async()` neither returns nor errors, a SIGINT
  would still leave the main loop blocked in `pull()` until libusb
  times the transfers out. The Airspy sources share this corner (they
  never call `push_end()` at all); resolving it fully would require a
  watchdog or a timed `pull()`, out of scope here.

## Files modified

- `include/RtlSdrSource.h`
- `sfmbase/RtlSdrSource.cpp`
