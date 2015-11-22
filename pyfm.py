"""
Test lab for FM decoding algorithms.

Use as follows:

    >>> graw  = pyfm.lazyRawSamples('rtlsdr.dat', 1000000)
    >>> gtune = pyfm.freqShiftIQ(graw, 0.25)
    >>> bfir  = scipy.signal.firwin(20, 0.2, window='nuttall')
    >>> gfilt = pyfm.firFilter(gtune, bfir)
    >>> gbase = pyfm.quadratureDetector(gfilt, fs=1.0e6)
    >>> fs,qs = pyfm.spectrum(gbase, fs=1.0e6)
"""

import sys
import datetime
import types
import numpy
import numpy.fft
import numpy.linalg
import numpy.random
import scipy.signal


def sincw(n):
    """Return Sinc or Lanczos window of length n."""

    w = numpy.zeros(n)
    for i in xrange(n):
        if 2 * i == n + 1:
            w[i] = 1.0
        else:
            t = 2 * i / float(n+1) - 1
            w[i] = numpy.sin(numpy.pi * t) / (numpy.pi * t)

    return w


def readRawSamples(fname):
    """Read raw sample file from rtl_sdr."""

    d = numpy.fromfile(fname, dtype=numpy.uint8)
    d = d.astype(numpy.float64)
    d = (d - 128) / 128.0

    return d[::2] + 1j * d[1::2]


def lazyRawSamples(fname, blocklen):
    """Return generator over blocks of raw samples."""

    f = file(fname, 'rb')
    while 1:
        d = f.read(2 * blocklen)
        if len(d) < 2 * blocklen:
            break
        d = numpy.fromstring(d, dtype=numpy.uint8)
        d = d.astype(numpy.float64)
        d = (d - 128) / 128.0
        yield d[::2] + 1j * d[1::2]


def freqShiftIQ(d, freqshift):
    """Shift frequency by multiplication with complex phasor."""

    def g(d, freqshift):
        p = 0
        for b in d:
            n = len(b)
            w = numpy.exp((numpy.arange(n) + p) * (2j * numpy.pi * freqshift))
            p += n
            yield b * w

    if isinstance(d, types.GeneratorType):
        return g(d, freqshift)
    else:
        n = len(d)
        w = numpy.exp(numpy.arange(n) * (2j * numpy.pi * freqshift))
        return d * w


def firFilter(d, coeff):
    """Apply FIR filter to sample stream."""

    # lazy version
    def g(d, coeff):
        prev = None
        for b in d:
            if prev is None:
                yield scipy.signal.lfilter(coeff, 1, b)
                prev = b
            else:
                k = min(len(prev), len(coeff))
                x = numpy.concatenate((prev[-k:], b))
                y = scipy.signal.lfilter(coeff, 1, x)
                yield y[k:]
                if len(coeff) > len(b):
                    prev = x
                else:
                    prev = b

    if isinstance(d, types.GeneratorType):
        return g(d, coeff)
    else:
        return scipy.signal.lfilter(coeff, 1, d)


def quadratureDetector(d, fs):
    """FM frequency detector based on quadrature demodulation.
    Return an array of real-valued numbers, representing frequencies in Hz."""

    k = fs / (2 * numpy.pi)

    # lazy version
    def g(d):
        prev = None
        for b in d:
            if prev is not None:
                x = numpy.concatenate((prev[1:], b[:1]))
                yield numpy.angle(x * prev.conj()) * k
            prev = b
        yield numpy.angle(prev[1:] * prev[:-1].conj()) * k

    if isinstance(d, types.GeneratorType):
        return g(d)
    else:
        return numpy.angle(d[1:] * d[:-1].conj()) * k


def modulateFm(sig, fs, fcenter=0):
    """Create an FM modulated IQ signal.

    sig     :: modulation signal, values in Hz
    fs      :: sample rate in Hz
    fcenter :: center frequency in Hz
    """

    return numpy.exp(2j * numpy.pi * (sig + fcenter).cumsum() / fs)


