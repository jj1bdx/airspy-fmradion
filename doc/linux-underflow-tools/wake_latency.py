#!/usr/bin/env python3
"""Wake-up latency of one thread from an unfiltered trace-cmd report.

Usage: wake_latency.py REPORT TID

For each sched_wakeup of TID, the latency is the time to the next
sched_switch into TID. Prints the distribution and, for each latency over
2 ms, the CPU it ran on and the tasks that held that CPU in between (with
their run time inside the window).
"""
import re
import sys
from collections import defaultdict

tid = sys.argv[2]
line_re = re.compile(r"\[(\d+)\]\s+\S+\s+(\d+\.\d+):\s+(sched_\w+):\s+(.*)")
switch_re = re.compile(r"^(.+):(\d+) \[(\d+)\] (\S+) ==> (.+):(\d+) \[(\d+)\]")
wake_re = re.compile(r"^(.+):(\d+) \[(\d+)\] CPU:(\d+)")

# Per-CPU list of (time, next_comm, next_pid, next_prio) switch events.
switches = defaultdict(list)
wakes = []      # (t, target_cpu)
switch_in = []  # (t, cpu, prev_comm, prev_pid, prev_prio)
for line in open(sys.argv[1], errors="replace"):
    m = line_re.search(line)
    if not m:
        continue
    cpu, t, ev, rest = int(m.group(1)), float(m.group(2)), m.group(3), m.group(4)
    if ev == "sched_switch":
        s = switch_re.match(rest)
        if not s:
            continue
        switches[cpu].append((t, s.group(5), s.group(6), s.group(7)))
        if s.group(6) == tid:
            switch_in.append((t, cpu, s.group(1), s.group(2), s.group(3)))
    elif ev == "sched_wakeup":
        w = wake_re.match(rest)
        if w and w.group(2) == tid:
            wakes.append((t, int(w.group(4))))


def holders(cpu, t0, t1):
    """Tasks on cpu during [t0, t1] with their run time in ms."""
    ev = switches[cpu]
    cur = None
    out = defaultdict(float)
    last = t0
    for t, comm, pid, prio in ev:
        if t <= t0:
            cur = (comm, pid, prio)
            continue
        if t > t1:
            break
        if cur is not None:
            out[cur] += (t - last) * 1000.0
        cur = (comm, pid, prio)
        last = t
    if cur is not None:
        out[cur] += (t1 - last) * 1000.0
    return sorted(out.items(), key=lambda x: -x[1])


lat = []
slow = []
si = 0
for t, tcpu in wakes:
    while si < len(switch_in) and switch_in[si][0] < t:
        si += 1
    if si == len(switch_in):
        break
    t2, cpu, pc, pp, pr = switch_in[si]
    d = (t2 - t) * 1000.0
    lat.append(d)
    if d > 2.0:
        slow.append((t, d, tcpu, cpu, holders(cpu, t, t2)))
lat.sort()
n = len(lat)
if n == 0:
    print("no events")
    sys.exit(0)
for p in (50, 90, 99, 99.9, 100):
    print(f"p{p}: {lat[min(n - 1, int(p / 100 * (n - 1)))]:.3f} ms")
print(f"wakeups: {n}, over 1 ms: {sum(1 for x in lat if x > 1)}, "
      f"over 2 ms: {len(slow)}, over 3 ms: {sum(1 for x in lat if x > 3)}")
for t, d, tcpu, cpu, hs in slow:
    hold = ", ".join(f"{c}:{p}[{pr}] {ms:.2f}" for (c, p, pr), ms in hs[:4])
    print(f"t={t:.6f} latency={d:.3f} ms target_cpu={tcpu} ran_on={cpu} held_by: {hold}")
