#!/usr/bin/env python3
"""Measure the sample-clock frequency offset of macOS audio interfaces.

Given an output device cabled to the input of a second device, this plays
a tone at each of several nominal sample rates and reports, in ppm:

  * the output device's DAC clock offset,
  * the input device's ADC clock offset.

Both are measured against the host clock from PortAudio's per-callback
DAC/ADC timestamps, and cross-checked against the frequency of the
recorded tone, which is independent of the host clock.  Three
measurements of two unknowns leaves a spare degree of freedom for a
consistency test; see `clock_offset_analyze.py` for the estimators.

The devices are named by PortAudio index -- the same numbering
airspy-fmradion's `-P` option uses.  Run with `--list-devices` to see it.

    clock_offset_measure.py --list-devices
    clock_offset_measure.py --out-device 1 --in-device 2 --seconds 60

Requires `sounddevice`, `numpy`; `soundfile` only for `--save-audio`.
"""

from __future__ import annotations

import argparse
import dataclasses
import math
import re
import sys
import time
from pathlib import Path

import numpy as np

# The two companion modules live next to this file; make them importable
# no matter which directory the tool is run from.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import clock_offset_analyze as ana  # noqa: E402
import coreaudio_rate as ca  # noqa: E402

DEFAULT_RATES = (44100, 48000, 96000)

# Devices whose "clock" is a host timer rather than a hardware oscillator.
# Measuring one answers a different question than the user is asking, so
# they need --allow-virtual.
VIRTUAL_NAME_RE = re.compile(
    r"aggregate|loopback|blackhole|soundflower|multi-output|virtual|"
    r"soundsource|audio\s*hijack|wsjtx",
    re.IGNORECASE,
)


@dataclasses.dataclass(frozen=True)
class Config:
    """Everything the capture needs, fixed once at startup."""

    out_device: int
    in_device: int
    rates: tuple[int, ...]
    seconds: float
    amplitude: float
    latency: str | float
    blocksize: int
    settle_seconds: float
    in_channel: int | None
    out_dir: Path
    save_audio: bool
    allow_virtual: bool
    keep_rate: bool


@dataclasses.dataclass
class Capture:
    """Raw capture for one nominal rate, before any estimation."""

    rate: int
    tone_hz: float
    out_nominal_rate: float
    in_nominal_rate: float
    out_frames: np.ndarray
    out_times: np.ndarray
    in_frames: np.ndarray
    in_times: np.ndarray
    recording: np.ndarray
    mono_times: np.ndarray
    real_times: np.ndarray
    xruns: int
    resampled: bool
    warnings: list[str] = dataclasses.field(default_factory=list)

    def as_npz_dict(self) -> dict:
        return {
            "rate": self.rate,
            "tone_hz": self.tone_hz,
            "out_nominal_rate": self.out_nominal_rate,
            "in_nominal_rate": self.in_nominal_rate,
            "out_frames": self.out_frames,
            "out_times": self.out_times,
            "in_frames": self.in_frames,
            "in_times": self.in_times,
            "recording": self.recording.astype(np.float32),
            "mono_times": self.mono_times,
            "real_times": self.real_times,
            "xruns": self.xruns,
            "resampled": self.resampled,
        }


class BlockLog:
    """Pre-allocated log of (frame index, timestamp, status) per callback.

    The callbacks run on a CoreAudio realtime thread, where a `list.append`
    can hit a backing-array realloc and a `queue.Queue.put` takes a lock.
    Neither is fatal in Python -- the GIL is already the bigger problem --
    but both are avoidable: the capacity is known in advance from the run
    length, so the arrays are allocated once and only written into.

    Overrunning the capacity drops rows rather than growing or raising; a
    callback that raises is worse than one that loses a row, and the
    overflow is reported afterwards.
    """

    def __init__(self, capacity: int):
        self.frames = np.zeros(capacity, dtype=np.int64)
        self.times = np.zeros(capacity, dtype=np.float64)
        self.status = np.zeros(capacity, dtype=np.uint8)
        self.count = 0
        self.dropped = 0

    def append(self, frame: int, timestamp: float, status: int) -> None:
        i = self.count
        if i >= len(self.frames):
            self.dropped += 1
            return
        self.frames[i] = frame
        self.times[i] = timestamp
        self.status[i] = status
        self.count = i + 1

    def view(self) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
        n = self.count
        return self.frames[:n], self.times[:n], self.status[:n]


