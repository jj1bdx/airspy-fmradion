[//]: # (-*- coding: utf-8 -*-)

# airspy-fmradion

* Version 20240107-0
* For macOS (Apple Silicon) and Linux

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the details.

## Known issues and changes

Please read [CHANGES.md](CHANGES.md) before using the software.

## What is airspy-fmradion?

* **airspy-fmradion** is software-defined radio receiver (SDR) software with command-line interface.

## What does airspy-fmradion provide?

- Supported SDR frontends: Airspy R2/Mini, Airspy HF+, and RTL-SDR
- I/Q WAV file frontend is also supported
- Mono or stereo decoding of FM broadcasting stations
- Mono decoding of AM stations
- Decoding NBFM/DSB/USB/LSB/CW/WSPR stations
- Playback to soundcard through PortAudio or dumping to file
- Command-line interface (*only*)

## Usage

```sh
# Portaudio output
airspy-fmradion -t airspy -q \
    -c freq=82500k,srate=1000k,lgain=2,mgain=0,vgain=10 \
    -P -

# 16-bit signed integer WAV output (pipe is not supported)
airspy-fmradion -t airspyhf -q \
    -c freq=82500000,srate=384000 \
    -W output_s16_le.wav

# 32-bit float WAV output (pipe is not supported)
airspy-fmradion -m am -t airspyhf -q \
    -c freq=666k \
    -G output_f32_le.wav
```

### airspy-fmradion requirements

 - Linux / macOS
 - C++17 (gcc, clang/llvm)
 - [Airspy library](https://github.com/airspy/airspyone_host)
 - [Airspy HF library](https://github.com/airspy/airspyhf)
 - [RTL-SDR library](http://sdr.osmocom.org/trac/wiki/rtl-sdr)
 - [sndfile](https://github.com/erikd/libsndfile)
 - [r8brain-free-src](https://github.com/avaneev/r8brain-free-src), a sample rate converter designed by Aleksey Vaneev of Voxengo 
 - [VOLK](https://www.libvolk.org/)
 - [PortAudio](http://www.portaudio.com)
 - [jj1bdx's fork of cmake-git-version-tracking](https://github.com/jj1bdx/cmake-git-version-tracking)
 - Supported SDR frontends: Airspy R2, Airspy Mini, Airspy HF+ Dual Port, Airspy HF+ Discovery, and RTL-SDR V3

For the latest version, see <https://github.com/jj1bdx/airspy-fmradion>

### Git branches and tags

  - Official releases are tagged
  - _main_ is the "production" branch with the most stable release (often ahead of the latest release though)
  - _dev_ is the development branch that contains current developments that will be eventually released in the main branch
  - Other branches are experimental (and presumably abandoned)

## Prerequisites

### Airspy HF+ firmware

Use the latest version of Airspy HF+ firmware, available at [Airspy HF+ Dual Port](https://airspy.com/airspy-hf-plus/) and [Airspy HF+ Discovery](https://airspy.com/airspy-hf-discovery/) Web pages.

airspy-fmradion sets the default sampling rates to 384kHz for FM broadcast, and 192kHz for the other modes.

### Required libraries

If you install from source in your own installation path, you have to specify the include path and library path.
For example if you installed it in `/opt/install/libairspy` you have to add `-DAIRSPY_INCLUDE_DIR=/opt/install/libairspy/include -DAIRSPYHF_INCLUDE_DIR=/opt/install/libairspyhf/include` to the cmake options.

### Debian/Ubuntu Linux

  - `sudo apt-get install cmake pkg-config libusb-1.0-0-dev libasound2-dev libairspy-dev libairspyhf-dev librtlsdr-dev libsndfile1-dev portaudio19-dev`

### macOS

* Install HomeBrew libraries as in the following shell script
* See <https://github.com/pothosware/homebrew-pothos/wiki>
* Use HEAD for `airspy` and `airspyhf`

```shell
brew update
brew install portaudio
brew install libsndfile
brew install rtl-sdr
brew install airspy --HEAD
brew install airspyhf --HEAD
brew install volk
```

### Install the supported libvolk

Install libvolk as described in [libvolk.md](libvolk.md).

### Dependency installation details

#### libvolk

* See [GitHub gnuradio/volk repository](https://github.com/gnuradio/volk) for the details.

#### libairspyhf

Use the latest HEAD version.

#### libairspy

*Note: this is applicable for both macOS and Linux.*

*Install and use the latest libairspy --HEAD version*.

#### git submodules

r8brain-free-src is the submodule of this repository. Download the submodule repositories by the following git procedure:

- `git submodule update --init --recursive`

## Installing

### A quick way

```shell
/bin/rm -rf build
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target all
```

### In details

To install airspy-fmradion, download and unpack the source code and go to the top level directory. Then do like this:

 - `git submodule update --init --recursive`
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

### Static analysis of the code

For using static analyzers such as [OCLint](https://oclint.org) and [Clangd](https://clangd.llvm.org), use the `compile_commands.json` file built in `build/` directory, with the following commands:

```
cd build
ln -s `pwd`/compile_commands.json ..
```

The following limitation is applicable:

* For *CMake 3.20 or later*, cmake-git-version-tracking code is intentionally removed from the compile command database. This is not applicable for the older CMake. 
* Use [compdb](https://github.com/Sarcasm/compdb.git) for a more precise analysis including all the header files, with the following command: `compdb -p build/ list > compile_commands.json`

### Compile and install

 - `make -j4` (for machines with 4 CPUs)
 - `make install`

## Basic command options

*Note well: `-b` option is removed and will cause an error.*

 - `-m devtype` is modulation type, one of `fm`, `nbfm`, `am`, `dsb`, `usb`, `lsb`, `cw`, `wspr` (default fm)
 - `-t devtype` is mandatory and must be `airspy` for Airspy R2 / Airspy Mini, `airspyhf` for Airspy HF+, `rtlsdr` for RTL-SDR, and `filesource` for the File Source driver.
 - `-q` Quiet mode.
 - `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
 - `-d devidx` Device index, 'list' to show device list (default 0)
 - `-M` Disable stereo decoding
 - `-R filename` Write audio data as raw `S16_LE` samples. Use filename `-` to write to stdout
 - `-F filename` Write audio data as raw `FLOAT_LE` samples. Use filename `-` to write to stdout
 - `-W filename` Write audio data as RF64/WAV `S16_LE` samples. Use filename `-` to write to stdout (*pipe is not supported*)
 - `-G filename` Write audio data as RF64/WAV `FLOAT_LE` samples. Use filename `-` to write to stdout (*pipe is not supported*)
 - `-P device_num` Play audio via PortAudio device index number. Use string `-` to specify the default PortAudio device
 - `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
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

### Audio gain adjustment

* Output maximum level is back at -6dB (0.5) (See `adjust_gain()`)

### Audio and IF downsampling is performed by r8brain-free-src

* Output of the stereo decoder is downsampled to 48kHz
* 19kHz cut LPF implemented for post-processing resampler output
* Output audio sample rate is fixed to 48000Hz
* `r8b::CDSPResampler24` is used for IF resampling

### Phase discriminator uses GNU Radio fast\_atan2f() 

* GNU Radio `fast_atan2f()` which has ~20-bit accuracy, is used for PhaseDiscriminator class and the 19kHz pilot PLL.

### FM multipath filter

* A Normalized LMS-based multipath filter can be enabled after IF AGC
* IF sample stages can be defined by `-E` options
* Reference amplitude level: 1.0
* Practical upper limit of `-E` value: 200
* This filter is not effective when the IF bandwidth is narrow (192kHz)
* This filter recalculates the coefficients for every four (4) samples, to reduce the processing load
* A configuration example of stages: `-E36 for 108 previous and 36 after stages (ratio 3:1).
* The multipath filter order: (4 * stages) + 1

### FM L-R signal boosted for the stereo separation improvement

* Implemented: Teruhiko Hayashi suggested boosting L-R signal by 1.017 for a better stereo separation.

### FM deemphasis error prevention

* Implemented: Teruhiko Hayashi suggested applying deemphasis *before* the sampling rate conversion, at the demodulator rate, higher than the audio output rate.

## Filter design documentation

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

## AGC algorithms

### Simple AGC with Tisserand-Berviller Algorithm

* Reference: Etienne Tisserand, Yves Berviller. Design and implementation of a new digital automatic gain control. Electronics Letters, IET, 2016, 52 (22), pp.1847 - 1849. ff10.1049/el.2016.1398ff. ffhal-01397371f
* Implementation reference: <https://github.com/sile/dagc/>

## Airspy R2 / Mini configuration options

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 1.8G. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `10000000`). Valid values depend on the Airspy firmware. Airspy firmware and library must support dynamic sample rate query.
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, list`. `list` lists valid values and exits. (default `8`)
  - `mgain=<x>` Mixer gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `8`)
  - `vgain=<x>` VGA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `0`)
  - `antbias` Turn on the antenna bias for remote LNA (default off)
  - `lagc` Turn on the LNA AGC (default off)
  - `magc` Turn on the mixer AGC (default off)

## Airspy HF+ modification 

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
  - `srate=<int>` Device sample rate. valid values in the [900001, 3200000] range. (default `1152000`)
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
* Kenji Rikitake, JJ1BDX, maintainer/developer/experimenter
* András Retzler, HA7ILM, for the former AF/IF AGC code in [csdr](https://github.com/simonyiszk/csdr)
* Youssef Touil, Airspy Founder, aka Twitter [@lambdaprog](https://twitter.com/lambdaprog/), for the intriguing exchange of Airspy product design details and the technical support
* [Iowa Hills Software](http://iowahills.com), for their FIR and IIR filter design tools
* [Brian Beezley, K6STI](http://ham-radio.com/k6sti/), for his comprehensive Web site of FM broadcasting reception expertise and the idea of [Quadrature Multipath Monitor](http://ham-radio.com/k6sti/qmm.htm)
* [Ryuji Suzuki](https://github.com/rsuzuki0), for reviewing the FM multipath filter coefficients and suggesting putting more weight on picking up more previous samples from the reference point than the samples after
* [Teruhiko Hayashi, JA2SVZ](http://fpga.world.coocan.jp/FM/), the creator of FM FPGA Tuner popular in Japan, for reviewing the measurement results of FM broadcast reception of airspy-fmradion, and various constructive suggestions
* [Takehiro Sekine](https://github.com/bstalk), for suggesting using GNU Radio's [VOLK](https://www.libvolk.org/) for faster calculation, and implementing Filesource device driver
* [Takeru Ohta](https://github.com/sile), for his [Rust implementation](https://github.com/sile/dagc) of [Tisserand-Berviller AGC algorithm](https://hal.univ-lorraine.fr/hal-01397371/document)
* [Cameron Desrochers](https://github.com/cameron314), for his [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) implementation of a single-producer-single-consumer lock-free queue for C++
* [Clayton Smith](https://github.com/argilo), for [a bugfix pull request to airspy-fmradion to find an uninitialized variable](https://github.com/jj1bdx/airspy-fmradion/pull/43) and his help during [bug tracking in VOLK](https://github.com/gnuradio/volk/pull/695).
* [Andrew Hardin](https://github.com/andrew-hardin), for [cmake-git-version-tracking](https://github.com/andrew-hardin/cmake-git-version-tracking.git)

## License

* As a whole package: GPLv3 (and later). See [LICENSE](LICENSE).
* [csdr](https://github.com/simonyiszk/csdr) AGC code: BSD license.
* Some source code files are stating GPL "v2 and later" license, and the MIT License.

[End of README.md]