# airspy-fmradion

* Version v0.2.0, 2-MAR-2019
* Software decoder for FM broadcast radio with AirSpy
* For MacOS and Linux
* This repository is forked from [ngsoftfm-jj1bdx](https://github.com/jj1bdx/ngsoftfm-jj1bdx) 0.1.14

## Usage

```sh
airspy-fmradion -q \
    -c freq=88100000,srate=10000000,lgain=2,mgain=0,vgain=10 \
    -b 1.0 -R - | \
    play -t raw -esigned-integer -b16 -r 48000 -c 2 -q -
```

## Introduction

**airspy-fmradion** is a software-defined radio receiver for FM broadcast radio, specifically designed for Airspy R2, forked from [ngsoftfm-jj1bdx](https://github.com/jj1bdx/ngsoftfm-jj1bdx).

### Purposes of airspy-fmradion

Experimenting with digital signal processing and software radio specifically designed for Airspy R2, with a simpler profile than ngsoftfm-jj1bdx.

### airspy-fmradion provides

 - mono or stereo decoding of FM broadcasting stations
 - buffered real-time playback to soundcard or dumping to file
 - command-line interface (*only*)

### airspy-fmradion requires

 - Linux / macOS
 - C++11 (gcc, clang/llvm)
 - [Airspy library](https://github.com/airspy/host/tree/master/libairspy)
 - [sox](http://sox.sourceforge.net/)
 - Tested: Airspy R2
 - Fast computer
 - Medium-strong FM radio signal

For the latest version, see https://github.com/jj1bdx/airspy-fmradion

### Branches and tags

  - Official releases are tagged
  - _master_ is the "production" branch with the most stable release (often ahead of the latest release though)
  - _dev_ is the development branch that contains current developments that will be eventually released in the master branch
  - Other branches are experimental (and abandoned)

## Prerequisites

### Base requirements

  - `sudo apt-get install cmake pkg-config libusb-1.0-0-dev libasound2-dev libboost-all-dev`

### Airspy support

If you install from source (https://github.com/airspy/host/tree/master/libairspy) in your own installation path you have to specify the include path and library path. For example if you installed it in `/opt/install/libairspy` you have to add `-DAIRSPY_INCLUDE_DIR=/opt/install/libairspy/include -DHACKRF_LIBRARY=/opt/install/libairspy/lib/libairspy.a` to the cmake options.

To install the library from a Debian/Ubuntu installation just do:

  - `sudo apt-get install libairspy-dev`

### macOS

* Install HomeBrew `airspy`

## Installing

To install airspy-fmradion, download and unpack the source code and go to the
top level directory. Then do like this:

 - `mkdir build`
 - `cd build`
 - `cmake ..`

CMake tries to find librtlsdr. If this fails, you need to specify
the location of the library in one the following ways:

 - `cmake .. -DRTLSDR_INCLUDE_DIR=/path/rtlsdr/include -DRTLSDR_LIBRARY_PATH=/path/rtlsdr/lib/librtlsdr.a`
 - `PKG_CONFIG_PATH=/path/to/rtlsdr/lib/pkgconfig cmake ..`

Compile and install

 - `make -j8` (for machines with 8 CPUs)
 - `make install`

## Basic command options

 - `-q` Quiet mode.
 - `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
 - `-d devidx` Device index, 'list' to show device list (default 0)
 - `-r pcmrate` Audio sample rate in Hz (default 48000 Hz)
 - `-M` Disable stereo decoding
 - `-R filename` Write audio data as raw S16_LE samples. Uuse filename `-` to write to stdout
 - `-W filename` Write audio data to .WAV file
 - `-P [device]` Play audio via ALSA device (default `default`). Use `aplay -L` to get the list of devices for your system
 - `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
 - `-b seconds` Set audio buffer size in seconds
 - `-X` Shift pilot phase (for Quadrature Multipath Monitor) (-X is ignored under mono mode (-M))
 - `-U` Set deemphasis to 75 microseconds (default: 50)

## Modification from ngsoftfm-jj1bdx

[Twitter @lambdaprog suggested the following strategy](https://twitter.com/lambdaprog/status/1101495337292910594):

> Try starting with 10MSPS and that small conversion filter (7 taps vs. the standard 47 taps), then decimate down to ~312.5 ksps (decimation by 32), then feed the FM demod. The overall CPU usage will be very low and the bit growth will give 14.5 bit resolution.

In airspy-fmradion, the following conversion process is implemented:

* Downsampling factor is split into two stages
* An integer downsampler is added to the first-stage LowPassFilterFirIQ (LPFIQ)
* Input -> LPFIQ 1st -> LPFIQ 2nd -> PhaseDiscriminator
* The filter order of LPFIQ is set to (downsample rate * 8)
* This reduces CPU usage on Mac mini 2018 from ~51% to ~36% in 10Msps
* Finetuner is now not activated unless `USE_FINETUNER` compile-time flag is set
* Not using the finetuner resulted in another 3~4% of CPU usage reduction
* CPU usage: ~51% -> ~29% (on Mac mini 2018, with debug output on)

## Airspy configuration options

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 1.8G. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `10000000`). Valid values depend on the Airspy firmware. Airspy firmware and library must support dynamic sample rate query.
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, list`. `list` lists valid values and exits. (default `8`)
  - `mgain=<x>` Mixer gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `8`)
  - `vgain=<x>` VGA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `0`)
  - `antbias` Turn on the antenna bias for remote LNA (default off)
  - `lagc` Turn on the LNA AGC (default off)
  - `magc` Turn on the mixer AGC (default off)

## Authors

* Joris van Rantwijk
* Edouard Griffiths, F4EXB
* Kenji Rikitake, JJ1BDX

## Acknowledgments

* András Retzler, HA7ILM
* Twitter [@lambdaprog](https://twitter.com/lambdaprog/)

## License

* As a whole package: GPLv3 (and later). See [LICENSE](LICENSE).
* Some source code files are stating GPL "v2 and later" license.
