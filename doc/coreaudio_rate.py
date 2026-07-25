#!/usr/bin/env python3
"""CoreAudio device enumeration and nominal-sample-rate control (macOS).

PortAudio will open a stream at *any* sample rate a CoreAudio device
claims to accept, and macOS silently inserts an AudioConverter when the
requested rate is not the device's current nominal rate.  The stream then
runs at the requested rate while the hardware keeps running at its own,
so a clock-offset measurement made on that stream describes the
resampler, not the converter.  On this author's Roland Rubix24 the
difference is 10 ppm at 48 kHz, and 88.2 kHz is not supported by the
hardware at all even though PortAudio accepts it.

This module talks to CoreAudio directly through ctypes so that the
measurement tools can

  * enumerate devices with their real ``AudioDeviceID``, name, UID,
    current nominal rate and the list of rates the hardware supports;
  * set the nominal rate and verify that the change took effect;
  * map a PortAudio device index onto the corresponding CoreAudio device.

Standalone usage:
    coreaudio_rate.py                 # list devices
    coreaudio_rate.py --set 2 96000   # set PortAudio device 2 to 96 kHz
"""

from __future__ import annotations

import ctypes
import ctypes.util
import struct
import sys
import time
from dataclasses import dataclass, field

# ---------------------------------------------------------------------------
# CoreAudio / CoreFoundation plumbing
# ---------------------------------------------------------------------------

_ca_path = ctypes.util.find_library("CoreAudio")
_cf_path = ctypes.util.find_library("CoreFoundation")
if _ca_path is None or _cf_path is None:  # pragma: no cover - non-macOS
    raise ImportError("CoreAudio is only available on macOS")

_ca = ctypes.CDLL(_ca_path)
_cf = ctypes.CDLL(_cf_path)

_cf.CFStringGetCString.argtypes = [
    ctypes.c_void_p,
    ctypes.c_char_p,
    ctypes.c_long,
    ctypes.c_uint32,
]
_cf.CFStringGetCString.restype = ctypes.c_bool
_cf.CFRelease.argtypes = [ctypes.c_void_p]

_kCFStringEncodingUTF8 = 0x08000100
_kAudioObjectSystemObject = 1


def _fourcc(code: str) -> int:
    """'nsrt' -> 0x6E737274, the way CoreAudio spells its selectors."""
    return struct.unpack(">I", code.encode("ascii"))[0]


class _AOPA(ctypes.Structure):
    """AudioObjectPropertyAddress."""

    _fields_ = [
        ("mSelector", ctypes.c_uint32),
        ("mScope", ctypes.c_uint32),
        ("mElement", ctypes.c_uint32),
    ]


# Selectors used below.  The scope 'glob' is kAudioObjectPropertyScopeGlobal;
# 'inpt'/'outp' select the input/output side of a duplex device.
_SEL_DEVICES = "dev#"  # kAudioHardwarePropertyDevices
_SEL_NAME = "lnam"  # kAudioObjectPropertyName            (CFStringRef)
_SEL_UID = "uid "  # kAudioDevicePropertyDeviceUID       (CFStringRef)
_SEL_NOMINAL = "nsrt"  # kAudioDevicePropertyNominalSampleRate      (Float64)
_SEL_AVAILABLE = "nsr#"  # ...AvailableNominalSampleRates  (AudioValueRange[])
_SEL_STREAMS = "stm#"  # kAudioDevicePropertyStreams
_SCOPE_GLOBAL = "glob"
_SCOPE_INPUT = "inpt"
_SCOPE_OUTPUT = "outp"


def _get_property(obj: int, selector: str, scope: str = _SCOPE_GLOBAL) -> bytes | None:
    """Return the raw bytes of a CoreAudio property, or None if absent."""
    addr = _AOPA(_fourcc(selector), _fourcc(scope), 0)
    size = ctypes.c_uint32(0)
    if _ca.AudioObjectGetPropertyDataSize(
        ctypes.c_uint32(obj), ctypes.byref(addr), 0, None, ctypes.byref(size)
    ):
        return None
    buf = (ctypes.c_char * size.value)()
    if _ca.AudioObjectGetPropertyData(
        ctypes.c_uint32(obj), ctypes.byref(addr), 0, None, ctypes.byref(size), buf
    ):
        return None
    return buf.raw[: size.value]


