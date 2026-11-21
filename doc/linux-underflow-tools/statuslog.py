#!/usr/bin/env python3
"""Timestamp airspy-fmradion stderr lines.

Reads stdin (or a FIFO given as argv[1]) as bytes, splits on CR and LF,
and writes "monotonic_ns<TAB>line" to argv[2] (or stdout). The status line
has no timestamp of its own; CLOCK_MONOTONIC matches the T7 trace and the
stall injector log.
"""
import os
import sys
import time


def main():
    src = open(sys.argv[1], "rb", buffering=0) if len(sys.argv) > 1 else sys.stdin.buffer
    dst = open(sys.argv[2], "w", buffering=1) if len(sys.argv) > 2 else sys.stdout
    pending = b""
    while True:
        chunk = os.read(src.fileno(), 65536)
        if not chunk:
            break
        t = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        pending += chunk
        parts = pending.replace(b"\r", b"\n").split(b"\n")
        pending = parts.pop()
        for p in parts:
            if p:
                dst.write(f"{t}\t{p.decode('utf-8', 'replace')}\n")
    if pending:
        t = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
        dst.write(f"{t}\t{pending.decode('utf-8', 'replace')}\n")


if __name__ == "__main__":
    main()