# ---------------------------------------------------------------------------
# Device listing and validation
# ---------------------------------------------------------------------------


def list_devices() -> None:
    """Print PortAudio indices alongside what CoreAudio says about them."""
    import sounddevice as sd

    ca_devices = {d.name.strip(): d for d in ca.list_devices()}
    print("PortAudio devices (use these indices; same numbering as fmradion -P)")
    print()
    print(
        "%3s  %-30s %3s %3s  %-9s %s"
        % ("idx", "name", "in", "out", "current", "hardware rates")
    )
    print("-" * 96)
    for index, info in enumerate(sd.query_devices()):
        name = info["name"].strip()
        found = ca_devices.get(name)
        current = "%g" % found.nominal_rate if found else "?"
        rates = (
            ",".join("%g" % r for r in found.available_rates) if found else "unknown"
        )
        flag = " *" if VIRTUAL_NAME_RE.search(name) else ""
        print(
            "%3d  %-30s %3d %3d  %-9s %s%s"
            % (
                index,
                name,
                info["max_input_channels"],
                info["max_output_channels"],
                current,
                rates,
                flag,
            )
        )
    print()
    print("* virtual or aggregate device: its clock is a host timer, not an")
    print("  independent oscillator, so measuring it does not describe any")
    print("  hardware. clock_offset_measure.py needs --allow-virtual to use one.")


def validate(config: Config) -> tuple[ca.CoreAudioDevice, ca.CoreAudioDevice]:
    """Check the device pair and resolve both to CoreAudio devices."""
    import sounddevice as sd

    if config.out_device == config.in_device:
        raise SystemExit(
            "error: output and input must be different devices. The point of the "
            "measurement is to compare two independent clocks; one device's own "
            "loopback shares a single oscillator and would read 0 ppm by "
            "construction."
        )
    try:
        out_info = sd.query_devices(config.out_device)
        in_info = sd.query_devices(config.in_device)
    except (ValueError, sd.PortAudioError) as err:
        raise SystemExit("error: %s (try --list-devices)" % err)

    if out_info["max_output_channels"] < 1:
        raise SystemExit(
            "error: PortAudio device %d (%s) has no output channels"
            % (config.out_device, out_info["name"])
        )
    if in_info["max_input_channels"] < 1:
        raise SystemExit(
            "error: PortAudio device %d (%s) has no input channels"
            % (config.in_device, in_info["name"])
        )
    for label, info in (("output", out_info), ("input", in_info)):
        if VIRTUAL_NAME_RE.search(info["name"]) and not config.allow_virtual:
            raise SystemExit(
                "error: %s device %r looks like a virtual or aggregate device. "
                "Its sample clock is derived from a host timer rather than its "
                "own oscillator, so the measurement would not mean what you "
                "want. Pass --allow-virtual to override." % (label, info["name"])
            )

    out_ca = ca.find_by_portaudio_index(config.out_device, "output")
    in_ca = ca.find_by_portaudio_index(config.in_device, "input")
    return out_ca, in_ca


# ---------------------------------------------------------------------------
# Capture
# ---------------------------------------------------------------------------