def _cfstring(raw: bytes | None) -> str:
    """Decode a CFStringRef returned by value in a property buffer."""
    if not raw or len(raw) < ctypes.sizeof(ctypes.c_void_p):
        return ""
    ref = ctypes.c_void_p(struct.unpack("<Q", raw[:8])[0])
    buf = (ctypes.c_char * 512)()
    if not _cf.CFStringGetCString(ref, buf, 512, _kCFStringEncodingUTF8):
        return ""
    return buf.value.decode("utf-8", "replace")


# ---------------------------------------------------------------------------
# Device model
# ---------------------------------------------------------------------------


@dataclass
class CoreAudioDevice:
    """One CoreAudio device, as the hardware layer sees it."""

    device_id: int
    name: str
    uid: str
    nominal_rate: float
    available_rates: list[float] = field(default_factory=list)
    has_input: bool = False
    has_output: bool = False

    def supports(self, rate: float, tol: float = 0.5) -> bool:
        """True if the hardware itself can run at `rate`."""
        return any(abs(rate - r) <= tol for r in self.available_rates)

    def __str__(self) -> str:
        sides = ("in" if self.has_input else "") + ("out" if self.has_output else "")
        rates = ",".join("%g" % r for r in self.available_rates)
        return "id=%d %-30s [%-5s] now=%g supports=%s" % (
            self.device_id,
            self.name,
            sides,
            self.nominal_rate,
            rates,
        )


def _has_streams(device_id: int, scope: str) -> bool:
    raw = _get_property(device_id, _SEL_STREAMS, scope)
    return bool(raw) and len(raw) >= 4


