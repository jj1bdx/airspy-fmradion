#!/usr/bin/env python3
"""Summarize a T7 block trace (doc/LINUX_UNDERFLOW_RESULTS_20261020_trace_format.md).

Usage: trace_stats.py RUNDIR [--skip SECONDS]

Prints: ring size B, underflow and ring-empty counts, the fill before each
write (the margin), the scheduling delay t_write_start - t_push and its
parts, decode wall and CPU time, the source push interval, and the source
sample rate against CLOCK_MONOTONIC in ppm (least-squares fit of push time
against cumulative frames). Also writes RUNDIR/trace_summary.json.
"""
import csv
import json
import os
import sys


def load(path):
    header, data = {}, []
    with open(path) as f:
        for line in f:
            if line.startswith("#"):
                key, _, value = line[2:].strip().partition("=")
                header[key] = value
            else:
                data.append(line)
    rows = []
    for r in csv.DictReader(data):
        rows.append({k: (v if k == "type" else int(v)) for k, v in r.items()})
    return header, rows


def pct(sorted_vals, p):
    if not sorted_vals:
        return float("nan")
    k = min(len(sorted_vals) - 1, max(0, int(round(p / 100.0 * (len(sorted_vals) - 1)))))
    return sorted_vals[k]


def fit_ppm(ts, xs):
    """Slope of t (ns) against x (frames); returns (ns_per_frame, n)."""
    n = len(ts)
    mx = sum(xs) / n
    mt = sum(ts) / n
    sxx = sum((x - mx) ** 2 for x in xs)
    sxt = sum((x - mx) * (t - mt) for x, t in zip(xs, ts))
    return sxt / sxx, n


def main():
    rundir = sys.argv[1]
    skip_s = 0.0
    if "--skip" in sys.argv:
        skip_s = float(sys.argv[sys.argv.index("--skip") + 1])
    header, rows = load(os.path.join(rundir, "trace.csv"))
    fs = float(header.get("pa_stream_sample_rate_hz", "48000") or 48000)
    B = int(header.get("pa_write_available_after_start_frames", "-1") or -1)
    blocks = [r for r in rows if r["type"] == "B"]
    blocks.sort(key=lambda r: r["t_write_start_ns"])
    t0 = blocks[0]["t_push_ns"]
    out = {"rundir": rundir, "B": B, "fs": fs,
           "pa_output_latency_ms": header.get("pa_output_latency_ms"),
           "pa_device_name": header.get("pa_device_name"),
           "records": len(blocks)}
    sel = [r for r in blocks if (r["t_push_ns"] - t0) / 1e9 >= skip_s]
    dur = (sel[-1]["t_write_end_ns"] - sel[0]["t_push_ns"]) / 1e9
    out["duration_s"] = dur

    # Underflow flags and ring-empty detection.
    uf = [r for r in sel if r["underflow"] == 1]
    have_avail = [r for r in sel if r["pre_avail"] >= 0]
    Bx = max([B] + [r["pre_avail"] for r in have_avail] + [r["post_avail"] for r in sel])
    empty = [r for r in have_avail if r["pre_avail"] >= Bx]
    out["underflow_flags"] = len(uf)
    out["ring_empty_before_write"] = len(empty)
    out["B_max_seen"] = Bx
    out["uf_times_s"] = [round((r["t_write_start_ns"] - t0) / 1e9, 3) for r in uf][:50]
    out["empty_times_s"] = [round((r["t_write_start_ns"] - t0) / 1e9, 3) for r in empty][:50]

    # Fill before each write (ms).
    fill = sorted(1000.0 * (Bx - r["pre_avail"]) / fs for r in have_avail)
    out["fill_before_ms"] = {p: round(pct(fill, p), 3) for p in (0, 0.1, 1, 5, 50, 95)}
    for th in (2, 4, 6, 8, 10):
        out[f"fill_before_lt_{th}ms"] = sum(1 for v in fill if v < th)

    # Delays (ms).
    def dist(vals):
        v = sorted(vals)
        return {"p50": round(pct(v, 50), 3), "p99": round(pct(v, 99), 3),
                "p99.9": round(pct(v, 99.9), 3), "max": round(v[-1], 3)}

    sched = [(r["t_write_start_ns"] - r["t_push_ns"]) / 1e6 for r in sel]
    wait = [(r["t_pull_ns"] - r["t_push_ns"]) / 1e6 for r in sel]
    dec_wall = [(r["t_decode_end_ns"] - r["t_pull_ns"]) / 1e6 for r in sel]
    dec_cpu = [(r["cpu_decode_end_ns"] - r["cpu_pull_ns"]) / 1e6 for r in sel]
    wr = [(r["t_write_end_ns"] - r["t_write_start_ns"]) / 1e6 for r in sel]
    out["sched_delay_ms"] = dist(sched)
    out["pull_wait_ms"] = dist(wait)
    out["decode_wall_ms"] = dist(dec_wall)
    out["decode_cpu_ms"] = dist(dec_cpu)
    out["decode_wall_minus_cpu_ms"] = dist([a - b for a, b in zip(dec_wall, dec_cpu)])
    out["write_ms"] = dist(wr)
    per_h = 3600.0 / dur if dur > 0 else float("nan")
    out["sched_delay_count_per_hour"] = {
        th: round(sum(1 for v in sched if v > th) * per_h, 1) for th in (2, 5, 8, 10, 20, 30, 50)}
    out["queue_len_max"] = max(r["queue_len"] for r in sel)

    # Source push interval and rate against CLOCK_MONOTONIC.
    pushes = [r for r in sel if r["t_push_ns"] > 0]
    iv = [(b["t_push_ns"] - a["t_push_ns"]) / 1e6 for a, b in zip(pushes, pushes[1:])]
    out["push_interval_ms"] = dist(iv)
    out["push_interval_ms"]["min"] = round(min(iv), 3)
    cum, xs = 0, []
    for r in pushes:
        xs.append(cum)
        cum += r["frames"]
    ns_per_frame, n = fit_ppm([r["t_push_ns"] for r in pushes], xs)
    rate = 1e9 / ns_per_frame
    out["source_audio_rate_hz"] = round(rate, 6)
    out["source_rate_ppm_vs_monotonic"] = round((rate / fs - 1.0) * 1e6, 3)
    # Ring-fill slope, frames per second, from post-write fill
    # (positive: the output consumes slower than the source produces).
    fills = [(r["t_write_end_ns"], Bx - r["post_avail"]) for r in sel if r["post_avail"] >= 0]
    if len(fills) > 10:
        ts = [t for t, _ in fills]
        fs_ = [float(f) for _, f in fills]
        slope, _ = fit_ppm(fs_, ts)  # frames per ns, by reusing the fit
        out["post_fill_slope_frames_per_s"] = round(slope * 1e9, 4)

    for k, v in out.items():
        print(f"{k}: {v}")
    with open(os.path.join(rundir, "trace_summary.json"), "w") as f:
        json.dump(out, f, indent=1)


if __name__ == "__main__":
    main()