def capture_one_rate(config: Config, rate: int,
                     out_ca: ca.CoreAudioDevice,
                     in_ca: ca.CoreAudioDevice) -> Capture | None:
    """Play and record a tone at one nominal rate.

    Both devices are switched to `rate` first.  This is the step that
    makes the measurement mean anything: PortAudio will happily open a
    stream at a rate the hardware cannot produce, and macOS inserts a
    resampler without saying so, at which point the fitted offset
    describes the resampler rather than the clock.  PortAudio reports the
    requested rate back through `stream.samplerate` either way, so only
    CoreAudio itself can be trusted here.
    """
    import sounddevice as sd

    warnings: list[str] = []
    for label, device in (("output", out_ca), ("input", in_ca)):
        if not device.supports(rate):
            print(
                "  skipping %g Hz: %s (%s) does not support it in hardware "
                "(supports %s)"
                % (
                    rate,
                    device.name,
                    label,
                    ",".join("%g" % r for r in device.available_rates),
                )
            )
            return None

    tone_hz = ana.choose_tone_frequency(rate)
    total_frames = int(round(rate * config.seconds))

    # Capacity assumes the host may pick blocks as small as 32 frames.
    capacity = int(total_frames / 32) + 4096
    out_log = BlockLog(capacity)
    in_log = BlockLog(capacity)

    n = np.arange(total_frames, dtype=np.float64)
    tone = (config.amplitude * np.sin(2.0 * math.pi * tone_hz * n / rate)).astype(
        np.float32
    )
    play = np.repeat(tone[:, None], 2, axis=1)

    in_channels = min(2, sd.query_devices(config.in_device)["max_input_channels"])
    # Room for the settle time plus a margin, so recording never wraps.
    record_capacity = int(round(rate * (config.seconds + config.settle_seconds + 4.0)))
    recording = np.zeros((record_capacity, in_channels), dtype=np.float32)

    state = {"out_pos": 0, "in_pos": 0, "done": False}

    def out_callback(outdata, frames, time_info, status):
        bits = (int(status.output_underflow) << 0) | (int(status.output_overflow) << 1)
        pos = state["out_pos"]
        out_log.append(pos, time_info.outputBufferDacTime, bits)
        remaining = total_frames - pos
        if remaining <= 0:
            outdata.fill(0.0)
            state["done"] = True
        else:
            m = min(frames, remaining)
            outdata[:m] = play[pos : pos + m]
            if m < frames:
                outdata[m:] = 0.0
                state["done"] = True
        state["out_pos"] = pos + frames

    def in_callback(indata, frames, time_info, status):
        bits = (int(status.input_overflow) << 2) | (int(status.input_underflow) << 3)
        pos = state["in_pos"]
        in_log.append(pos, time_info.inputBufferAdcTime, bits)
        end = pos + frames
        if end <= record_capacity:
            recording[pos:end] = indata
        state["in_pos"] = end

    # Switching the nominal rate is a system-wide change, so it is undone
    # on the way out unless the user asked otherwise.
    out_ctx = ca.NominalRate(out_ca.device_id, rate)
    in_ctx = ca.NominalRate(in_ca.device_id, rate)
    out_stream = in_stream = None
    mono_times: list[float] = []
    real_times: list[float] = []

    try:
        out_ctx.__enter__()
        in_ctx.__enter__()
        # Re-read: a device that another application holds open can refuse
        # the change while still returning success.
        out_actual = ca.get_nominal_rate(out_ca.device_id)
        in_actual = ca.get_nominal_rate(in_ca.device_id)
        resampled = abs(out_actual - rate) > 0.5 or abs(in_actual - rate) > 0.5
        if resampled:
            warnings.append(
                "device rate is %g/%g Hz, not %g Hz -- CoreAudio is resampling"
                % (out_actual, in_actual, rate)
            )

        latency = config.latency
        out_stream = sd.OutputStream(
            device=config.out_device,
            samplerate=rate,
            channels=2,
            dtype="float32",
            blocksize=config.blocksize,
            latency=latency,
            callback=out_callback,
        )
        in_stream = sd.InputStream(
            device=config.in_device,
            samplerate=rate,
            channels=in_channels,
            dtype="float32",
            blocksize=config.blocksize,
            latency=latency,
            callback=in_callback,
        )
        print(
            "  granted latency: output %.4f s, input %.4f s"
            % (out_stream.latency, in_stream.latency)
        )

        in_stream.start()
        time.sleep(config.settle_seconds)
        out_stream.start()

        # Sample both clocks from the main thread while the streams run.
        # No CallbackStop is raised anywhere: letting the callbacks pad
        # with silence past the end and stopping from here avoids the
        # stop-vs-callback race that can wedge Pa_StopStream.
        deadline = time.monotonic() + config.seconds + config.settle_seconds + 5.0
        while not state["done"] and time.monotonic() < deadline:
            mono_times.append(time.monotonic())
            real_times.append(time.time())
            time.sleep(0.2)
        time.sleep(0.5)
    finally:
        for stream in (out_stream, in_stream):
            if stream is not None:
                stream.abort(ignore_errors=True)
                stream.close(ignore_errors=True)
        if not config.keep_rate:
            in_ctx.__exit__(None, None, None)
            out_ctx.__exit__(None, None, None)

    out_frames, out_times, out_status = out_log.view()
    in_frames, in_times, in_status = in_log.view()
    xruns = int(np.count_nonzero(out_status)) + int(np.count_nonzero(in_status))
    if xruns:
        warnings.append("%d callbacks reported over/underflow" % xruns)
    if out_log.dropped or in_log.dropped:
        warnings.append(
            "timestamp log overflowed (%d + %d rows lost)"
            % (out_log.dropped, in_log.dropped)
        )
    if len(out_frames) < 16 or len(in_frames) < 16:
        warnings.append("too few callbacks captured; the stream may not have run")
        print("  error: captured only %d/%d callbacks" % (len(out_frames), len(in_frames)))
        return None

    recorded = recording[: state["in_pos"]]
    if config.in_channel is not None:
        if config.in_channel >= recorded.shape[1]:
            raise SystemExit(
                "error: --in-channel %d but only %d channels were opened"
                % (config.in_channel, recorded.shape[1])
            )
        channel = config.in_channel
    else:
        # Pick whichever input the cable is actually plugged into.
        channel = int(np.argmax(np.sqrt(np.mean(recorded**2, axis=0))))
    mono = recorded[:, channel].astype(np.float64)

    rms = float(np.sqrt(np.mean(mono**2))) if len(mono) else 0.0
    peak = float(np.max(np.abs(mono))) if len(mono) else 0.0
    print(
        "  recorded %.2f s on channel %d: %.1f dBFS rms, peak %.3f"
        % (len(mono) / rate, channel, 20 * math.log10(rms) if rms > 0 else -999, peak)
    )
    if peak > 0.98:
        warnings.append("recording is clipping (peak %.3f)" % peak)
    # A line output feeding a line input through a volume control is
    # routinely 20-30 dB down, and that costs the estimator nothing --
    # only signal-to-noise matters, and the report shows it. So warn only
    # at a level low enough to actually threaten the fit.
    if rms < 0.01 * config.amplitude:
        warnings.append(
            "recorded level is %.1f dB below the played amplitude; check cabling "
            "and input gain" % (20 * math.log10(max(rms, 1e-12) / config.amplitude))
        )

    return Capture(
        rate=rate,
        tone_hz=tone_hz,
        out_nominal_rate=float(rate),
        in_nominal_rate=float(rate),
        out_frames=out_frames.astype(np.float64),
        out_times=out_times,
        in_frames=in_frames.astype(np.float64),
        in_times=in_times,
        recording=mono,
        mono_times=np.asarray(mono_times, dtype=np.float64),
        real_times=np.asarray(real_times, dtype=np.float64),
        xruns=xruns,
        resampled=resampled,
        warnings=warnings,
    )


