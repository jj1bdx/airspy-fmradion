#!/usr/bin/env python3
"""DAC->ADC analog loopback: sample-clock calibration and path delay.

Plays a 3 kHz tone with periodic marker chirps out of a DAC at 48 kHz,
records it back through an ADC at 44.1 kHz, and reports

  * each device's true sample rate against the host clock, from a linear
    fit of the PortAudio per-block DAC/ADC timestamps;
  * the DAC/ADC clock ratio, independently, from the phase slope of the
    received tone (the two agree to well under 1 ppm);
  * the analog path delay left over after PortAudio's own DAC-time and
    ADC-time accounting, from the marker chirps.

The path delay is the reason to reach for this script rather than
`clock_offset_measure.py`, which drops the chirps to keep the tone clean.
Note that this one does not set the devices' CoreAudio nominal rates, so
its ppm figures are only valid when the devices already sit at FS_OUT and
FS_IN; see OFFSET_MEASUREMENT_20260725.md section 10.3.

Devices are named by PortAudio index, as in `clock_offset_measure.py`.

Requires `sounddevice`, `soundfile`, `numpy`, `scipy`.

Usage:
    loopback_clock_cal.py --list-devices
    loopback_clock_cal.py --out-device 1 --in-device 2 [--seconds 60]
                          [--base cal] [--analyze-only]
"""
import argparse
import queue
import sys
import time
from pathlib import Path

import numpy as np
import soundfile as sf
from scipy.signal import fftconvolve

sys.path.insert(0, str(Path(__file__).resolve().parent))

FS_OUT = 48000
FS_IN = 44100
F_TONE = 3000.0
AMP = 0.1
CHIRP_LEN = 0.010
CHIRP_F0, CHIRP_F1 = 1000.0, 8000.0


def make_signal(dur):
    n = int(FS_OUT * dur)
    t = np.arange(n) / FS_OUT
    sig = AMP * np.sin(2 * np.pi * F_TONE * t)
    at = list(range(5, int(dur) - 2, 10))
    for s in at:
        sig[s * FS_OUT:s * FS_OUT + len(chirp())] += 0.45 * chirp()
    return sig, at


def chirp(fs=FS_OUT):
    n = int(CHIRP_LEN * fs)
    t = np.arange(n) / fs
    k = (CHIRP_F1 - CHIRP_F0) / (2 * t[-1])
    return np.hanning(n) * np.sin(2 * np.pi * (CHIRP_F0 * t + k * t * t))


def record(dur, base, outdev, indev):
    import sounddevice as sd
    sig, _ = make_signal(dur)
    play = np.repeat(sig[:, None], 2, axis=1).astype(np.float32)
    n = len(sig)
    q, in_ts, out_ts = queue.Queue(), [], []
    st = {'oi': 0, 'ii': 0, 'done': False}

    def ocb(outdata, frames, tinfo, status):
        i = st['oi']
        out_ts.append((i, tinfo.outputBufferDacTime))
        m = max(0, min(frames, n - i))
        outdata[:m] = play[i:i + m]
        if m < frames:
            outdata[m:] = 0
            st['done'] = True
        st['oi'] = i + frames

    def icb(indata, frames, tinfo, status):
        in_ts.append((st['ii'], tinfo.inputBufferAdcTime))
        st['ii'] += frames
        q.put(indata.copy())

    o = sd.OutputStream(device=outdev, samplerate=FS_OUT, channels=2,
                        dtype='float32', callback=ocb, latency='low')
    i = sd.InputStream(device=indev, samplerate=FS_IN, channels=2,
                       dtype='float32', callback=icb, latency='low')
    print('granted output latency %.6f s, input latency %.6f s'
          % (o.latency, i.latency))
    i.start()
    time.sleep(0.5)
    o.start()
    t0 = time.monotonic()
    while not st['done'] and time.monotonic() - t0 < dur + 5:
        time.sleep(0.2)
    time.sleep(0.7)
    # abort(), not stop(): stop() can deadlock when the callback has
    # already run past the end of the buffer.
    o.abort(ignore_errors=True)
    i.abort(ignore_errors=True)
    o.close(ignore_errors=True)
    i.close(ignore_errors=True)
    rec = np.concatenate([q.get() for _ in range(q.qsize())], axis=0)
    sf.write(base + '_rec.wav', rec, FS_IN, subtype='FLOAT')
    np.save(base + '_in_ts.npy', np.array(in_ts))
    np.save(base + '_out_ts.npy', np.array(out_ts))
    print('recorded %d frames (%.3f s), rms L %.4f R %.4f'
          % (len(rec), len(rec) / FS_IN, np.sqrt((rec[:, 0] ** 2).mean()),
             np.sqrt((rec[:, 1] ** 2).mean())))


def rate_fit(ts, nominal, label):
    f, tt = ts[5:-5, 0].astype(float), ts[5:-5, 1].astype(float)
    t0 = tt.mean()
    A = np.vstack([tt - t0, np.ones_like(tt)]).T
    (sl, ic) = np.linalg.lstsq(A, f, rcond=None)[0]
    res = f - A @ [sl, ic]
    print('%-24s %6d blocks  %11.4f Hz  (%+7.2f ppm)  resid %.3f frames rms'
          % (label, len(f), sl, (sl / nominal - 1) * 1e6,
             np.sqrt((res ** 2).mean())))
    return (lambda fr: t0 + (fr - ic) / sl), sl


