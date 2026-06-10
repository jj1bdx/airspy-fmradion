# X7 Follow-up Amendment — DataBuffer overflow warning

**Date:** 2026-06-10
**Author:** Kenji Rikitake (change); Claude Code (claude-fable-5) (review and
verification)
**Scope:** User-authored follow-up amendment to the X7 fix from
`FIXES_CLAUDE_20260610.md` (commit 27934e0). The change touches
`include/DataBuffer.h` only; no other source files are modified.

## Summary

| ID | File(s) | Severity | Change |
|----|---------|----------|--------|
| X7a | `include/DataBuffer.h` | Informational | The rate-limiting condition on the queue-overflow warning (`dropped == 1 \|\| (dropped % 100) == 0`) is removed as redundant; the warning now prints on every dropped block, and the message text is changed to `DataBuffer: queue overflow, dropped blocks = {}` |

## The change

The X7 fix (commit 27934e0) made `DataBuffer<T>` a bounded queue
(`max_queue_blocks = 1024`, drop-oldest policy) with a `dropped_blocks()`
counter and a stderr warning that was rate-limited to the first drop and
every 100th drop thereafter. This amendment simplifies the warning logic in
`push()`:

```diff
-      if (dropped > 0 && (dropped == 1 || (dropped % 100) == 0)) {
+      if (dropped > 0) {
         fmt::println(
             stderr,
-            "DataBuffer: queue overflow, dropped oldest block ({} so far)",
+            "DataBuffer: queue overflow, dropped blocks = {}",
             dropped);
```

## Review

The change is correct:

- **Logic.** `dropped` is a local snapshot of the cumulative
  `m_dropped_blocks` counter, taken while `m_mutex` is held; it is non-zero
  only on a `push()` call that actually dropped a block (each `push()` adds
  exactly one block, so at most one drop per call). With the rate limit
  removed, exactly one warning is printed per dropped block, and the
  printed value is the monotonically increasing cumulative drop count. The
  new message text ("dropped blocks = {}") matches these semantics — it
  reports the running total, which is exactly what `dropped` holds.
- **Thread safety.** The `fmt::println` deliberately remains outside the
  mutex scope (no I/O under lock; the consumer in `pull()` is never blocked
  by a slow stderr). Printing a local snapshot outside the lock is safe.
  In theory, two producers pushing concurrently could print their
  snapshots out of order, but every `DataBuffer` in this program has a
  single producer thread (the source callback), so this cannot occur in
  practice — and even then the counts themselves remain correct, since
  `m_dropped_blocks` is only ever read or written under the mutex.
- **Volume.** Removing the rate limit means one stderr line per dropped
  block (up to the source block rate, ~100–400 lines/s during sustained
  overload). The X7 rationale called this condition abnormal — it occurs
  only when the consumer persistently cannot keep up — so unconditional
  per-drop reporting makes the problem maximally visible, which is the
  user's stated intent.

Two cosmetic touch-ups were applied before committing:

1. The comment above `push()` said the warning is "(rate-limited)"; the
   stale "(rate-limited)" was removed. (The X7 note in
   `FIXES_CLAUDE_20260610.md` still describes the original rate-limited
   behavior; this document records the amendment.)
2. The edited `fmt::println` call was not in clang-format canonical form;
   `clang-format -i include/DataBuffer.h` collapsed the four-line call to
   two lines:

   ```cpp
   fmt::println(stderr, "DataBuffer: queue overflow, dropped blocks = {}",
                dropped);
   ```

   The formatting change has no semantic effect.

## Verification

### Build

Incremental build (`cmake --build build --target all`) succeeded. The only
compiler warnings are the two pre-existing `unused variable 'ssr'` / `'dsr'`
warnings from the unmodified `r8brain-free-src` submodule, identical to the
state documented in `FIXES_CLAUDE_20260610.md`; no new warnings are
attributable to this change.

### Runtime — filesource regression run

A 2-second 384 kHz raw float32 IQ file (complex tone at +100 kHz) was
generated and decoded:

```sh
./build/airspy-fmradion -m fm -t filesource -q \
  -c freq=88100000,srate=384000,filename=/tmp/test_iq.bin,raw,format=FLOAT \
  -W /dev/null
```

The run completed normally ("airspy-fmradion terminated", exit code 0) with
no overflow warning — expected, since the main loop keeps up with the
paced filesource producer and the queue never approaches 1024 blocks. The
overflow path cannot realistically be triggered through the binary without
artificially stalling the consumer.

### Runtime — DataBuffer overflow harness

The overflow path was therefore exercised directly with a throwaway test
program (`/tmp/databuffer_overflow_test.cpp`, deleted after the run) that
compiles `include/DataBuffer.h` standalone against the build tree's
`{fmt}` (`-std=c++20 -Wall -Wextra`, no warnings) and runs two phases:

1. **Deterministic overflow:** push `max_queue_blocks + 7` blocks with no
   consumer. Exactly 7 warnings were printed, one per dropped block, with a
   monotonically increasing count, and `dropped_blocks() == 7` /
   `queue_size() == 1024` (asserted):

   ```
   DataBuffer: queue overflow, dropped blocks = 1
   DataBuffer: queue overflow, dropped blocks = 2
   ...
   DataBuffer: queue overflow, dropped blocks = 7
   ```

2. **Fast producer / slow consumer:** with the queue full, 50 more pushes
   against a consumer pulling one block per millisecond produced one
   warning per additional drop, the counter continuing monotonically
   (total 56 drops, 56 warning lines — verified by `grep -c`).

The same harness compiled with `-fsanitize=thread` ran to completion with
no ThreadSanitizer reports, confirming the lock-snapshot-print pattern is
race-free.

No automated tests exist in this repository; final correctness should be
validated by running the binary against an SDR device per the project
documentation.
