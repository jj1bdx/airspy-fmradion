[//]: # (-*- coding: utf-8 -*-)

# airspy-fmradion

* Version 20210702-0
* For MacOS and Linux

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the details.

## Known issues and changes

Please read [CHANGES.md](CHANGES.md) before using the software.

## What is airspy-fmradion?

* **airspy-fmradion** is a software-defined radio receiver for FM and AM broadcast radio, and also NBFM/DSB/USB/LSB/CW/WSPR utility communications, specifically designed for Airspy R2, Airspy Mini, Airspy HF+, and RTL-SDR.

## What does airspy-fmradion provide?

- Mono or stereo decoding of FM broadcasting stations
- Mono decoding of AM stations
- Decoding NBFM/DSB/USB/LSB/CW/WSPR stations
- Buffered real-time playback to soundcard or dumping to file
- Command-line interface (*only*)

## Usage

```sh
airspy-fmradion -t airspy -q \
    -c freq=88100000,srate=10000000,lgain=2,mgain=0,vgain=10 \
    -b 1.0 -P -

airspy-fmradion -t airspyhf -q \
    -c freq=88100000,srate=768000 \
    -b 1.0 -R - | \
    play -t raw -esigned-integer -b16 -r 48000 -c 2 -q -

airspy-fmradion -m am -t airspyhf -q \
    -c freq=666000 \
    -b 0.5 -F - | \
    play --buffer=1024 -t raw -e floating-point -b32 -r 48000 -c 1 -q -
```

### airspy-fmradion requirements

 - Linux / macOS
 - C++17 (gcc, clang/llvm)
 - [Airspy library](https://github.com/airspy/airspyone_host)
 - [Airspy HF library](https://github.com/airspy/airspyhf)
 - [RTL-SDR library](http://sdr.osmocom.org/trac/wiki/rtl-sdr)
 - [sndfile](https://github.com/erikd/libsndfile)
 - [The SoX Resampler library aka libsoxr](https://sourceforge.net/p/soxr/wiki/Home/)
 - [VOLK](http://libvolk.org/)
 - [PortAudio](http://www.portaudio.com)
 - Tested: Airspy R2, Airspy Mini, Airspy HF+ Dual Port, RTL-SDR V3
 - Fast computer
 - Medium-strong radio signals

For the latest version, see https://github.com/jj1bdx/airspy-fmradion

### Recommended utilities

 - [sox](http://sox.sourceforge.net/)

### Branches and tags

  - Official releases are tagged
  - _master_ is the "production" branch with the most stable release (often ahead of the latest release though)
  - _dev_ is the development branch that contains current developments that will be eventually released in the master branch
  - Other branches are experimental (and presumably abandoned)

## Prerequisites

### Airspy HF+ firmware

Use the latest version of Airspy HF+ firmware, available at [Airspy HF+ Dual Port](https://airspy.com/airspy-hf-plus/) and [Airspy HF+ Discovery](https://airspy.com/airspy-hf-discovery/) Web pages.

airspy-fmradion sets the default sampling rates to 384kHz for FM broadcast, and 192kHz for the other modes. Old Airspy HF+ firmwares do not support the lower sampling rate other than 768kHz.

### Required libraries

Note: the master branch of libvolk is now required from v0.8.1.

If you install from source in your own installation path, you have to specify the include path and library path.
For example if you installed it in `/opt/install/libairspy` you have to add `-DAIRSPY_INCLUDE_DIR=/opt/install/libairspy/include -DAIRSPYHF_INCLUDE_DIR=/opt/install/libairspyhf/include` to the cmake options.

### Debian/Ubuntu Linux

  - `sudo apt-get install cmake pkg-config libusb-1.0-0-dev libasound2-dev libairspy-dev libairspyhf-dev librtlsdr-dev libsoxr-dev libsndfile1-dev portaudio19-dev`

### macOS

* Install HomeBrew libraries as in the following shell script
* See <https://github.com/pothosware/homebrew-pothos/wiki>
* Use HEAD for `airspy` and `airspyhf`

```shell
brew tap pothosware/homebrew-pothos
brew tap dholm/homebrew-sdr #other sdr apps
brew update
brew install portaudio
brew install libsoxr
brew install libsndfile
brew install rtl-sdr
brew install airspy --HEAD
brew install airspyhf --HEAD
```

### Install the latest libvolk

Install libvolk as described in [INSTALL-latest-libvolk.md](INSTALL-latest-libvolk.md).

### Dependency installation details

#### libvolk

* See [GitHub gnuradio/volk repository](https://github.com/gnuradio/volk) for the details.
* libvolk requires Boost to compile.

#### libairspyhf

Use the latest HEAD version.

#### libairspy

*Note: this is applicable for both macOS and Linux.*

*Install and use the latest libairspy --HEAD version* for:

* Working `airspy_open_devices()`, required by `airspy_open_sn()`. See [this commit](https://github.com/airspy/airspyone_host/commit/61fec20fbd710fc54d57dfec732d314d693b5a2f) for the details.
* Proper transfer block size. `if_blocksize` for Airspy HF+ is reduced from 16384 to 2048, following [this commit](https://github.com/airspy/airspyhf/commit/a1f6f4a0537f53bede6e80c51826fc9d45061c28).

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

 - `-m devtype` is modulation type, one of `fm`, `nbfm`, `am`, `dsb`, `usb`, `lsb`, `cw`, `wspr` (default fm)
 - `-t devtype` is mandatory and must be `airspy` for Airspy R2 / Airspy Mini, `airspyhf` for Airspy HF+, `rtlsdr` for RTL-SDR, and `filesource` for the File Source driver.
 - `-q` Quiet mode.
 - `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
 - `-d devidx` Device index, 'list' to show device list (default 0)
 - `-M` Disable stereo decoding
 - `-R filename` Write audio data as raw `S16_LE` samples. Use filename `-` to write to stdout
 - `-F filename` Write audio data as raw `FLOAT_LE` samples. Use filename `-` to write to stdout
 - `-W filename` Write audio data to .WAV file
 - `-P device_num` Play audio via PortAudio device index number. Use string `-` to specify the default PortAudio device
 - `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
 - `-b seconds` Set audio buffer size in seconds (default: 1 second)
 - `-X` Shift pilot phase (for Quadrature Multipath Monitor) (-X is ignored under mono mode (-M))
 - `-U` Set deemphasis to 75 microseconds (default: 50)
 - `-f` Set Filter type
   - for FM: wide and default: none, medium: +-156kHz, narrow: +-121kHz
   - for AM: wide: +-9kHz, default: +-6kHz, medium: +-4.5kHz, narrow: +-3kHz
   - for NBFM: wide: +-20kHz, default: +-10kHz, medium: +-8kHz, narrow: +-6.25kHz
 - `-l dB` Enable IF squelch, set the level to minus given value of dB
 - `-E stages` Enable multipath filter for FM (For stable reception only: turn off if reception becomes unstable)
 - `-r ppm` Set IF offset in ppm (range: +-1000000ppm) (Note: this option affects output pitch and timing: *use for the output timing compensation only!*

## Major changes

### Timestamp file format

* For FM: `pps_index sample_index unix_time if_level`
* For the other modes: `block unix_time if_level`
* if\_level is in dB

### Rate compensation for adjusting audio device playback speed offset

* Background: some audio devices shows non-negligible offset of playback speed, which causes eventual audio output buffer overflow and significant delay in long-term playback.
* How to fix: compensating the playback speed offset gives more accurate playback timing, by sacrificing output audio pitch accuracy. A proper compensation will eliminate the cause of increasing output buffer length, by sending less data (lower sampling rate) to the conversion process.
* You can specify the compensation rate by ppm using `-r` option.
* How to estimate the rate offset: when elapsed playback time is `Tp` [seconds] and output buffer length (`buf=` in the debug output) increases during the time is `Ts` [seconds], the compensation rate is `(Ts/Tp) * 1000000` [ppm].
* For example, if the output buffer length increases for 1 second after playing back for 7 hours (25200 seconds), the offset rate is 1/25200 * 1000000 ~= 39.68ppm.
* +- 100ppm offset is not uncommon among the consumer-grade audio devices.
* +- 100ppm pitch change may not be recongizable by human.

#### Caveats for the rate compensation

* *Do not use this feature if the per-sample accuracy is essential.*
* *Do not use this feature for non-realtime output (for example, to files).*
* Output audio pitch increases as the offset increases.
* Too much compensation will cause output underflow.
* This feature causes fractional (non-integer) resampling by `IfResampler` class, which causes more CPU usage.

### Smaller latency

* v0.9.2 uses smaller latency algorithms for all modulation types and filters. The output frequency characteristics may be different from the previous versions.

### Audio gain adjustment

* Since v0.4.2, output maximum level is back at -6dB (0.5) (`adjust_gain()` is reintroduced) again, as in pre-v0.2.7
* During v0.2.7 to v0.4.1, output level was at unity (`adjust_gain()` is removed)
* Before v0.2.7, output maximum level is at -6dB (0.5) 

### Audio and IF downsampling is now performed by libsoxr

* Output of the stereo decoder is downsampled by libsoxr to 48kHz
* Quality: `SOXR_VHQ`
* 19kHz cut LPF implemented for post-processing libsoxr output
* *Do not use* `SOXR_STEEP_FILTER` because it induces unacceptable higher latency
* Audio sample rate is fixed to 48000Hz

### Phase discriminator uses GNU Radio fast_atan2f() 

* From v0.7.8-pre0, GNU Radio `fast_atan2f()` which has ~20-bit accuracy, is used for PhaseDiscriminator class and the 19kHz pilot PLL.
* The past `fastatan2()` used in v0.6.10 and before was removed due to low accuracy (of ~10 bits)
* Changing from the past `atan2()` to `fast_atan2f()` showed no noticeable difference of the THD+N (0.218%) and THD (0.018%). (Measured from JOBK-FM NHK Osaka FM 88.1MHz hourly time tone 880Hz, using airwaves after the multipath canceler filter of -E36)
* [The past `fastatan2()` allowed +-0.005 radian max error](https://www.dsprelated.com/showarticle/1052.php)
* libm `atan2()` allows only approx. 0.5 ULP as the max error for macOS 10.14.5, measured by using the code from ["Error analysis of system mathematical functions
" by Gaston H. Gonnet](http://www-oldurls.inf.ethz.ch/personal/gonnet/FPAccuracy/Analysis.html) (1 ULP for macOS 64bit `double` = 2^(-53) = approx. 10^(15.95))

### FM multipath filter

* A Normalized LMS-based multipath filter can be enabled after IF AGC
* IF sample stages can be defined by `-E` options
* Reference amplitude level: 1.0
* For Mac mini 2018 with 3.2 GHz Intel Core i7, 288 stages consume 99% of one CPU core
* This filter is not effective when the IF bandwidth is narrow (192kHz)
* The multipath filter starts after discarding the first 100 blocks. This change is to avoid the initial instability of Airspy R2.
* Note: this filter recalculates the coefficients for every four (4) samples, to reduce the processing load.

### Multipath filter configuration

* v0.7.3 and later: -E36 for 108 previous and 36 after stages (ratio 3:1). The multipath filter order: (4 * stages) + 1
* For reference only: v0.7.3-pre1 and before: -E72 for 72 previous and 72 after stages (ratio 1:1), summary: set the -E parameter to 1/2 of the previous value
* Rule of thumb: -E36 is sufficient for a stable strong singal (albeit with considerable multipath distortion).

### FM L-R signal boosted for the stereo separation improvement

* Teruhiko Hayashi suggested boosting L-R signal by 1.017 for a better stereo separation. Implemented since v0.7.6-pre3.
* DiscriminatorEqualizer removed since v1.7.6-pre3 (needs more precise compensation, presumably with an FIR filter.

### FM deemphasis error prevention

* Teruhiko Hayashi suggested applying deemphasis *before* the sampling rate conversion, at the demodulator rate, higher than the audio output rate. Implemented since v0.7.6.

## No-goals

* CIC filters for the IF 1st stage (unable to explore parallelism, too complex to compensate)
* Using lock-free threads (`boost::lockfree::spsc_queue` didn't make things faster, and consumed x2 CPU power)

## Filter design documentation

### General characteristics

* Filter is implemented as the libsoxr sampling converter
* Filter cutoff by libsoxr: 0.982 * sampling frequency

### For FM

* FM Filter coefficients are listed under `doc/filter-design`

### For NBFM

* Deviation: normal +-8kHz, for wide +-17kHz
* Output audio LPF: flat up to 4kHz
* NBFM Filter coefficients are listed under `doc/filter-design`
* `default` filter width: +-10kHz
* Narrower filters by `-f` options: `middle` +-8kHz, `narrow` +-6.25kHz
* Wider filters by `-f` options: `wide` +-20kHz (with wider deviation of +-17kHz)
* Audio gain reduced by -3dB to prevent output clipping

### For AM and DSB

* AM Filter coefficients are listed under `doc/filter-design`
* `default` filter width: +-6kHz
* Narrower filters by `-f` options: `middle` +-4.5kHz, `narrow` +-3kHz
* Wider filters by `-f` options: `wide` +-9kHz

### For SSB (USB/LSB)

* For USB: shift down 1.5kHz -> LPF -> shift up 1.5kHz
* For USB: shift up 1.5kHz -> LPF -> shift down 1.5kHz
* Rate conversion of 48kHz to 12kHz and vice versa for the input and output of LPF
* 12kHz sampling rate LPF: flat till +-1200Hz, -3dB +-1320Hz, -10dB +-1370Hz, -58.64dB at +-1465Hz

### For CW

* Zeroed-in pitch: 500Hz
* Filter width: +- 100Hz flat, +-250Hz for cutoff
* Uses downsampling to 12kHz for applying a steep filter

### For WSPR

* Set USB TX carrier frequency for reception (as shown in [WSPRnet](https://wsprnet.org/))
* Pitch: 1500Hz
* Filter width: +- 100Hz flat, +-250Hz for cutoff
* Uses downsampling to 12kHz for applying a steep filter

## AGC algorithm

* Use simple logarithm-based AGC algorithm, which only depends on the single previous sample
* See <https://www.mathworks.com/help/comm/ref/comm.agc-system-object.html> for the implementation details
* IF AGC: gain up to 100dB (100000) (for broadcast FM: 80dB (10000))
* Audio AGC: gain up to 7dB (5.0)

## Airspy R2 / Mini modification from ngsoftfm-jj1bdx

### Feature changes

* Finetuner is removed (Not really needed for +-1ppm or less offset)

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
  - `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `384000`). Valid values depend on the Airspy HF firmware. Airspy HF firmware and library must support dynamic sample rate query.
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

## File Source driver

* Reads an IQ signal format file and decode the output.
* *This device is still experimental.*

### File Source configuration options

  - `freq=<int>` Frequency of radio station in Hz.
  - `srate=<int>` IF sample rate in Hz.
  - `filename=<string>` Source file name. Supported encodings: `FLOAT`, `S24_LE`, `S16_LE`
  - `zero_offset` Set if the source file is in zero offset, which requires Fs/4 IF shifting.
  - `blklen=<int>` Set block length in samples.
  - `raw` Set if the file is raw binary.
  - `format=<string>` Set the file format for the raw binary file. Supported formats: `U8_LE`, `S8_LE`, `S16_LE`, `S24_LE`, `FLOAT`

## Authors and contributors

* Joris van Rantwijk, primary author of SoftFM
* Edouard Griffiths, F4EXB, primary author of NGSoftFM (no longer involving in maintaining NGSoftFM)
* Kenji Rikitake, JJ1BDX, maintainer
* András Retzler, HA7ILM, for the former AF/IF AGC code in [csdr](https://github.com/simonyiszk/csdr)
* Youssef Touil, Airspy Founder, aka Twitter [@lambdaprog](https://twitter.com/lambdaprog/), for the intriguing exchange of Airspy product design details and the technical support
* [Iowa Hills Software](http://iowahills.com), for their FIR and IIR filter design tools
* [Brian Beezley, K6STI](http://ham-radio.com/k6sti/), for his comprehensive Web site of FM broadcasting reception expertise and the idea of [Quadrature Multipath Monitor](http://ham-radio.com/k6sti/qmm.htm)
* [Ryuji Suzuki](https://github.com/rsuzuki0), for reviewing the FM multipath filter coefficients and suggesting putting more weight on picking up more previous samples from the reference point than the samples after
* [Teruhiko Hayashi, JA2SVZ](http://fpga.world.coocan.jp/FM/), the creator of FM FPGA Tuner popular in Japan, for reviewing the measurement results of FM broadcast reception of airspy-fmradion, and various constructive suggestions
* [Takehiro Sekine](https://github.com/bstalk), for suggesting using GNU Radio's [VOLK](http://libvolk.org/) for faster calculation, and implementing Filesource device driver

## License

* As a whole package: GPLv3 (and later). See [LICENSE](LICENSE).
* [csdr](https://github.com/simonyiszk/csdr) AGC code: BSD license.
* Some source code files are stating GPL "v2 and later" license.

## Repository history

* This repository is forked from [ngsoftfm-jj1bdx](https://github.com/jj1bdx/ngsoftfm-jj1bdx) 0.1.14 and merged with [airspfhf-fmradion](https://github.com/jj1bdx/airspyhf-fmradion).