# ---------------------------------------------------------------------------
# Driver
# ---------------------------------------------------------------------------


def run(config: Config) -> int:
    import sounddevice as sd

    out_ca, in_ca = validate(config)
    out_name = sd.query_devices(config.out_device)["name"].strip()
    in_name = sd.query_devices(config.in_device)["name"].strip()

    print("output : PortAudio %d = %s" % (config.out_device, out_ca))
    print("input  : PortAudio %d = %s" % (config.in_device, in_ca))
    print("run    : %g s per rate, %d rates" % (config.seconds, len(config.rates)))
    print()

    config.out_dir.mkdir(parents=True, exist_ok=True)
    all_warnings: list[str] = []
    reports = []

    for rate in config.rates:
        print("--- %g Hz ---" % rate)
        capture = capture_one_rate(config, rate, out_ca, in_ca)
        if capture is None:
            print()
            continue
        npz_path = config.out_dir / ("capture_%d.npz" % rate)
        np.savez_compressed(npz_path, **capture.as_npz_dict())
        print("  saved %s" % npz_path)

        if config.save_audio:
            try:
                import soundfile as sf

                wav_path = config.out_dir / ("capture_%d.wav" % rate)
                sf.write(wav_path, capture.recording, rate, subtype="FLOAT")
                print("  saved %s" % wav_path)
            except ImportError:
                print("  (soundfile not installed; --save-audio ignored)")

        try:
            result = ana.analyze_capture(capture.as_npz_dict())
        except ValueError as err:
            print("  analysis failed: %s" % err)
            all_warnings.append("%g Hz: %s" % (rate, err))
            print()
            continue
        reports.append(result)
        for warning in capture.warnings:
            all_warnings.append("%g Hz: %s" % (rate, warning))
        print()

    if not reports:
        print("No rate produced a usable measurement.")
        return 1

    for result in reports:
        print(ana.format_report(result, out_name, in_name))
        print()

    print("=" * 78)
    print("  Summary: clock offset in ppm (combined estimate)")
    print("=" * 78)
    print("%10s  %24s  %24s" % ("rate", out_name[:24] + " (out)", in_name[:24] + " (in)"))
    for result in reports:
        comb = result["combined"]
        flag = "" if comb.consistent else "  <- discrepant"
        print(
            "%10g  %+15.3f +/- %-6.3f  %+15.3f +/- %-6.3f%s"
            % (result["rate"], comb.dac_ppm, comb.dac_se,
               comb.adc_ppm, comb.adc_se, flag)
        )

    if all_warnings:
        print()
        print("Warnings:")
        for warning in all_warnings:
            print("  - %s" % warning)
    return 0


