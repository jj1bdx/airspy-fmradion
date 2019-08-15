# airspy-fmradion

* Version v0.7.2, 15-AUG-2019
* For MacOS and Linux

### Known issues

*Use the latest libairspy --HEAD version* (v1.6.7) for:

* Working `airspy_open_devices()`, required by `airspy_open_sn()`. See [this commit](https://github.com/airspy/airspyone_host/commit/61fec20fbd710fc54d57dfec732d314d693b5a2f) for the details.
* Proper transfer block size. `if_blocksize` for Airspy HF+ is reduced from 16384 to 2048, following [this commit](https://github.com/airspy/airspyhf/commit/a1f6f4a0537f53bede6e80c51826fc9d45061c28).

### What is airspy-fmradion?

* **airspy-fmradion** is a software-defined radio receiver for FM and AM broadcast radio, and also DSB/USB/LSB/CW utility communications, specifically designed for Airspy R2, Airspy Mini, Airspy HF+, and RTL-SDR.
* This repository is forked from [ngsoftfm-jj1bdx](https://github.com/jj1bdx/ngsoftfm-jj1bdx) 0.1.14 and merged with [airspfhf-fmradion](https://github.com/jj1bdx/airspyhf-fmradion)

### What does airspy-fmradion provide?

- Mono or stereo decoding of FM broadcasting stations
- Mono decoding of AM broadcasting stations
- Decoding DSB/USB/LSB/CW communication/broadcasting stations
- Decoding narrow band FM communication/broadcasting stations (experimental)
- Buffered real-time playback to soundcard or dumping to file
- Command-line interface (*only*)

## Usage

```sh
airspy-fmradion -t airspy -q \
    -c freq=88100000,srate=10000000,lgain=2,mgain=0,vgain=10 \
    -b 1.0 -R - | \
    play -t raw -esigned-integer -b16 -r 48000 -c 2 -q -

airspy-fmradion -t airspyhf -q \
    -c freq=88100000,srate=768000 \
    -b 1.0 -R - | \
    play -t raw -esigned-integer -b16 -r 48000 -c 2 -q -

airspy-fmradion -m am -t airspyhf -q \
    -c freq=666000 \
    -b 0.5 -F - | \
    play --buffer=1024 -t raw -e floating-point -b32 -r 48000 -c 1 -q -
```

### airspy-fmradion requires

 - Linux / macOS
 - C++11 (gcc, clang/llvm)
 - [Airspy library](https://github.com/airspy/airspyone_host)
 - [Airspy HF library](https://github.com/airspy/airspyhf)
 - [RTL-SDR library](http://sdr.osmocom.org/trac/wiki/rtl-sdr)
 - [sox](http://sox.sourceforge.net/)
 - [The SoX Resampler library aka libsoxr](https://sourceforge.net/p/soxr/wiki/Home/)
 - Tested: Airspy R2, Airspy Mini, Airspy HF+ Dual Port, RTL-SDR V3
 - Fast computer
 - Medium-strong FM and/or AM radio signals, or DSB/USB/LSB/CW signals

For the latest version, see https://github.com/jj1bdx/airspy-fmradion

### Branches and tags

  - Official releases are tagged
  - _master_ is the "production" branch with the most stable release (often ahead of the latest release though)
  - _dev_ is the development branch that contains current developments that will be eventually released in the master branch
  - Other branches are experimental (and abandoned)

## Prerequisites

### Required libraries

If you install from source in your own installation path, you have to specify the include path and library path.
For example if you installed it in `/opt/install/libairspy` you have to add `-DAIRSPY_INCLUDE_DIR=/opt/install/libairspy/include -DAIRSPYHF_INCLUDE_DIR=/opt/install/libairspyhf/include` to the cmake options.

### Debian/Ubuntu Linux

Base requirements:

  - `sudo apt-get install cmake pkg-config libusb-1.0-0-dev libasound2-dev libboost-all-dev`

To install the library from a Debian/Ubuntu installation just do:

  - `sudo apt-get install libairspy-dev libairspyhf-dev librtlsdr-dev libsoxr-dev`

### macOS

* Install HomeBrew `airspy`, `airspyhf`, `rtl-sdr`, and `libsoxr`
* See <https://github.com/pothosware/homebrew-pothos/wiki>
* Use HEAD for `airspy` and `airspyhf`

```shell
brew tap pothosware/homebrew-pothos
brew tap dholm/homebrew-sdr #other sdr apps
brew update
brew install libsoxr
brew install rtl-sdr
brew install airspy --HEAD
brew install airspyhf --HEAD
```

## Installing

To install airspy-fmradion, download and unpack the source code and go to the
top level directory. Then do like this:

 - `mkdir build`
 - `cd build`
 - `cmake ..`

CMake tries to find librtlsdr. If this fails, you need to specify
the location of the library in one the following ways:

```shell
cmake .. \
  -DAIRSPY_INCLUDE_DIR=/path/airspy/include \
  -DAIRSPY_LIBRARY_PATH=/path/airspy/lib/libairspy.a
  -DAIRSPYHF_INCLUDE_DIR=/path/airspyhf/include \
  -DAIRSPYHF_LIBRARY_PATH=/path/airspyhf/lib/libairspyhf.a \
  -DRTLSDR_INCLUDE_DIR=/path/rtlsdr/include \
  -DRTLSDR_LIBRARY_PATH=/path/rtlsdr/lib/librtlsdr.a

PKG_CONFIG_PATH=/path/to/airspy/lib/pkgconfig cmake ..
```

Compile and install

 - `make -j4` (for machines with 4 CPUs)
 - `make install`

## Basic command options

 - `-m devtype` is modulation type, one of `fm`, `am`, `dsb`, `usb`, `lsb`, `cw`, `nbfm` (default fm)
 - `-t devtype` is mandatory and must be `airspy` for Airspy R2 / Airspy Mini, `airspyhf` for Airspy HF+, and `rtlsdr` for RTL-SDR.
 - `-q` Quiet mode.
 - `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
 - `-d devidx` Device index, 'list' to show device list (default 0)
 - `-M` Disable stereo decoding
 - `-R filename` Write audio data as raw `S16_LE` samples. Use filename `-` to write to stdout
 - `-F filename` Write audio data as raw `FLOAT_LE` samples. Use filename `-` to write to stdout
 - `-W filename` Write audio data to .WAV file
 - `-P [device]` Play audio via ALSA device (default `default`). Use `aplay -L` to get the list of devices for your system
 - `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
 - `-b seconds` Set audio buffer size in seconds
 - `-X` Shift pilot phase (for Quadrature Multipath Monitor) (-X is ignored under mono mode (-M))
 - `-U` Set deemphasis to 75 microseconds (default: 50)
 - `-f` Set Filter type
   - for FM: default: +-176kHz, middle: +-145kHz, narrow: +-112kHz
   - for AM: default: +-6kHz, middle: +-4kHz, narrow: +-3kHz
 - `-l dB` Set IF squelch level to minus given value of dB
 - `-E` Enable multipath filter for FM (For stable reception only: turn off if reception becomes unstable)

## Major changes

### Audio gain adjustment

* Since v0.4.2, output maximum level is back at -6dB (0.5) (`adjust_gain()` is reintroduced) again, as in pre-v0.2.7
* During v0.2.7 to v0.4.1, output level was at unity (`adjust_gain()` is removed)
* Before v0.2.7, output maximum level is at -6dB (0.5) 

### Audio and IF downsampling is now performed by libsoxr

* Output of the stereo decoder is downsampled by libsoxr to 48kHz
* Quality: `SOXR_VHQ`
* 19kHz cut LPF implemented for post-processing libsoxr output

### Phase discriminator now uses atan2() as is

* `atan2()` is now used as is for PhaseDiscriminator
* The past `fastatan2()` used in v0.6.10 and before is removed
* Changing from the past `fastatan2()` to `atan2()` reduced the THD+N from approx. 0.9% to approx. 0.6% (measured from JOBK-FM NHK Osaka FM 88.1MHz hourly time tone 880Hz, using airwaves including the multipath distortion)
* [The past `fastatan2()` allowed +-0.005 radian max error](https://www.dsprelated.com/showarticle/1052.php)
* libm `atan2()` allows only approx. 0.5 ULP as the max error for macOS 10.14.5, measured by using the code from ["Error analysis of system mathematical functions
" by Gaston H. Gonnet](http://www-oldurls.inf.ethz.ch/personal/gonnet/FPAccuracy/Analysis.html) (1 ULP for macOS 64bit `double` = 2^(-53) = approx. 10^(15.95))

### FM multipath filter

* An LMS-based multipath filter can be enabled after IF AGC
* 40 IF sample stages before and ahead

## No-goals

* CIC filters for the IF 1st stage (unable to explore parallelism, too complex to compensate)

## Filter design documentation

### General characteristics

* Filter is implemented as the libsoxr sampling converter
* Filter cutoff by libsoxr: 0.913 * sampling frequency

### For FM

* FM Filter coefficients are listed under `doc/filter-design`
* DiscriminatorEqualizer IF range: 180kHz - 1MHz (nyquist: 180kHz - 500kHz)

### For AM

* AM Filter coefficients are listed under `doc/filter-design`
* Max +-5.5kHz IF filter width without aliasing set for all IF filters
* Narrower filters by `-f` options: `middle` +-4kHz, `narrow` +-3kHz

### For SSB

* Filter method applied by shifting 0 - 3kHz to 3 - 6kHz (when sampling frequency is 12kHz)
* SSB filter: designed for 3 to 6kHz, BW 2.4kHz, from 3.3 to 5.7kHz
* Use `-f narrow` option

### For CW

* Pitch: 500Hz
* Filter width: +- 250Hz

### For NBFM

* Deviation: max +-8kHz
* Output audio LPF: flat up to 3kHz

## AM AGC

* Use csdr's `simple_agc_cc` algorithm for IF and Audio AGC functions
* IF AGC: gain up to 100dB (100000)
* Audio AGC: gain up to 7dB (5.0)
* csdr's `fastagc_ff` is not applicable due to variable-length downsampled block output
* TODO: an audio level compression/limiting algorithm?

## FM AGC

* IF AGC: gain up to 80dB (10000)

## Airspy R2 / Mini modification from ngsoftfm-jj1bdx

### The modification strategy

[Twitter @lambdaprog suggested the following strategy](https://twitter.com/lambdaprog/status/1101495337292910594):

> Try starting with 10MSPS and that small conversion filter (7 taps vs. the standard 47 taps), then decimate down to approx. 312.5 ksps (decimation by 32), then feed the FM demod. The overall CPU usage will be very low and the bit growth will give 14.5 bit resolution.

### Feature changes

* Halfband kernel filter designed by Twitter @lambdaprog is set for Airspy conversion filter
* Finetuner is removed (Not really needed for +-1ppm or less offset)
* Audio sample rate is fixed to 48000Hz

### Airspy R2 / Mini configuration options

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 1.8G. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `10000000`). Valid values depend on the Airspy firmware. Airspy firmware and library must support dynamic sample rate query.
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, list`. `list` lists valid values and exits. (default `8`)
  - `mgain=<x>` Mixer gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `8`)
  - `vgain=<x>` VGA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `0`)
  - `antbias` Turn on the antenna bias for remote LNA (default off)
  - `lagc` Turn on the LNA AGC (default off)
  - `magc` Turn on the mixer AGC (default off)

## Airspy HF+ modification from airspy-fmradion v0.2.7

### Sample rates and IF modes

* This version supports 768kHz zero-IF mode and 384/256/192kHz low-IF modes

### Conversion process for 768kHz zero-IF mode

* LPFIQ is single-stage
* IF center frequency is down Fs/4 than the station frequency, i.e: when the station is 76.5MHz, the tuned frequency is 76.308MHz
* Airspy HF+ allows only 660kHz alias-free BW, so the maximum alias-free BW for IF is (660/2)kHz - 192kHz = 138kHz

### Conversion process for 384/256/192kHz low-IF mode

* IF center frequency is not shifted

### Airspy HF configuration options

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 0 to 31M, and from 60M to 240M. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `768000`). Valid values depend on the Airspy HF firmware. Airspy HF firmware and library must support dynamic sample rate query.
  - `hf_att=<int>` HF attenuation level and AGC control.
     - 0: enable AGC, no attenuation
     - 1 - 8: disable AGC, apply attenuation of value * 6dB

## RTL-SDR

### Sample rate

* Valid sample rates are from 1000000 to 1250000 [Hz]
* The default value is 1200000Hz

### RTL-SDR configuration options

  - `freq=<int>` Desired tune frequency in Hz. Accepted range from 10M to 2.2G.
(default 100M: `100000000`)
  - `gain=<x>` (default `auto`)
    - `auto` Selects gain automatically
    - `list` Lists available gains and exit
    - `<float>` gain in dB. Possible gains in dB are: `0.0, 0.9, 1.4, 2.7, 3.7,
7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8
, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6`
  - `srate=<int>` Device sample rate. valid values in the [225001, 300000], [900001, 3200000] ranges. (default `1000000`)
  - `blklen=<int>` Device block length in bytes (default RTL-SDR default i.e. 64k)
  - `agc` Activates device AGC (default off)
  - `antbias` Turn on the antenna bias for remote LNA (default off)

## Authors

* Joris van Rantwijk
* Edouard Griffiths, F4EXB (no longer involving in maintaining NGSoftFM)
* Kenji Rikitake, JJ1BDX (maintainer)
* András Retzler, HA7ILM (for AM AGC code in [csdr](https://github.com/simonyiszk/csdr))

## Acknowledgments

* Twitter [@lambdaprog](https://twitter.com/lambdaprog/), for the intriguing exchange of Airspy product design details and the technical support
* [Iowa Hills Software](http://iowahills.com), for their FIR and IIR filter design tools
* [Brian Beezley, K6STI](http://ham-radio.com/k6sti/), for his comprehensive Web site of FM broadcasting reception expertise and the idea of [Quadrature Multipath Monitor](http://ham-radio.com/k6sti/qmm.htm)

## License

* As a whole package: GPLv3 (and later). See [LICENSE](LICENSE).
* [csdr](https://github.com/simonyiszk/csdr) AGC code: BSD license.
* Some source code files are stating GPL "v2 and later" license.
