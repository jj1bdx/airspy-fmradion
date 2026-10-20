#!/usr/bin/env python3
"""Evaluate injected stalls against the T7 trace and the capture gaps.

Usage: stall_eval.py RUNDIR

For each stall in RUNDIR/stalls.tsv:
  margin_ms  fill left in the ring at the stall start, from the last write
             before the stall: post-write fill minus the time since then
  late_ms    next write start minus the time the ring would have run dry
             (positive: the ring should have run dry)
  uf         underflow flag on the first write after the stall
  empty      that write found the ring empty (pre_avail >= B)
  gap_ms     inserted silence in the capture at that point (file source
             with a reference only; gaps.tsv from gapscan -r)
"""
import csv
import os
import sys


def load_trace(path):
    header, data = {}, []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                k, _, v = line[2:].strip().partition("=")
                header[k] = v
            else:
                data.append(line)
    rows = [{k: (v if k == "type" else int(v)) for k, v in r.items()}
            for r in csv.DictReader(data)]
    return header, rows


def main():
    rundir = sys.argv[1]
    header, rows = load_trace(os.path.join(rundir, "trace.csv"))
    fs = float(header.get("pa_stream_sample_rate_hz", "48000"))
    B = int(header["pa_write_available_after_start_frames"])
    blocks = sorted([r for r in rows if r["type"] == "B"], key=lambda r: r["t_write_start_ns"])
    allw = sorted(rows, key=lambda r: r["t_write_start_ns"])
    # Reference frame position after each block's write (audio frames only).
    cum = 0
    for r in blocks:
        cum += r["frames"]
        r["ref_end"] = cum

    stalls = []
    with open(os.path.join(rundir, "stalls.tsv")) as f:
        for line in f:
            if line.startswith("#") or line.startswith("idx"):
                continue
            p = line.split("\t")
            stalls.append({"idx": int(p[0]), "req": float(p[1]), "t_stop": int(p[3]),
                           "t_res": int(p[4]), "actual": float(p[5])})

    gaps = []
    gp = os.path.join(rundir, "gaps.tsv")
    if os.path.exists(gp):
        with open(gp) as f:
            for line in f:
                p = line.rstrip("\n").split("\t")
                if p[0] == "ins":
                    gaps.append((int(p[2]), float(p[4])))

    print("idx\treq_ms\tactual_ms\tmargin_ms\tlate_ms\tpredicted\tuf\tempty\tgap_ms\tqueue_after")
    for s in stalls:
        before = [w for w in allw if w["t_write_end_ns"] <= s["t_stop"]]
        after = [w for w in blocks if w["t_write_start_ns"] >= s["t_stop"]]
        if not before or not after:
            continue
        last = before[-1]
        nxt = after[0]
        fill_post = B - last["post_avail"]
        t_dry = last["t_write_end_ns"] + fill_post / fs * 1e9
        margin_ms = (t_dry - s["t_stop"]) / 1e6
        late_ms = (nxt["t_write_start_ns"] - t_dry) / 1e6
        # The write that underflowed reports it; the flag may also land on
        # a later write within the backlog burst, so look at a few.
        burst = after[:8]
        uf = int(any(w["underflow"] for w in burst))
        empty = int(any(w["pre_avail"] >= B for w in burst))
        last_block = [b for b in blocks if b["t_write_end_ns"] <= s["t_stop"]]
        gap_ms = ""
        if gaps and last_block:
            ref_pos = last_block[-1]["ref_end"]
            near = [g for g in gaps if abs(g[0] - ref_pos) <= 4096]
            gap_ms = f"{sum(g[1] for g in near):.3f}" if near else "0"
        print(f"{s['idx']}\t{s['req']:.1f}\t{s['actual']:.2f}\t{margin_ms:.2f}\t{late_ms:.2f}\t"
              f"{int(late_ms > 0)}\t{uf}\t{empty}\t{gap_ms}\t{nxt['queue_len']}")


if __name__ == "__main__":
    main()
