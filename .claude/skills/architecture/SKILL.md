---
name: architecture
description: Architecture of airspy-fmradion — signal flow from SDR source to audio output, key type aliases, the Source abstraction, and the FM decoder pipeline. Use when reading or modifying the DSP chain, sources, decoders, buffers, or audio output.
---

# Architecture

## Signal flow

```
SDR hardware / file
    ↓
Source subclass (AirspySource / AirspyHFSource / RtlSdrSource / FileSource)
    ↓  [separate thread, pushes IQSampleVector]
DataBuffer<IQSample>  ← thread-safe queue with condition variable
    ↓  [main thread pulls]
FourthConverterIQ     ← Fs/4 downconversion for zero-IF devices (Airspy HF+, RTL-SDR)
    ↓
IfResampler           ← r8brain-based rational resampler to decoder's target rate
    ↓
FmDecoder / AmDecoder / NbfmDecoder
    ↓  [SampleVector at 48 kHz]
AudioOutput (SndfileOutput or PortAudioOutput)
```

## Key type aliases (include/SoftFM.h)

| Alias | Underlying type | Use |
|---|---|---|
| `IQSample` | `std::complex<float>` | Raw IF samples |
| `IQSampleVector` | `std::vector<IQSample>` | Block of IF samples |
| `IQSampleDecoded` | `float` | Phase-discriminated baseband sample |
| `Sample` | `double` | Audio sample |
| `SampleVector` | `std::vector<double>` | Audio block |
| `IQSampleCoeff` | `std::vector<float>` | FIR filter coefficients for IQ path |

Enums also live in `SoftFM.h`: `FilterType`, `DevType`, `ModType`,
`OutputMode`, `PilotState`.

## Source abstraction

All SDR frontends implement the abstract `Source` interface
(`include/Source.h`):

- `configure(string)` — parse key=value config string and open device
- `start(DataBuffer<IQSample>*, atomic_bool*)` — begin streaming in a
  background thread
- `stop()` / `is_low_if()` / `get_sample_rate()` / `get_frequency()`

Low-IF sources (Airspy R2/Mini output real low-IF, not zero-IF) skip the
`FourthConverterIQ` step — controlled by `is_low_if()`.

## FM decoder pipeline (sfmbase/FmDecode.cpp)

Inside `FmDecoder::process()`:

1. Optional IQ bandpass filter (`LowPassFilterFirIQ`)
2. IF AGC (`IfSimpleAgc`)
3. Optional multipath filter (`MultipathFilter` — normalized LMS, enabled
   with `-E`)
4. Phase discriminator → baseband
5. Audio resampler (r8brain via `AudioResampler`)
6. Pilot PLL for stereo detection (`PilotPhaseLock`, also generates PPS
   events)
7. Stereo L−R demodulation, de-emphasis, DC blocking
