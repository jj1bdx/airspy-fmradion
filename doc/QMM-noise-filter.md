# A noise filter for L-R signal

19-OCT-2019 Kenji Rikitake

## Definitions

* S = L-R signal (DSB-SC 38kHz pilot in-phase)
* Q = Quadrature Multipath Monitor (QMM) output of L-R signal (DSB-SC 38kHz pilot quadrature phase)

## QMM behavior

* Q must be zero for an ideal transmission
* Reality: Q is not zero, and represents multipath and other non-linear distortion of S (L-R signal)

## Filtering possibility

* S2 = S - Q
* Is S2 a better (lower distortion) L-R signal? It might be [1].
* Note well: the noise component (Q) behaves differently from the signal (S) component [1]. This suggests a Hilbert transformer is required for Q.

## QMM monitoring test

* S and Q can be monitored together as a stereo output.

## References

[1]: S. Ito, M. Fujimoto, T. Hori, T. Harada and Y. Hattori, "Noise suppression system for AM using quadrature demodulation and Hilbert transformation," 2015 IEEE 4th Asia-Pacific Conference on Antennas and Propagation (APCAP), Kuta, 2015, pp. 333-334.  doi: 10.1109/APCAP.2015.7374394