def analyze(base, dur):
    rec, sr = sf.read(base + '_rec.wav', always_2d=True)
    x = rec[:, 1]
    its = np.load(base + '_in_ts.npy')
    ots = np.load(base + '_out_ts.npy')
    _, at = make_signal(dur)

    print('--- device rate vs host clock (PortAudio block timestamps) ---')
    in_time, fin = rate_fit(its, FS_IN, 'ADC @%d' % FS_IN)
    out_time, fout = rate_fit(ots, FS_OUT, 'DAC @%d' % FS_OUT)
    r = (fout / FS_OUT) / (fin / FS_IN)
    print('DAC/ADC ratio from timestamps: %+.3f ppm' % ((r - 1) * 1e6))

    print('\n--- DAC/ADC ratio from the received tone (phase slope) ---')
    t = np.arange(len(x)) / sr
    D = 441
    m = (len(x) // D) * D
    z = (x[:m] * np.exp(-2j * np.pi * F_TONE * t[:m])).reshape(-1, D).mean(1)
    tz = (np.arange(len(z)) * D + D / 2) / sr
    good = (np.abs(z) > 0.4 * np.median(np.abs(z)))
    good &= (tz > tz[0] + 0.5) & (tz < tz[-1] - 0.5)
    A = np.vstack([tz[good], np.ones(good.sum())]).T
    sl, ic = np.linalg.lstsq(A, np.unwrap(np.angle(z))[good], rcond=None)[0]
    df = sl / (2 * np.pi)
    resid = np.unwrap(np.angle(z))[good] - A @ [sl, ic]
    print('tone %.6f Hz (nominal %.0f): %+.3f ppm, phase resid %.4f rad rms'
          % (F_TONE + df, F_TONE, df / F_TONE * 1e6,
             np.sqrt((resid ** 2).mean())))

    print('\n--- analog path delay beyond PortAudio DAC/ADC timestamps ---')
    c2 = np.interp(np.arange(int(round(CHIRP_LEN * FS_IN))) * FS_OUT / FS_IN,
                   np.arange(len(chirp())), chirp())
    corr = np.abs(fftconvolve(x, c2[::-1], mode='valid'))
    idx = np.arange(len(corr)) / FS_IN
    ds = []
    for s in at:
        t_dac = out_time(s * FS_OUT)
        near = np.abs(idx - (t_dac - in_time(0))) < 0.4
        k = int(np.argmax(corr * near))
        y0, y1, y2 = corr[k - 1], corr[k], corr[k + 1]
        rf = k + 0.5 * (y0 - y2) / (y0 - 2 * y1 + y2)
        ds.append(in_time(rf) - t_dac)
        print('  chirp @%3ds: delta %8.3f ms' % (s, ds[-1] * 1e3))
    ds = np.array(ds)
    print('mean %.3f ms, std %.3f ms (n=%d)'
          % (ds.mean() * 1e3, ds.std() * 1e3, len(ds)))


def check_devices(outdev, indev):
    """Reject a device pair that cannot do the job, before recording."""
    import sounddevice as sd
    if outdev == indev:
        raise SystemExit('error: output and input must be different devices; '
                         'one device looping back to itself shares a single '
                         'clock and would read 0 ppm by construction')
    try:
        oi, ii = sd.query_devices(outdev), sd.query_devices(indev)
    except (ValueError, sd.PortAudioError) as e:
        raise SystemExit('error: %s (try --list-devices)' % e)
    if oi['max_output_channels'] < 2:
        raise SystemExit('error: PortAudio device %d (%s) has %d output '
                         'channels, need 2'
                         % (outdev, oi['name'], oi['max_output_channels']))
    if ii['max_input_channels'] < 2:
        raise SystemExit('error: PortAudio device %d (%s) has %d input '
                         'channels, need 2'
                         % (indev, ii['name'], ii['max_input_channels']))
    # This script never sets the nominal rate, so say so when the devices
    # are not already where FS_OUT/FS_IN expect them: CoreAudio would
    # resample silently and the ppm figures would describe the resampler.
    try:
        import coreaudio_rate as ca
        for idx, kind, want in ((outdev, 'output', FS_OUT),
                                (indev, 'input', FS_IN)):
            d = ca.find_by_portaudio_index(idx, kind)
            if abs(d.nominal_rate - want) > 0.5:
                print('warning: %s (%s) is at %g Hz, not %g Hz; CoreAudio will '
                      'resample and the ppm figures will describe the '
                      'resampler.\n         Fix with: coreaudio_rate.py --set '
                      '%d %d%s'
                      % (d.name, kind, d.nominal_rate, want, idx, want,
                         '' if kind == 'output' else ' --kind input'))
    except Exception as e:
        print('warning: could not check nominal sample rates (%s)' % e)
    print('output: PortAudio %d = %s' % (outdev, oi['name'].strip()))
    print('input : PortAudio %d = %s' % (indev, ii['name'].strip()))


if __name__ == '__main__':
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument('--list-devices', action='store_true',
                    help='list PortAudio devices with their hardware rates')
    ap.add_argument('--out-device', type=int,
                    help='PortAudio index of the device to play from')
    ap.add_argument('--in-device', type=int,
                    help='PortAudio index of the device to record on')
    ap.add_argument('--seconds', type=float, default=60.0)
    ap.add_argument('--base', default='cal')
    ap.add_argument('--analyze-only', action='store_true')
    a = ap.parse_args()
    if a.list_devices:
        import clock_offset_measure
        clock_offset_measure.list_devices()
        raise SystemExit(0)
    if not a.analyze_only:
        if a.out_device is None or a.in_device is None:
            ap.error('--out-device and --in-device are required '
                     '(see --list-devices)')
        check_devices(a.out_device, a.in_device)
        record(a.seconds, a.base, a.out_device, a.in_device)
    analyze(a.base, a.seconds)
