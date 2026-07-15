# The -L/--portaudio-latency option: branch summary (20260715)

This document summarizes the changes between branch
`dev-resampler-lowlatency` (base) and branch
`dev-portaudio-latency-option` (head), analyzed with Claude Code
(c-expert agent).

## Overview

The branch adds a new command line option `-L` (long form
`--portaudio-latency`) that lets the user set PortAudio's
`suggestedLatency` in milliseconds, and removes the macOS-specific
default-latency path so that all platforms share one default:
`defaultHighOutputLatency` floored at `minimum_latency_default`
(0.04 s = 40 ms).

## Commits

In order (oldest first):

* `1196d95` Add -L/--portaudio-latency option: the core feature.
  Adds the option to `usage()`, `optstring`, and `longopts`; parses
  and range-checks the value; converts ms to seconds; passes it as a
  new 4th `PortAudioOutput` constructor parameter. Also removed the
  unused `pa_mac_core.h` include (leftover from commit cde04d0).
* `a4cf5ca` PortAudioOutput: remove macOS-specific code: removes the
  `#ifdef __APPLE__` branch (`defaultLowOutputLatency` +
  `minimum_latency_low` floor), unifying all platforms onto the
  `defaultHighOutputLatency` + `minimum_latency_default` path.
* `d3683db` AudioOutput.h: remove macOS-specific comment: removes the
  `minimum_latency_low` constant and its macOS comment.
* `8b78495` README.md: add -L option description.
* `c749a05` CHANGES.md: add dev-portaudio-latency-option 20260715
  entry.
* `cc3489f` Reword -L default latency description: replaces
  "default: platform-dependent" with "default: floored at
  40 milliseconds" in `usage()`, README.md, and CHANGES.md.

Note: `minimum_latency_low` was modified (0.025 to 0.005) in
`1196d95` and then deleted entirely in `a4cf5ca`/`d3683db`, so the
constant does not survive this branch. The "no option -> 0.005000"
verification figure in the `1196d95` commit message reflects that
intermediate state; the final no-option default is 0.04 s.

## Interface changes

`PortAudioOutput` constructor gains a defaulted trailing parameter
(strictly additive; existing 3-argument call sites remain valid):

```cpp
// old
PortAudioOutput(const PaDeviceIndex device_index, unsigned int samplerate,
                bool stereo);

// new
PortAudioOutput(const PaDeviceIndex device_index, unsigned int samplerate,
                bool stereo, PaTime suggested_latency_sec = -1.0);
```

`suggested_latency_sec` is in seconds (`PaTime`); a negative value
(the default) means "no user override".

Removed: `static constexpr PaTime minimum_latency_low` and the
`pa_mac_core.h` include in `sfmbase/AudioOutput.cpp`.

## Behavioral changes

### Latency selection in PortAudioOutput

Old logic (base branch):

* macOS: `defaultLowOutputLatency`, floored at
  `minimum_latency_low` (0.025 s).
* Other platforms: `defaultHighOutputLatency`, floored at
  `minimum_latency_default` (0.04 s).

New logic (head branch):

```cpp
if (suggested_latency_sec >= 0.0) {
  // User-specified latency (-L / --portaudio-latency): use it verbatim.
  m_outputparams.suggestedLatency = suggested_latency_sec;
} else {
  m_outputparams.suggestedLatency =
      Pa_GetDeviceInfo(m_outputparams.device)->defaultHighOutputLatency;
  m_outputparams.hostApiSpecificStreamInfo = NULL;
  if (m_outputparams.suggestedLatency < minimum_latency_default) {
    m_outputparams.suggestedLatency = minimum_latency_default;
  }
}
```

Two independent shifts:

* User override: a non-negative `suggested_latency_sec` is used
  verbatim, intentionally bypassing the `minimum_latency_default`
  floor. The [1, 40] ms bound is enforced in `main.cpp` before
  conversion, not in the class.
* Platform unification: without `-L`, macOS now takes the same
  `defaultHighOutputLatency` + 40 ms floor path as all other
  platforms (the macOS `defaultLowOutputLatency` tuning introduced in
  `dev-resampler-lowlatency` is removed; `-L` is now the way to
  request lower latency).

### Option parsing in main.cpp

```cpp
case 'L':
  if (!Utility::parse_dbl(optarg, portaudio_latency_ms) ||
      portaudio_latency_ms < 1.0 || portaudio_latency_ms > 40.0) {
    badarg("-L");
  }
  break;
```

* `Utility::parse_dbl` rejects unparsable strings, trailing garbage,
  NaN/Inf, and out-of-range `strtod` results; k/M/G suffixes are
  parsed but any result outside [1, 40] fails the range check.
* On failure, `badarg("-L")` prints `usage()` and
  `ERROR: Invalid argument for -L`, then exits with status 1, before
  any device is opened and regardless of `-P`.
* The sentinel `portaudio_latency_ms = -1.0` ("unset") and the valid
  range [1, 40] are disjoint by construction, so the ms-to-seconds
  conversion needs no extra guard:

```cpp
PaTime portaudio_suggested_latency =
    (portaudio_latency_ms >= 0.0) ? (portaudio_latency_ms / 1000.0) : -1.0;
```

## User-visible behavior

* `usage()` (between `-P` and `-T`; README.md carries the same
  wording):

```
  -L ms          Set PortAudio output suggested latency in milliseconds
                 valid range: 1 to 40
                 (default: floored at 40 milliseconds)
                 (-L is ignored unless PortAudio output (-P) is used)
```

* `-L` without `-P`: parsed and range-checked, then silently unused —
  only the `OutputMode::PORTAUDIO` case reads the value. This matches
  the existing precedent of options accepted regardless of
  applicability (e.g., `-X` under `-M`); no runtime diagnostic is
  printed.
* Out-of-range or unparsable `-L`: usage text plus
  `ERROR: Invalid argument for -L`, exit status 1.
* Without `-L`: all platforms get `defaultHighOutputLatency` floored
  at 40 ms.
* The constructor prints the chosen value as
  `suggestedLatency = ...` on stderr in both paths.

## Correctness observations

* `suggestedLatency` is a hint: PortAudio host APIs round it to a
  supported buffer size, so a low request (e.g., `-L 1`) does not
  guarantee 1 ms of real latency. On CoreAudio with the USB DAC
  FiiO K7, granted latency was observed at roughly 5x the requested
  value.
* No floor exists inside `PortAudioOutput` for user-supplied values;
  the [1, 40] ms guarantee holds only for values arriving through the
  CLI gate in `main.cpp`. Any future non-CLI call site must apply its
  own range check.
* `hostApiSpecificStreamInfo` is NULL in both branches: explicitly
  assigned in the default path, and zero-initialized by the
  `PaStreamParameters m_outputparams{};` member declaration in the
  `-L` path. The asymmetry is harmless.
* Minor leftover in `include/AudioOutput.h`: the comment line
  "For lower latencies," above `minimum_latency_default` used to
  introduce the now-deleted `minimum_latency_low` sentence and now
  trails off; it could be rewritten to point at `-L` instead.
