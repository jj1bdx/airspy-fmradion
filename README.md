# ngsoftfm-jj1bdx

* Version 0.1.9, 22-JAN-2019
* Software decoder for FM broadcast radio with RTL-SDR, AirSpy, and HackRF
* For MacOS, Linux, and FreeBSD
* This repository is forked from [NGSoftFM](https://github.com/f4exb/ngsoftfm)
* *Note: F4EXB no longer maintains this code.*
* Code merged from [softfm-jj1bdx](https://github.com/jj1bdx/softfm-jj1bdx)
* The same as original NGSoftFM: IF bandwidth: 200kHz (+-100kHz), default sample rate for RTL-SDR to 1000kHz

## Modification by @jj1bdx

* Remove 19kHz pilot signal when the stereo PLL is locked
* Add equalizer to compensate 0th-hold aperture effect of phase discriminator output
* The compensation equalizer output parameters are pre-calculated and interpolated from 200kHz ~ 10MHz sampling rates
* Increase the number of FineTuner table size from 64 to 256
* Add quiet mode `-q`
* Add option `-X` for [Quadratic Multipath Monitor (QMM)](http://ham-radio.com/k6sti/qmm.htm) (This option is not effective in monaural mode (`-M` option))
* Add option `-U` to set deemphasis timing to 75 microseconds for North America (default: 50 microseconds for Europe/Japan)
* Add D/U ratio estimation based on I/F level: see <https://github.com/jj1bdx/rtl_power-fm-multipath> (this requires higher sampling rate above 900kHz)

## Introduction

**NGSoftFM** is a software-defined radio receiver for FM broadcast radio. Stereo
decoding is supported. It is written in C++. It is a derivative work of SoftFM (https://github.com/jorisvr/SoftFM) with a new application design and new features. It also corrects wrong de-emphasis scheme for stereo signals.

Hardware supported:

  - **RTL-SDR** based (RTL2832-based) hardware is suppoeted and uses the _librtlsdr_ library to interface with the RTL-SDR hardware.
  - **HackRF** One and variants are supported with _libhackrf_ library.
  - **Airspy** is supported with _libairspy_ library.

The purposes of NGSoftFM are:

 - experimenting with digital signal processing and software radio;
 - investigating the stability of the 19 kHz pilot;
 - doing the above while listening to my favorite radio station.

NGSoftFM actually produces pretty good stereo sound
when receiving a strong radio station.  Weak stations are noisy,
but NGSoftFM gets much better results than rtl_fm (bundled with RTL-SDR)
and the few GNURadio-based FM receivers I have seen.

NGSoftFM provides:

 - mono or stereo decoding of FM broadcasting stations
 - real-time playback to soundcard or dumping to file
 - command-line interface (no GUI, no visualization, nothing fancy)

NGSoftFM requires:

 - Linux
 - C++11
 - RTL-SDR library (http://sdr.osmocom.org/trac/wiki/rtl-sdr)
 - HackRF library (https://github.com/mossmann/hackrf/tree/master/host/libhackrf)
 - Airspy library (https://github.com/airspy/host/tree/master/libairspy)
 - supported RTL-SDR DVB-T receiver or HackRF Rx/Tx
 - medium-fast computer (NGSoftFM takes 25% CPU time on a 1.6 GHz Core i3, ~12% of one core of a Core i7 5700HQ @ 2.7 GHz)
 - medium-strong FM radio signal. However the R820T2 based dongles give better results than the former R820T based dongles. HackRF, or Airspy are usually even better but you have to spend the buck for the bang. 

For the latest version, see https://github.com/jj1bdx/ngsoftfm-jj1bdx

Branches:

  - _master_ is the "production" branch with the most stable release
  - _dev_ is the development branch that contains current developments that will be eventually released in the master branch


## Prerequisites

### Base requirements

  - `sudo apt-get install cmake pkg-config libusb-1.0-0-dev libasound2-dev libboost-all-dev`

### RTL-SDR support

The Osmocom RTL-SDR library must be installed before you can build NGSoftFM.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more information.
NGSoftFM has been tested successfully with RTL-SDR 0.5.3. Normally your distribution should provide the appropriate librtlsdr package.
If you go with your own installation of librtlsdr you have to specify the include path and library path. For example if you installed it in `/opt/install/librtlsdr` you have to add `-DRTLSDR_INCLUDE_DIR=/opt/install/librtlsdr/include -DRTLSDR_LIBRARY=/opt/install/librtlsdr/lib/librtlsdr.a` to the cmake options

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install librtlsdr-dev`
  
### HackRF support

For now HackRF support must be installed even if no HackRF device is connected.

If you install from source (https://github.com/mossmann/hackrf/tree/master/host/libhackrf) in your own installation path you have to specify the include path and library path. For example if you installed it in `/opt/install/libhackrf` you have to add `-DHACKRF_INCLUDE_DIR=/opt/install/libhackrf/include -DHACKRF_LIBRARY=/opt/install/libhackrf/lib/libhackrf.a` to the cmake options.

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install libhackrf-dev`
  
### Airspy support

For now Airspy support must be installed even if no Airspy device is connected.

If you install from source (https://github.com/airspy/host/tree/master/libairspy) in your own installation path you have to specify the include path and library path. For example if you installed it in `/opt/install/libairspy` you have to add `-DAIRSPY_INCLUDE_DIR=/opt/install/libairspy/include -DHACKRF_LIBRARY=/opt/install/libairspy/lib/libairspy.a` to the cmake options.

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install libairspy-dev`
  
### FreeBSD

* Install Port `comms/rtl-sdr`, `comms/hackrf`, `comms/airspy`

### macOS

* Install HomeBrew `rtl-sdr`, `hackrf`, `airspy`

## Installing

To install NGSoftFM, download and unpack the source code and go to the
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
 

## Running

### Examples

Basic usage:

 - `./ngsoftfm -t rtlsdr -c freq=94600000` Tunes to 94.6 MHz

Specify gain:

 - `./ngsoftfm -t rtlsdr -c freq=94600000,gain=22.9` Tunes to 94.6 MHz and sets gain to 22.9 dB

### All options

 - `-t devtype` is mandatory and must be `rtlsdr` for RTL-SDR devices or `hackrf` for HackRF.
 - `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
 - `-d devidx` Device index, 'list' to show device list (default 0)
 - `-r pcmrate` Audio sample rate in Hz (default 48000 Hz)
 - `-M ` Disable stereo decoding
 - `-R filename` Write audio data as raw S16_LE samples. Uuse filename `-` to write to stdout
 - `-W filename` Write audio data to .WAV file
 - `-P [device]` Play audio via ALSA device (default `default`). Use `aplay -L` to get the list of devices for your system
 - `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
 - `-b seconds` Set audio buffer size in seconds

### Device type specific configuration options

#### RTL-SDR

  - `freq=<int>` Desired tune frequency in Hz. Accepted range from 10M to 2.2G. (default 100M: `100000000`)
  - `gain=<x>` (default `auto`)
    - `auto` Selects gain automatically
    - `list` Lists available gains and exit
    - `<float>` gain in dB. Possible gains in dB are: `0.0, 0.9, 1.4, 2.7, 3.7, 7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6`
  - `srate=<int>` Device sample rate. valid values in the [225001, 300000], [900001, 3200000] ranges. (default `1000000`)
  - `blklen=<int>` Device block length in bytes (default RTL-SDR default i.e. 64k)
  - `agc` Activates device AGC (default off)

#### HackRF

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 6G. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate (default `5000000`). Valid values from 1M to 20M. In fact rates lower than 10M are not specified in the datasheets of the ADC chip however a rate of `1000000` (1M) still works well with NGSoftFM.
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 8, 16, 24, 32, 40, list`. `list` lists valid values and exits. (default `16`)
  - `vgain=<x>` VGA gain in dB. Valid values are: `0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, list`. `list` lists valid values and exits. (default `22`)
  - `bwfilter=<x>` RF (IF) filter bandwith in MHz. Actual value is taken as the closest to the following values: `1.75, 2.5, 3.5, 5, 5.5, 6, 7,  8, 9, 10, 12, 14, 15, 20, 24, 28, list`. `list` lists valid values and exits. (default `2.5`)
  - `extamp` Turn on the extra amplifier (default off)
  - `antbias` Turn on the antenna bias for remote LNA (default off)
  
#### Airspy

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

## License

* As a whole package: GPLv3 (and later). See [LICENSE](LICENSE).
* Some source code files are stating GPL "v2 and later" license.
