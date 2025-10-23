[//]: # (-*- coding: utf-8 -*-)

# airspy-fmradion

* Version 20250929-0
* For macOS (Apple Silicon) and Linux

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for the details.

## Known issues and changes

* Please read [CHANGES.md](CHANGES.md) before using the software.
* Refer to [doc/old-README-until-2023.md](doc/old-README-until-2023.md) for the other miscellaneous technical details.

## What is airspy-fmradion?

* **airspy-fmradion** is software-defined radio receiver (SDR) software with command-line interface.

## What does airspy-fmradion provide?

* Supported SDR frontends: Airspy R2/Mini, Airspy HF+, and RTL-SDR
* Playing back from I/Q WAV files
* Monaural/stereo decoding of FM broadcasting stations
* Monaural decoding of AM stations
* Decoding NBFM/DSB/USB/LSB/CW/WSPR station audio
* Playback to soundcard through PortAudio or saving to file through libsndfile
* Command-line interface

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

* Linux / macOS
* C++20 (gcc, clang/llvm)

Required libraries:

* [Airspy library](https://github.com/airspy/airspyone_host)
* [Airspy HF library](https://github.com/airspy/airspyhf)
* [RTL-SDR library](http://sdr.osmocom.org/trac/wiki/rtl-sdr)
* [libsndfile](https://github.com/erikd/libsndfile)
* [VOLK](https://www.libvolk.org/)
* [PortAudio](http://www.portaudio.com)

The following repository is fetched as a git submodule:

* [r8brain-free-src](https://github.com/avaneev/r8brain-free-src), a sample rate converter designed by Aleksey Vaneev of Voxengo

Repositories in the following list are automatically fetched from CMake during the build process:

* [jj1bdx's fork of cmake-git-version-tracking](https://github.com/jj1bdx/cmake-git-version-tracking)
* [{fmt}](https://fmt.dev/)

### Git branches and tags

* Official releases are tagged
* _main_ is the "production" branch with the most stable release (often ahead of the latest release though)
* _dev_ is the development branch that contains current developments that will be eventually released in the main branch
* Other branches are experimental (and presumably abandoned)

## Prerequisites

### Debian/Ubuntu Linux

```sh
sudo apt-get install cmake pkg-config \
    libusb-1.0-0-dev \
    libasound2-dev \
    libairspy-dev \
    libairspyhf-dev \
    librtlsdr-dev \
    libsndfile1-dev \
    portaudio19-dev \
    libvolk2-dev
```

### macOS

* Install HomeBrew libraries as in the following shell script
* Use HEAD for `airspy` and `airspyhf`

```sh
brew update
brew install portaudio
brew install libsndfile
brew install rtl-sdr
brew install airspy --HEAD
brew install airspyhf --HEAD
brew install volk
brew install fmt
```

### Install the supported libvolk

* Install libvolk as described in [libvolk.md](libvolk.md).
* Run `volk_profile` and save the configuration data for speed optimization.

### Install the supported libsndfile for MP3 capability

* You may need to install libsndfile as described in [libsndfile.md](libsndfile.md).

## Installation

```sh
/bin/rm -rf build
mkdir build
git submodule update --init --recursive
cmake -S . -B build
cmake --build build --target all
```

## Basic command options

* `-m devtype` is modulation type, one of `fm`, `nbfm`, `am`, `dsb`, `usb`, `lsb`, `cw`, `wspr` (default fm)
* `-t devtype` is mandatory and must be `airspy` for Airspy R2 / Airspy Mini, `airspyhf` for Airspy HF+, `rtlsdr` for RTL-SDR, and `filesource` for the File Source driver.
* `-q` Quiet mode.
* `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
* `-d devidx` Device index, 'list' to show device list (default 0)
* `-M` Disable stereo decoding
* `-R filename` Write audio data as raw `S16_LE` samples. Use filename `-` to write to stdout
* `-F filename` Write audio data as raw `FLOAT_LE` samples. Use filename `-` to write to stdout
* `-W filename` Write audio data as RF64/WAV `S16_LE` samples. Use filename `-` to write to stdout (_pipe is not supported_)
* `-G filename` Write audio data as RF64/WAV `FLOAT_LE` samples. Use filename `-` to write to stdout (_pipe is not supported_)
* `-C filename`  Write audio data to MP3 file of VBR -V 1. Use filename '-' to write to stdout. (_This function is available when linked with supported libsndfile only._)
* `-P device_num` Play audio via PortAudio device index number. Use string `-` to specify the default PortAudio device
* `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
* `-X` Shift pilot phase (for Quadrature Multipath Monitor) (-X is ignored under mono mode (-M))
* `-U` Set deemphasis to 75 microseconds (default: 50)
* `-f` Set Filter type
  * for FM: wide and default: none, medium: +-156kHz, narrow: +-121kHz
  * for AM: wide: +-9kHz, default: +-6kHz, medium: +-4.5kHz, narrow: +-3kHz
  * for NBFM: wide: +-20kHz, default: +-10kHz, medium: +-8kHz, narrow: +-6.25kHz
* `-l dB` Enable IF squelch, set the level to minus given value of dB
* `-E stages` Enable multipath filter for FM (For stable reception only: turn off if reception becomes unstable)
* `-r ppm` Set IF offset in ppm (range: +-1000000ppm) (Note: this option affects output pitch and timing: *use for the output timing compensation only!*

## Timestamp file format

* For FM: `pps_index sample_index unix_time if_level`
* For the other modes: `block unix_time if_level`
* if\_level is in dB

## Output audio specification

* Output maximum level is nominally -6dB (0.5) but may increase up to 0dB (1.0)
* Output audio sample rate is fixed to 48000Hz

### FM multipath filter

* A Normalized LMS-based multipath filter can be enabled after IF AGC
* IF sample stages is defined by `-E` options
* Practical upper limit of `-E` value: 200
* Practical `-E` value: up to 50 for Raspberry Pi 4, approx. 100 for a modern computer

## Airspy R2 / Mini configuration options

* `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 1.8G. (default 100M: `100000000`)
* `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `10000000`). Valid values depend on the Airspy firmware. Airspy firmware and library must support dynamic sample rate query.
* `lgain=<x>` LNA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, list`. `list` lists valid values and exits. (default `8`)
* `mgain=<x>` Mixer gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `8`)
* `vgain=<x>` VGA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `0`)
* `antbias` Turn on the antenna bias for remote LNA (default off)
* `lagc` Turn on the LNA AGC (default off)
* `magc` Turn on the mixer AGC (default off)

## Airspy HF+ configuration options

* `freq=<int>` Desired tune frequency in Hz. Valid range from 0 to 31M, and from 60M to 240M. (default 100M: `100000000`)
* `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `384000`). Valid values depend on the Airspy HF firmware. Airspy HF firmware and library must support dynamic sample rate query.
* `hf_att=<int>` HF attenuation level and AGC control.
  * 0: enable AGC, no attenuation
  * 1 - 8: disable AGC, apply attenuation of value * 6dB

## RTL*SDR configuration options

* `freq=<int>` Desired tune frequency in Hz. Accepted range from 10M to 2.2G.
(default 100M: `100000000`)
* `gain=<x>` (default `auto`)
  * `auto` Selects gain automatically
  * `list` Lists available gains and exit
  * `<float>` gain in dB. Possible gains in dB are: `0.0, 0.9, 1.4, 2.7, 3.7,
7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8
, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6`
* `srate=<int>` Device sample rate. valid values in the [900001, 3200000] range. (default `1152000`)
* `blklen=<int>` Device block length in bytes (default RTL-SDR default i.e. 64k)
* `agc` Activates device AGC (default off)
* `antbias` Turn on the antenna bias for remote LNA (default off)

## File Source driver configuration options

* `freq=<int>` Frequency of radio station in Hz.
* `srate=<int>` IF sample rate in Hz.
* `filename=<string>` Source file name. Supported encodings: `FLOAT`, `S24_LE`, `S16_LE`
* `zero_offset` Set if the source file is in zero offset, which requires Fs/4 IF shifting.
* `blklen=<int>` Set block length in samples.
* `raw` Set if the file is raw binary.
* `format=<string>` Set the file format for the raw binary file. Supported formats: `U8_LE`, `S8_LE`, `S16_LE`, `S24_LE`, `FLOAT`

## Authors and contributors

* Joris van Rantwijk, primary author of SoftFM
* Edouard Griffiths, F4EXB, primary author of NGSoftFM (no longer involving in maintaining NGSoftFM)
* Kenji Rikitake, JJ1BDX, maintainer/developer/experimenter
* András Retzler, HA7ILM, for the former AF/IF AGC code in [csdr](https://github.com/simonyiszk/csdr)
* Youssef Touil, Airspy Founder, aka Twitter [@lambdaprog](https://twitter.com/lambdaprog/), for the intriguing exchange of Airspy product design details and the technical support
* [Iowa Hills Software](http://iowahills.com), for their FIR and IIR filter design tools
* [Brian Beezley, K6STI](https://k6sti.neocities.org/), for his comprehensive Web site of FM broadcasting reception expertise and the idea of [Quadrature Multipath Monitor](https://k6sti.neocities.org/qmm)
* [Ryuji Suzuki](https://github.com/rsuzuki0), for reviewing the FM multipath filter coefficients and suggesting putting more weight on picking up more previous samples from the reference point than the samples after
* [Teruhiko Hayashi, JA2SVZ](http://fpga.world.coocan.jp/FM/), the creator of FM FPGA Tuner popular in Japan, for reviewing the measurement results of FM broadcast reception of airspy-fmradion, and various constructive suggestions
* [Takehiro Sekine](https://github.com/bstalk), for suggesting using GNU Radio's [VOLK](https://www.libvolk.org/) for faster calculation, and implementing Filesource device driver
* [Takeru Ohta](https://github.com/sile), for his [Rust implementation](https://github.com/sile/dagc) of [Tisserand-Berviller AGC algorithm](https://hal.univ-lorraine.fr/hal-01397371/document)
* [Cameron Desrochers](https://github.com/cameron314), for his [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) implementation of a single-producer-single-consumer lock-free queue for C++
* [Clayton Smith](https://github.com/argilo), for [a bugfix pull request to airspy-fmradion to find an uninitialized variable](https://github.com/jj1bdx/airspy-fmradion/pull/43) and his help during [bug tracking in VOLK](https://github.com/gnuradio/volk/pull/695).
* [Andrew Hardin](https://github.com/andrew-hardin), for [cmake-git-version-tracking](https://github.com/andrew-hardin/cmake-git-version-tracking.git)

## License

* As a whole package: GPLv3 (and later). See [LICENSE](LICENSE).
* Each source code file might state a GPLv3-compatible license.

[End of README.md]
