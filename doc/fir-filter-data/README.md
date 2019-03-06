# airspy-fmradion FIR filter data

* Filter characteristics are described in Scipy code

## Design methods

Parks-McClellan

### 10000kHz-div8-60taps

* IF Stage 1
* <0.01dB: 125kHz, -3dB: 438kHz, -90dB: 1000kHz
* Aliases for center 625kHz: 250~1000kHz
* No alias show up under 156.25kHz (312.5kHz / 2)

### 1250kHz-div4-67taps

* IF Stage 2
* <0.01dB: 80kHz, -3dB: 106.5kHz, -90dB: 156.2kHz

### 312.5kHz-div6-100taps

* Audio stage 1
* <0.01dB: 13.35kHz, -3dB: 18.23kHz, -90dB: 26kHz

### 52.083333kHz-100taps

* Audio stage 2
* Must block 19kHz pilot signal
* <0.01dB: 13.38kHz, -3dB: 15kHz, -90dB: 17.75kHz