def list_devices() -> list[CoreAudioDevice]:
    """Enumerate every CoreAudio device on the system."""
    raw = _get_property(_kAudioObjectSystemObject, _SEL_DEVICES)
    if not raw:
        return []
    ids = struct.unpack("<%dI" % (len(raw) // 4), raw)
    devices = []
    for dev_id in ids:
        nominal_raw = _get_property(dev_id, _SEL_NOMINAL)
        if nominal_raw is None or len(nominal_raw) < 8:
            continue  # not a real audio device
        rates_raw = _get_property(dev_id, _SEL_AVAILABLE) or b""
        # AudioValueRange is a pair of Float64 (minimum, maximum).  Discrete
        # rates come back as degenerate ranges; ranges proper (a few USB
        # devices report them) are represented by their maximum.
        rates = []
        for i in range(len(rates_raw) // 16):
            lo, hi = struct.unpack("<dd", rates_raw[i * 16 : i * 16 + 16])
            rates.append(hi)
        devices.append(
            CoreAudioDevice(
                device_id=dev_id,
                name=_cfstring(_get_property(dev_id, _SEL_NAME)),
                uid=_cfstring(_get_property(dev_id, _SEL_UID)),
                nominal_rate=struct.unpack("<d", nominal_raw[:8])[0],
                available_rates=sorted(rates),
                has_input=_has_streams(dev_id, _SCOPE_INPUT),
                has_output=_has_streams(dev_id, _SCOPE_OUTPUT),
            )
        )
    return devices


def get_nominal_rate(device_id: int) -> float:
    """Current nominal sample rate of a CoreAudio device, in Hz."""
    raw = _get_property(device_id, _SEL_NOMINAL)
    if raw is None or len(raw) < 8:
        raise RuntimeError("cannot read nominal rate of CoreAudio device %d" % device_id)
    return struct.unpack("<d", raw[:8])[0]


def set_nominal_rate(
    device_id: int, rate: float, timeout: float = 5.0, tol: float = 0.5
) -> float:
    """Set the nominal rate and wait until the device reports it.

    The change is asynchronous: CoreAudio returns before the hardware has
    relocked, and reading the property back immediately can still yield
    the old value.  Returns the rate the device settled on; raises if it
    never gets there.
    """
    addr = _AOPA(_fourcc(_SEL_NOMINAL), _fourcc(_SCOPE_GLOBAL), 0)
    value = ctypes.c_double(float(rate))
    status = _ca.AudioObjectSetPropertyData(
        ctypes.c_uint32(device_id),
        ctypes.byref(addr),
        0,
        None,
        ctypes.c_uint32(ctypes.sizeof(value)),
        ctypes.byref(value),
    )
    if status != 0:
        raise RuntimeError(
            "AudioObjectSetPropertyData(rate=%g) on device %d failed with OSStatus %d "
            "(another application may hold the device)" % (rate, device_id, status)
        )
    deadline = time.monotonic() + timeout
    current = get_nominal_rate(device_id)
    while abs(current - rate) > tol and time.monotonic() < deadline:
        time.sleep(0.05)
        current = get_nominal_rate(device_id)
    if abs(current - rate) > tol:
        raise RuntimeError(
            "device %d did not switch to %g Hz (still %g Hz after %.1f s)"
            % (device_id, rate, current, timeout)
        )
    return current


def find_by_portaudio_index(index: int, kind: str) -> CoreAudioDevice:
    """Map a PortAudio device index onto its CoreAudio device.

    PortAudio does not expose the AudioDeviceID it is using, so the match
    is made on the device name, which PortAudio copies verbatim from
    CoreAudio, disambiguated by which side (input/output) is in use.

    `kind` is 'input' or 'output'.
    """
    import sounddevice as sd  # imported lazily: only needed for the mapping

    info = sd.query_devices(index)
    name = info["name"].strip()
    want_input = kind == "input"
    candidates = [
        d
        for d in list_devices()
        if d.name.strip() == name and (d.has_input if want_input else d.has_output)
    ]
    if not candidates:
        # Fall back to a name match alone; a few aggregate devices do not
        # advertise streams on the scope PortAudio uses.
        candidates = [d for d in list_devices() if d.name.strip() == name]
    if not candidates:
        raise RuntimeError(
            "no CoreAudio device matches PortAudio %s device %d (%r)"
            % (kind, index, name)
        )
    if len(candidates) > 1:
        print(
            "warning: %d CoreAudio devices are named %r; using id=%d"
            % (len(candidates), name, candidates[0].device_id),
            file=sys.stderr,
        )
    return candidates[0]


class NominalRate:
    """Context manager that sets a device's rate and restores it after.

    Leaving a device parked at 192 kHz because a measurement was
    interrupted is rude to whatever the user runs next, so the original
    rate is always put back.
    """

    def __init__(self, device_id: int, rate: float):
        self.device_id = device_id
        self.rate = rate
        self.previous: float | None = None

    def __enter__(self) -> float:
        self.previous = get_nominal_rate(self.device_id)
        if abs(self.previous - self.rate) > 0.5:
            set_nominal_rate(self.device_id, self.rate)
        return self.rate

    def __exit__(self, *exc) -> None:
        if self.previous is not None and abs(self.previous - self.rate) > 0.5:
            try:
                set_nominal_rate(self.device_id, self.previous)
            except RuntimeError as err:  # do not mask the original exception
                print("warning: could not restore rate: %s" % err, file=sys.stderr)


# ---------------------------------------------------------------------------
# Command line
# ---------------------------------------------------------------------------


def _main(argv: list[str]) -> int:
    import argparse

    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--set",
        nargs=2,
        metavar=("PA_INDEX", "RATE"),
        help="set the nominal rate of a PortAudio device index",
    )
    parser.add_argument(
        "--kind",
        choices=("input", "output"),
        default="output",
        help="which side of the device --set refers to (default: output)",
    )
    args = parser.parse_args(argv)

    if args.set:
        device = find_by_portaudio_index(int(args.set[0]), args.kind)
        rate = float(args.set[1])
        if not device.supports(rate):
            print(
                "error: %s does not support %g Hz (supports %s)"
                % (device.name, rate, device.available_rates),
                file=sys.stderr,
            )
            return 1
        print("%s: %g -> %g Hz" % (device.name, device.nominal_rate, rate))
        set_nominal_rate(device.device_id, rate)
        return 0

    for device in list_devices():
        print(device)
    return 0


if __name__ == "__main__":
    sys.exit(_main(sys.argv[1:]))