def parse_args(argv: list[str]) -> tuple[Config | None, argparse.Namespace]:
    parser = argparse.ArgumentParser(
        description=__doc__.splitlines()[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Cable the output device's line output to the input device's "
        "line input before running.",
    )
    parser.add_argument("--list-devices", action="store_true",
                        help="list PortAudio devices with their hardware rates")
    parser.add_argument("--out-device", type=int,
                        help="PortAudio index of the device to play from")
    parser.add_argument("--in-device", type=int,
                        help="PortAudio index of the device to record on")
    parser.add_argument("--rates", default=",".join(str(r) for r in DEFAULT_RATES),
                        help="comma-separated nominal rates (default: %(default)s)")
    parser.add_argument("--seconds", type=float, default=60.0,
                        help="capture length per rate (default: %(default)s)")
    parser.add_argument("--amplitude", type=float, default=0.1,
                        help="tone amplitude, full scale (default: %(default)s)")
    parser.add_argument("--latency", default="low",
                        help="'low', 'high', or seconds (default: %(default)s)")
    parser.add_argument("--blocksize", type=int, default=0,
                        help="frames per callback, 0 = host default (default: 0)")
    parser.add_argument("--settle-seconds", type=float, default=1.0,
                        help="delay between starting input and output streams")
    parser.add_argument("--in-channel", type=int, default=None,
                        help="input channel to analyse (default: loudest)")
    parser.add_argument("--out-dir", type=Path, default=Path("clock_offset_data"),
                        help="where to write captures (default: %(default)s)")
    parser.add_argument("--save-audio", action="store_true",
                        help="also write the recording as a WAV file")
    parser.add_argument("--allow-virtual", action="store_true",
                        help="permit aggregate or virtual devices")
    parser.add_argument("--keep-rate", action="store_true",
                        help="leave devices at the last rate instead of restoring")
    args = parser.parse_args(argv)

    if args.list_devices:
        return None, args
    if args.out_device is None or args.in_device is None:
        parser.error("--out-device and --in-device are required "
                     "(see --list-devices)")
    if not 0.0 < args.amplitude <= 1.0:
        parser.error("--amplitude must be in (0, 1]")
    if args.seconds <= 2.0:
        parser.error("--seconds must be more than 2")

    latency: str | float = args.latency
    if latency not in ("low", "high"):
        try:
            latency = float(latency)
        except ValueError:
            parser.error("--latency must be 'low', 'high', or a number of seconds")

    config = Config(
        out_device=args.out_device,
        in_device=args.in_device,
        rates=tuple(int(r) for r in args.rates.split(",") if r.strip()),
        seconds=args.seconds,
        amplitude=args.amplitude,
        latency=latency,
        blocksize=args.blocksize,
        settle_seconds=args.settle_seconds,
        in_channel=args.in_channel,
        out_dir=args.out_dir,
        save_audio=args.save_audio,
        allow_virtual=args.allow_virtual,
        keep_rate=args.keep_rate,
    )
    return config, args


def main(argv: list[str]) -> int:
    config, args = parse_args(argv)
    if args.list_devices:
        list_devices()
        return 0
    assert config is not None
    try:
        return run(config)
    except KeyboardInterrupt:
        print("\ninterrupted", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
