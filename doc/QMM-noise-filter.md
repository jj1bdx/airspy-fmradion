# Thoughts on a noise filter for L-R signal

31-OCT-2019 Kenji Rikitake

## Definitions

* S = L-R signal (DSB-SC 38kHz pilot in-phase)
* Q = Quadrature Multipath Monitor (QMM) output of L-R signal (DSB-SC 38kHz pilot quadrature phase)

## QMM behavior

* Q must be zero for an ideal transmission
* Reality: Q is not zero, and represents multipath and other non-linear distortion of S (L-R signal)

## Audio and MPX filtering possibilities

* S2 = S - Q
* Strategy: minimize Q^2 (closest to 0)
* Is S2 a better (lower distortion) L-R signal? It might be. See [1].
* The noise component (Q) behaves differently from the signal (S) component. This suggests a Hilbert transformer is required for Q. [1]
* An audio-level adaptive filter to estimate S2 from S and Q will be practical [2].
* Applying the adaptive filter to the composite MPX output might even be possible, so that the monaural part of the MPX signal could also be multipath-filtered. The effectiveness is limited and unknown, though.

## More aggressive filtering: controlling IF filter by QMM output

* Strategy: minimize Q^2 (closest to 0)
* Filter: at IF
* Issue: delay between the IF filter output to the calculated Q value. Filtered-X LMS model could be applied.

### Thoughts on delay

* Conversion of 384kHz -> 3.84kHz with /5/5/4 3-stage FIR filters, each 31 taps, will take approx. (31/384 + 31/76.8 + 31/15.36) ~= 2.503msec = 961 samples of 384kHz sampling rate.
* Having 961 samples in an IF-stage FIR filter of 384kHz sample rate is *impractical* (practical maximum value: approx. 400 stages for Raspberry Pi 4)

### Thoughts on power estimation

* FFT of Q will give the power spectrum estimate much faster (Freq 1/n in a n-sample block of sampled signal)

## QMM monitoring test

* S and Q can be monitored together as a stereo output.

## References

[1]: S. Ito, M. Fujimoto, T. Hori, T. Harada and Y. Hattori, "Noise suppression system for AM using quadrature demodulation and Hilbert transformation," 2015 IEEE 4th Asia-Pacific Conference on Antennas and Propagation (APCAP), Kuta, 2015, pp. 333-334.  doi: 10.1109/APCAP.2015.7374394

[2]: K. Takano, T. Chinda, M. Taira, and K. Ogino, "New AM Noise Canceller", DENSO TEN Technical Review Vol. 2 (English Version), pp. 12-17, February 2019, https://www.denso-ten.com/business/technicaljournal/ 