def spectrum(d, fs=1, nfft=None, sortfreq=False):
    """Calculate Welch-style power spectral density.

    fs       :: sample rate, default to 1
    nfft     :: FFT length, default to block length
    sortfreq :: True to put negative freqs in front of positive freqs

    Use Hann window with 50% overlap.

    Return (freq, Pxx)."""

    if not isinstance(d, types.GeneratorType):
        d = [ d ]

    prev = None

    if nfft is not None:
        assert nfft > 0
        w = numpy.hanning(nfft)
        q = numpy.zeros(nfft)

    pos = 0
    i = 0
    for b in d:

        if nfft is None:
            nfft = len(b)
            assert nfft > 0
            w = numpy.hanning(nfft)
            q = numpy.zeros(nfft)

        while pos + nfft <= len(b):

            if pos < 0:
                t = numpy.concatenate((prev[pos:], b[:pos+nfft]))
            else:
                t = b[pos:pos+nfft]

            t *= w
            tq = numpy.fft.fft(t)
            tq *= numpy.conj(tq)
            q += numpy.real(tq)

            del t
            del tq

            pos += (nfft+(i%2)) // 2
            i += 1

        pos -= len(b)
        if pos + len(b) > 0:
            prev = b
        else:
            prev = numpy.concatenate((prev[pos+len(b):], b))

    if i > 0:
        q /= (i * numpy.sum(numpy.square(w)) * fs)

    f = numpy.arange(nfft) * (fs / float(nfft))
    f[nfft//2:] -= fs

    if sortfreq:
        f = numpy.concatenate((f[nfft//2:], f[:nfft//2]))
        q = numpy.concatenate((q[nfft//2:], q[:nfft//2]))

    return f, q


def pll(d, centerfreq, bandwidth):
    """Simulate the stereo pilot PLL."""

    minfreq = (centerfreq - bandwidth) * 2 * numpy.pi
    maxfreq = (centerfreq + bandwidth) * 2 * numpy.pi

    w = bandwidth * 2 * numpy.pi
    phasor_a = numpy.poly([ numpy.exp(-1.146*w), numpy.exp(-5.331*w) ])
    phasor_b = numpy.array([ sum(phasor_a) ])

    loopfilter_b = numpy.poly([ numpy.exp(-0.1153*w) ])
    loopfilter_b *= 0.62 * w

    n = len(d)
    y = numpy.zeros(n)
    phasei = numpy.zeros(n)
    phaseq = numpy.zeros(n)
    phaseerr = numpy.zeros(n)
    freq = numpy.zeros(n)
    phase = numpy.zeros(n)
    freq[0] = centerfreq * 2 * numpy.pi

    phasor_i1 = phasor_i2 = 0
    phasor_q1 = phasor_q2 = 0
    loopfilter_x1 = 0

    for i in xrange(n):

        psin = numpy.sin(phase[i])
        pcos = numpy.cos(phase[i])
        y[i] = pcos

        pi = pcos * d[i]
        pq = psin * d[i]

        pi = phasor_b[0] * pi - phasor_a[1] * phasor_i1 - phasor_a[2] * phasor_i2
        pq = phasor_b[0] * pq - phasor_a[1] * phasor_q1 - phasor_a[2] * phasor_q2
        phasor_i2 = phasor_i1
        phasor_i1 = pi
        phasor_q2 = phasor_q1
        phasor_q1 = pq

        phasei[i] = pi
        phaseq[i] = pq

        if pi > abs(pq):
            perr = pq / pi
        elif pq > 0:
            perr = 1
        else:
            perr = -1
        phaseerr[i] = perr

        dfreq = loopfilter_b[0] * perr + loopfilter_b[1] * loopfilter_x1
        loopfilter_x1 = perr

        if i + 1 < n:
            freq[i+1] = min(maxfreq, max(minfreq, freq[i] - dfreq))
            p = phase[i] + freq[i+1]
            if p > 2 * numpy.pi:  p -= 2 * numpy.pi
            if p < -2 * numpy.pi: p += 2 * numpy.pi
            phase[i+1] = p

    return y, phasei, phaseq, phaseerr, freq, phase


def pilotLevel(d, fs, freqshift, nfft=None, bw=150.0e3):
    """Calculate level of the 19 kHz pilot vs noise floor in the guard band.

    d         :: block of raw I/Q samples or lazy I/Q sample stream
    fs        :: sample frequency in Hz
    nfft      :: FFT length
    freqshift :: frequency offset in Hz
    bw        :: half-bandwidth of IF signal in Hz

    Return (pilot_power, guard_floor, noise)
    where pilot_power is the power of the pilot tone in dB
          guard_floor is the noise floor in the guard band in dB/Hz
          noise       is guard_floor - pilot_power in dB/Hz
    """

    # Shift frequency
    if freqshift != 0:
        d = freqShiftIQ(d, freqshift / float(fs))

    # Filter
    b = scipy.signal.firwin(31, 2.0 * bw / fs, window='nuttall')
    d = firFilter(d, b)

    # Demodulate FM.
    d = quadratureDetector(d, fs)

    # Power spectral density.
    f, q = spectrum(d, fs=fs, nfft=nfft, sortfreq=False)

    # Locate 19 kHz bin.
    k19 = int(19.0e3 * len(q) / fs)
    kw  = 5 + int(100.0 * len(q) / fs)
    k19 = k19 - kw + numpy.argmax(q[k19-kw:k19+kw])

    # Calculate pilot power.
    p19 = numpy.sum(q[k19-1:k19+2]) * fs * 1.5 / len(q)

    # Calculate noise floor in guard band.
    k17 = int(17.0e3 * len(q) / fs)
    k18 = int(18.0e3 * len(q) / fs)
    guard = numpy.mean(q[k17:k18])

    p19db   = 10 * numpy.log10(p19)
    guarddb = 10 * numpy.log10(guard)

    return (p19db, guarddb, guarddb - p19db)


def modulateAndReconstruct(sigfreq, sigampl, nsampl, fs, noisebw=None, ifbw=None, ifnoise=0, ifdownsamp=1):
    """Create a pure sine wave, modulate to FM, add noise, filter, demodulate.

    sigfreq     :: frequency of sine wave in Hz
    sigampl     :: amplitude of sine wave in Hz (carrier swing)
    nsampl      :: number of samples
    fs          :: sample rate in Hz
    noisebw     :: calculate noise after demodulation over this bandwidth
    ifbw        :: IF filter bandwidth in Hz, or None for no filtering
    ifnoise     :: IF noise level
    ifdownsamp  :: downsample factor before demodulation

    Return (ampl, phase, noise)
    where ampl  is the amplitude of the reconstructed sine wave (~ sigampl)
          phase is the phase shift after reconstruction
          noise is the standard deviation of noise in the reconstructed signal
    """

    # Make sine wave.
    sig0  = sigampl * numpy.sin(2*numpy.pi*sigfreq/fs * numpy.arange(nsampl))

    # Modulate to IF.
    fm = modulateFm(sig0, fs=fs, fcenter=0)

    # Add noise.
    if ifnoise:
        fm +=      numpy.sqrt(0.5) * numpy.random.normal(0, ifnoise, nsampl)
        fm += 1j * numpy.sqrt(0.5) * numpy.random.normal(0, ifnoise, nsampl)

    # Filter IF.
    if ifbw is not None:
        b  = scipy.signal.firwin(101, 2.0 * ifbw / fs, window='nuttall')
        fm = scipy.signal.lfilter(b, 1, fm)
        fm = fm[61:]

    # Downsample IF.
    fs1 = fs
    if ifdownsamp != 1:
        fm = fm[::ifdownsamp]
        fs1 = fs / ifdownsamp

    # Demodulate.
    sig1 = quadratureDetector(fm, fs=fs1)

    # Fit original sine wave.
    k = len(sig1)
    m = numpy.zeros((k, 3))
    m[:,0] = numpy.sin(2*numpy.pi*sigfreq/fs1 * (numpy.arange(k) + nsampl - k))
    m[:,1] = numpy.cos(2*numpy.pi*sigfreq/fs1 * (numpy.arange(k) + nsampl - k))
    m[:,2] = 1
    fit = numpy.linalg.lstsq(m, sig1)
    csin, ccos, coffset = fit[0]
    del fit

    # Calculate amplitude, phase.
    ampl1  = numpy.sqrt(csin**2 + ccos**2)
    phase1 = numpy.arctan2(-ccos, csin)

    # Calculate residual noise.
    res1   = sig1 - m[:,0] * csin - m[:,1] * ccos

    if noisebw is not None:
        b  = scipy.signal.firwin(101, 2.0 * noisebw / fs1, window='nuttall')
        res1 = scipy.signal.lfilter(b, 1, res1)

    noise1 = numpy.sqrt(numpy.mean(res1 ** 2))

    return ampl1, phase1, noise1


def rdsDemodulate(d, fs):
    """Demodulate RDS bit stream.

    d       :: block of baseband samples or lazy baseband sample stream
    fs      :: sample frequency in Hz

    Return (bits, levels)
    where bits is a list of RDS data bits
          levels is a list of squared RDS carrier amplitudes
    """

    # RDS carrier in Hz
    carrier = 57000.0

    # RDS bit rate in bit/s
    bitrate = 1187.5

    # Approximate nr of samples per bit.
    bitsteps = round(fs / bitrate)

    # Prepare FIR coefficients for matched filter.
    #
    # The filter is a root-raised-cosine with hard cutoff at f = 2/bitrate.
    #   H(f) = cos(pi * f / (4*bitrate))   if f <  2*bitrate
    #   H(f) = 0                           if f >= 2*bitrate
    #
    # Impulse response:
    #   h(t) = ampl * cos(pi*4*bitrate*t) / (1 - 4 * (4*bitrate*t)**2)
    #
    wlen = int(1.5 * fs / bitrate)
    w    = numpy.zeros(wlen)
    for i in xrange(wlen):
        t = (i - 0.5 * (wlen - 1)) * 4.0 * bitrate / fs
        if abs(abs(t) - 0.5) < 1.0e-4:
            # lim {t->0.5} {cos(pi*t) / (1 - 4*t**2)} = 0.25 * pi
            w[i] = 0.25 * numpy.pi - 0.25 * numpy.pi * (abs(t) - 0.5)
        else:
            w[i] = numpy.cos(numpy.pi * t) / (1 - 4.0 * t * t)

    # Use Sinc window to reduce leakage.
    w *= sincw(wlen)

    # Scale filter such that peak output of filter equals original amplitude.
    w /= numpy.sum(w**2)

    demod_phase = 0.0
    prev_a1 = 0.0

    prevb = numpy.array([])
    pos   = 0

    bits = [ ]
    levels = [ ]

    if not isinstance(d, types.GeneratorType):
        d = [ d ]

    for b in d:

        n = len(b)

        # I/Q demodulate with fixed 57 kHz phasor
        ps  = numpy.arange(n) * (carrier / float(fs)) + demod_phase
        dem = b * numpy.exp(-2j * numpy.pi * ps)
        demod_phase = (demod_phase + n * carrier / float(fs)) % 1.0

        # Merge with remaining data from previous block
        prevb = numpy.concatenate((prevb[pos:], dem))
        pos   = 0

        # Detect bits.
        while pos + bitsteps + wlen < len(prevb):

            # Measure average phase of first impulse of symbol.
            a1 = numpy.sum(prevb[pos:pos+wlen] * w)

            # Measure average phase of second impulse of symbol.
            a2 = numpy.sum(prevb[pos+bitsteps//2:pos+wlen+bitsteps//2] * w)

            # Measure average phase in middle of symbol.
            a3 = numpy.sum(prevb[pos+bitsteps//4:pos+wlen+bitsteps//4] * w)

            # Calculate inner product of first impulse and previous symbol.
            sym = a1.real * prev_a1.real + a1.imag * prev_a1.imag
            prev_a1 = a1

            if sym < 0:
                # Consecutive symbols have opposite phase; this is a 1 bit.
                bits.append(1)
            else:
                # Consecutive symbols are in phase; this is a 0 bit.
                bits.append(0)

            # Calculate inner product of first and second impulse.
            a1a2 = a1.real * a2.real + a1.imag * a2.imag

            # Calculate inner product of first impulse and middle phasor.
            a1a3 = a1.real * a3.real + a1.imag * a3.imag

            levels.append(-a1a2)

            if a1a2 >= 0:
                # First and second impulse are in phase;
                # we must be woefully misaligned.
                pos += 5 * bitsteps // 8
            elif a1a3 > -0.02 * a1a2:
                # Middle phasor is in phase with first impulse;
                # we are sampling slightly too early.
                pos += (102 * bitsteps) // 100
            elif a1a3 > -0.01 * a1a2:
                pos += (101 * bitsteps) // 100
            elif a1a3 < 0.02 * a1a2:
                # Middle phasor is opposite to first impulse;
                # we are sampling slightly too late.
                pos += (98 * bitsteps) // 100
            elif a1a3 < 0.01 * a1a2:
                pos += (99 * bitsteps) // 100
            else:
                # Middle phasor is zero; we are sampling just right.
                pos += bitsteps

    return (bits, levels)


def rdsDecodeBlock(bits, typ):
    """Decode one RDS data block.

    bits    :: list of 26 bits
    typ     :: expected block type, "A" or "B" or "C" or "C'" or "D" or "E"

    Return (block, ber)
    where block is a 16-bit unsigned integer if the block is correctly decoded,
          block is None if decoding failed,
          ber is 0 if the block is error-free,
          ber is 1 if a single-bit error has been corrected,
          ber is 2 if decoding failed.
    """

# TODO : there are still problems with bit alignment on weak stations
# TODO : try to pin down the problem

    # Offset word for each type of block.
    rdsOffsetTable = { "A": 0x0fc, "B": 0x198,
                       "C": 0x168, "C'": 0x350,
                       "D": 0x1B4, "E": 0 }

    # RDS checkword generator polynomial.
    gpoly = 0x5B9

    # Convert bits to word.
    assert len(bits) == 26
    w = 0
    for b in bits:
        w = 2 * w + b

    # Remove block offset.
    w ^= rdsOffsetTable[typ]

    # Calculate error syndrome.
    syn = w
    for i in xrange(16):
        if syn & (1 << (25 - i)):
            syn ^= gpoly << (15 - i)

    # Check block.
    if syn == 0:
        return (w >> 10, 0)

    # Error detected; try all single-bit errors.
    p = 1
    for k in xrange(26):
        if p == syn:
            # Detected single-bit error in bit k.
            w ^= (1 << k)
            return (w >> 10, 1)
        p <<= 1
        if p & 0x400:
            p ^= gpoly

    # No single-bit error can explain this syndrome.
    return (None, 2)


class RdsData(object):
    """Stucture to hold common RDS data fields."""

    pi  = None
    pty = None
    tp  = None
    ta  = None
    ms  = None
    af  = None
    di  = None
    pin = None
    pserv   = None
    ptyn    = None
    ptynab  = None
    rtext   = None
    rtextab = None
    time    = None

    tmp_afs = None
    tmp_aflen = 0
    tmp_afmode = 0

    ptyTable = [
        'None',             'News',             
        'Current Affairs',  'Information',
        'Sport',            'Education',        
        'Drama',            'Cultures',
        'Science',          'Varied Speech',    
        'Pop Music',        'Rock Music',
        'Easy Listening',   'Light Classics M', 
        'Serious Classics', 'Other Music',
        'Weather & Metr',   'Finance',          
        "Children's Progs", 'Social Affairs',
        'Religion',         'Phone In',         
        'Travel & Touring', 'Leisure & Hobby',
        'Jazz Music',       'Country Music',    
        'National Music',   'Oldies Music',
        'Folk Music',       'Documentary',      
        'Alarm Test',       'Alarm - Alarm !' ]

    def __str__(self):

        if self.pi is None:
            return str(None)

        s = 'RDS PI=%-5d' % self.pi

        s += ' TP=%d' % self.tp

        if self.ta is not None:
            s += ' TA=%d' % self.ta
        else:
            s += '     '

        if self.ms is not None:
            s += ' MS=%d' % self.ms
        else:
            s += '     '

        s += ' PTY=%-2d %-20s' % (self.pty, '(' + self.ptyTable[self.pty] + ')')

        if self.ptyn is not None:
            s += ' PTYN=%r' + str(self.ptyn).strip('\x00')

        if self.di is not None or self.pserv is not None:
            s += '\n   '
            if self.di is not None:
                distr = '('
                distr += 'stereo' if self.di & 1 else 'mono'
                if self.di & 2:
                    distr += ',artificial'
                if self.di & 4:
                    distr += ',compressed'
                if self.di & 8:
                    distr += ',dynpty'
                distr += ')'
                s += ' DI=%-2d %-37s' % (self.di, distr)
            else:
                s += 45 * ' '
            if self.pserv is not None:
                s += ' SERV=%r' % str(self.pserv).strip('\x00')

        if self.time is not None or self.pin is not None:
            s += '\n   '
            if self.time is not None:
                (day, hour, mt, off) = self.time
                dt = datetime.date.fromordinal(day + datetime.date(1858, 11, 17).toordinal())
                s += ' TIME=%04d-%02d-%02d %02d:%02d UTC ' % (dt.year, dt.month, dt.day, hour, mt)
            else:
                s += 27 * ' '
            if self.pin is not None:
                (day, hour, mt) = self.pin
                s += ' PIN=d%02d %02d:%02d' % (day, hour, mt)
            else:
                s += 14 * ' '

        if self.af is not None:
            s += '\n    AF='
            for f in self.af:
                if f > 1.0e6:
                    s += '%.1fMHz ' % (f * 1.0e-6)
                else:
                    s += '%.0fkHz ' % (f * 1.0e-3)

        if self.rtext is not None:
            s += '\n    RT=%r' % str(self.rtext).strip('\x00')

        return s


def rdsDecode(bits, rdsdata=None):
    """Decode RDS data stream.

    bits    :: list of RDS data bits
    rdsdata :: optional RdsData object to store RDS information

    Return (rdsdata, ngroups, errsoft, errhard)
    where rdsdata is the updated RdsData object
          ngroup  is the number of correctly decoded RDS groups
          errsoft is the number of correctable bit errors
          errhard is the number of uncorrectable bit errors
    """

    if rdsdata is None:
        rdsdata = RdsData()

    ngroup = 0
    errsoft = 0
    errhard = 0

    p = 0
    n = len(bits)
    while p + 4 * 26 <= n:

        (wa, ea) = rdsDecodeBlock(bits[p:p+26], "A")
        if wa is None:
            errhard += 1
            p += 1
            continue

        (wb, eb) = rdsDecodeBlock(bits[p+26:p+2*26], "B")
        if wb is None:
            errhard += 1
            p += 1
            continue

        if (wb >> 11) & 1:
            (wc, ec) = rdsDecodeBlock(bits[p+2*26:p+3*26], "C'")
        else:
            (wc, ec) = rdsDecodeBlock(bits[p+2*26:p+3*26], "C")
        if wc is None:
            errhard += 1
            p += 1
            continue

        (wd, ed) = rdsDecodeBlock(bits[p+3*26:p+4*26], "D")
        if wd is None:
            errhard += 1
            p += 1
            continue

        errsoft += ea + eb + ec + ed
        ngroup += 1

        # Found an RDS group; decode it.
        typ  = (wb >> 12)
        typb = (wb >> 11) & 1

        # PI, TP, PTY are present in all groups
        rdsdata.pi  = wa
        rdsdata.tp  = (wb >> 10) & 1
        rdsdata.pty = (wb >> 5) & 0x1f

        if typ == 0:
            # group type 0: TA, MS, DI, program service name 
            rdsdata.ta = (wb >> 4) & 1
            rdsdata.ms = (wb >> 3) & 1
            dseg = wb & 3
            if rdsdata.di is None:
                rdsdata.di = 0
            rdsdata.di &= ~(1 << dseg)
            rdsdata.di |= (((wb >> 2) & 1) << dseg)
            if rdsdata.pserv is None:
                rdsdata.pserv = bytearray(8)
            rdsdata.pserv[2*dseg]   = wd >> 8
            rdsdata.pserv[2*dseg+1] = wd & 0xff

        if typ == 0 and not typb:
            # group type 0A: alternate frequencies
            for f in ((wc >> 8), wc & 0xff):
                if f >= 224 and f <= 249:
                    rdsdata.tmp_aflen = f - 224
                    rdsdata.tmp_aflfmode = 0
                    rdsdata.tmp_afs = [ ]
                elif f == 250 and rdsdata.tmp_aflen > 0 and len(rdsdata.tmp_afs) < rdsdata.tmp_aflen:
                    rdsdata.tmp_aflfmode = 1
                elif f >= 1 and f <= 204 and rdsdata.tmp_aflen > 0 and len(rdsdata.tmp_afs) < rdsdata.tmp_aflen:
                    if rdsdata.tmp_aflfmode:
                        rdsdata.tmp_afs.append(144.0e3 + f * 9.0e3)
                    else:
                        rdsdata.tmp_afs.append(87.5e6 + f * 0.1e6)
                    if len(rdsdata.tmp_afs) == rdsdata.tmp_aflen:
                        rdsdata.af = rdsdata.tmp_afs
                        rdsdata.tmp_aflen = 0
                        rdsdata.tmp_afs = [ ]
                    rdsdata.tmp_aflfmode = 0

        if typ == 1:
            # group type 1: program item number
            rdsdata.pin = (wd >> 11, (wd >> 6) & 0x1f, wd & 0x3f)

        if typ == 2:
            # group type 2: radio text
            dseg = wb & 0xf
            if rdsdata.rtext is None or ((wb >> 4) & 1) != rdsdata.rtextab:
                rdsdata.rtext = bytearray(64)
            rdsdata.rtextab = (wb >> 4) & 1
            if typb:
                rdsdata.rtext[2*dseg]   = (wd >> 8)
                rdsdata.rtext[2*dseg+1] = wd & 0xff
            else:
                rdsdata.rtext[4*dseg]   = (wc >> 8)
                rdsdata.rtext[4*dseg+1] = wc & 0xff
                rdsdata.rtext[4*dseg+2] = (wd >> 8)
                rdsdata.rtext[4*dseg+3] = wd & 0xff

        if typ == 4 and not typb:
            # group type 4A: clock-time and date
            rdsdata.time = (((wb & 3) << 15) | (wc >> 1),
                            ((wc & 1) << 4) | (wd >> 12),
                            (wd >> 6) & 0x3f, (wd & 0x1f) - (wd & 0x20))

        if typ == 10 and not typb:
            # group type 10A: program type name
            dseg = wb & 1
            if rdsdata.ptyn is None or ((wb >> 4) & 1) != rdsdata.ptynab:
                rdsdata.ptyn = bytearray(8)
            rdsdata.ptynab = (wb >> 4) & 1
            rdsdata.ptyn[4*dseg]    = (wc >> 8)
            rdsdata.ptyn[4*dsseg+1] = wc & 0xff
            rdsdata.ptyn[4*dseg+2]  = (wd >> 8)
            rdsdata.ptyn[4*dsseg+3] = wd & 0xff
        
        # Go to next group.
        p += 4 * 26

    return (rdsdata, ngroup, errsoft, errhard)

