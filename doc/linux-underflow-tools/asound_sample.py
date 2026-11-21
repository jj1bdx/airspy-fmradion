#!/usr/bin/env python3
"""Sample an ALSA PCM substream through /proc/asound.

Usage: asound_sample.py PCMDIR OUTDIR [interval_ms] [timeout_s]
  PCMDIR: for example /proc/asound/card2/pcm0p

Waits until a substream under PCMDIR is open, saves its hw_params and
sw_params once (and again when they change, e.g. after a restart), then
polls its status every interval_ms until the substream closes. Writes
OUTDIR/asound_status.tsv with CLOCK_MONOTONIC ns, state, avail,
avail_max (max since the previous read), delay, hw_ptr, appl_ptr.
"""
import glob
import os
import sys
import time


def read(path):
    try:
        with open(path) as f:
            return f.read()
    except OSError:
        return ""


def parse(text):
    out = {}
    for line in text.splitlines():
        if ":" in line:
            k, v = line.split(":", 1)
            out[k.strip()] = v.strip()
    return out


def main():
    pcmdir, outdir = sys.argv[1], sys.argv[2]
    interval = (float(sys.argv[3]) if len(sys.argv) > 3 else 20.0) / 1000.0
    timeout = float(sys.argv[4]) if len(sys.argv) > 4 else 60.0
    t_end = time.monotonic() + timeout
    sub = None
    while sub is None and time.monotonic() < t_end:
        for s in sorted(glob.glob(os.path.join(pcmdir, "sub*"))):
            if read(os.path.join(s, "status")).strip() != "closed":
                sub = s
                break
        if sub is None:
            time.sleep(0.005)
    if sub is None:
        print("asound_sample: no open substream", file=sys.stderr)
        return 1
    last_hw = None
    n_params = 0
    with open(os.path.join(outdir, "asound_status.tsv"), "w") as f:
        f.write(f"#sub={sub}\n")
        f.write("t_ns\tstate\tavail\tavail_max\tdelay\thw_ptr\tappl_ptr\n")
        next_t = time.monotonic()
        while True:
            t = time.clock_gettime_ns(time.CLOCK_MONOTONIC)
            st = read(os.path.join(sub, "status"))
            if st.strip() == "closed" or not st:
                break
            hw = read(os.path.join(sub, "hw_params"))
            if hw != last_hw and hw.strip() not in ("", "closed"):
                with open(os.path.join(outdir, f"asound_params_{n_params}.txt"), "w") as p:
                    p.write(f"t_ns {t}\n{sub}\n--- hw_params\n{hw}--- sw_params\n")
                    p.write(read(os.path.join(sub, "sw_params")))
                last_hw = hw
                n_params += 1
            d = parse(st)
            f.write("\t".join([str(t), d.get("state", ""), d.get("avail", ""),
                               d.get("avail_max", ""), d.get("delay", ""),
                               d.get("hw_ptr", ""), d.get("appl_ptr", "")]) + "\n")
            next_t += interval
            delay = next_t - time.monotonic()
            if delay > 0:
                time.sleep(delay)
            else:
                next_t = time.monotonic()
    return 0


if __name__ == "__main__":
    sys.exit(main())
