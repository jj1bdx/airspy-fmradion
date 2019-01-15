NGSoftFM
========

**NGSoftFM** is a command line software decoder for FM broadcast radio with stereo support 

<h1>Introduction</h1>

**NGSoftFM** is a software-defined radio receiver for FM broadcast radio. Stereo
decoding is supported. It is written in C++. It is a derivative work of SoftFM (https://github.com/jorisvr/SoftFM) with a new application design and new features. It also corrects wrong de-emphasis scheme for stereo signals.

Hardware supported:

  - **RTL-SDR** based (RTL2832-based) hardware is suppoeted and uses the _librtlsdr_ library to interface with the RTL-SDR hardware.
  - **HackRF** One and variants are supported with _libhackrf_ library.
  - **Airspy** is supported with _libairspy_ library.
  - **BladeRF** is supported with _libbladerf_ library.

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
 - medium-strong FM radio signal. However the R820T2 based dongles give better results than the former R820T based dongles. HackRF, Airspy or BladeRF are usually even better but you have to spend the buck for the bang. 

For the latest version, see https://github.com/f4exb/ngsoftfm

Branches:

  - _master_ is the "production" branch with the most stable release
  - _dev_ is the development branch that contains current developments that will be eventually released in the master branch


<h1>Prerequisites</h1>

<h2>Base requirements</h2>

  - `sudo apt-get install cmake pkg-config libusb-1.0-0-dev libasound2-dev libboost-all-dev`

<h2>RTL-SDR support</h2>

The Osmocom RTL-SDR library must be installed before you can build NGSoftFM.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more information.
NGSoftFM has been tested successfully with RTL-SDR 0.5.3. Normally your distribution should provide the appropriate librtlsdr package.
If you go with your own installation of librtlsdr you have to specify the include path and library path. For example if you installed it in `/opt/install/librtlsdr` you have to add `-DRTLSDR_INCLUDE_DIR=/opt/install/librtlsdr/include -DRTLSDR_LIBRARY=/opt/install/librtlsdr/lib/librtlsdr.a` to the cmake options

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install librtlsdr-dev`
  
<h2>HackRF support</h2>

For now HackRF support must be installed even if no HackRF device is connected.

If you install from source (https://github.com/mossmann/hackrf/tree/master/host/libhackrf) in your own installation path you have to specify the include path and library path. For example if you installed it in `/opt/install/libhackrf` you have to add `-DHACKRF_INCLUDE_DIR=/opt/install/libhackrf/include -DHACKRF_LIBRARY=/opt/install/libhackrf/lib/libhackrf.a` to the cmake options.

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install libhackrf-dev`
  
<h2>Airspy support</h2>

For now Airspy support must be installed even if no Airspy device is connected.

If you install from source (https://github.com/airspy/host/tree/master/libairspy) in your own installation path you have to specify the include path and library path. For example if you installed it in `/opt/install/libairspy` you have to add `-DAIRSPY_INCLUDE_DIR=/opt/install/libairspy/include -DHACKRF_LIBRARY=/opt/install/libairspy/lib/libairspy.a` to the cmake options.

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install libairspy-dev`
  
<h2>BladeRF support</h2>

For now BladeRF support must be installed even if no Airspy device is connected.

If you install from source (https://github.com/Nuand/bladeRF) in your own installation path you have to specify the include path and library path. For example if you installed it in `/opt/install/libbladerf` you have to add `-DBLADERF_INCLUDE_DIR=/opt/install/libbladerf/include -DBLADERF_LIBRARY=/opt/install/libbladerf/lib/libbladeRF.so` to the cmake options.

To install the library from a Debian/Ubuntu installation just do: 

  - `sudo apt-get install libbladerf-dev`
  
Note: for the BladeRF to work effectively on FM broadcast frequencies you have to fit it with the XB200 extension board.
  
<h1>Installing</h1>

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
 

<h1>Running</h1>

<h2>Examples</h2>

Basic usage:

 - `./softfm -t rtlsdr -c freq=94600000` Tunes to 94.6 MHz

Specify gain:

 - `./softfm -t rtlsdr -c freq=94600000,gain=22.9` Tunes to 94.6 MHz and sets gain to 22.9 dB

<h2>All options</h2>

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

<h2>Device type specific configuration options</h2>

<h3>RTL-SDR</h3>

  - `freq=<int>` Desired tune frequency in Hz. Accepted range from 10M to 2.2G. (default 100M: `100000000`)
  - `gain=<x>` (default `auto`)
    - `auto` Selects gain automatically
    - `list` Lists available gains and exit
    - `<float>` gain in dB. Possible gains in dB are: `0.0, 0.9, 1.4, 2.7, 3.7, 7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6`
  - `srate=<int>` Device sample rate. valid values in the [225001, 300000], [900001, 3200000] ranges. (default `1000000`)
  - `blklen=<int>` Device block length in bytes (default RTL-SDR default i.e. 64k)
  - `agc` Activates device AGC (default off)

<h3>HackRF</h3>

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 6G. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate (default `5000000`). Valid values from 1M to 20M. In fact rates lower than 10M are not specified in the datasheets of the ADC chip however a rate of `1000000` (1M) still works well with NGSoftFM.
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 8, 16, 24, 32, 40, list`. `list` lists valid values and exits. (default `16`)
  - `vgain=<x>` VGA gain in dB. Valid values are: `0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30, 32, 34, 36, 38, 40, 42, 44, 46, 48, 50, 52, 54, 56, 58, 60, 62, list`. `list` lists valid values and exits. (default `22`)
  - `bwfilter=<x>` RF (IF) filter bandwith in MHz. Actual value is taken as the closest to the following values: `1.75, 2.5, 3.5, 5, 5.5, 6, 7,  8, 9, 10, 12, 14, 15, 20, 24, 28, list`. `list` lists valid values and exits. (default `2.5`)
  - `extamp` Turn on the extra amplifier (default off)
  - `antbias` Turn on the antenna bias for remote LNA (default off)
  
<h3>Airspy</h3>

  - `freq=<int>` Desired tune frequency in Hz. Valid range from 1M to 1.8G. (default 100M: `100000000`)
  - `srate=<int>` Device sample rate. `list` lists valid values and exits. (default `10000000`). Valid values depend on the Airspy firmware. Airspy firmware and library must support dynamic sample rate query. 
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, list`. `list` lists valid values and exits. (default `8`)
  - `mgain=<x>` Mixer gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `8`)
  - `vgain=<x>` VGA gain in dB. Valid values are: `0, 1, 2, 3, 4, 5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, list`. `list` lists valid values and exits. (default `0`)  
  - `antbias` Turn on the antenna bias for remote LNA (default off)
  - `lagc` Turn on the LNA AGC (default off)
  - `magc` Turn on the mixer AGC (default off)
  
<h3>BladeRF</h3>

  - `freq=<int>` Desired tune frequency in Hz. Valid range low boundary depends whether the XB200 extension board is fitted (default `300000000`). 
    - XB200 fitted: 100kHz to 3,8 GHz
    - XB200 not fitted: 300 MHZ to 3.8 GHz.
  - `srate=<int>` Device sample rate in Hz. Valid range is 48kHZ to 40MHz. (default `1000000`).
  - `bw=<int>` IF filter bandwidth in Hz. `list` lists valid values and exits. (default `1500000`).
  - `lgain=<x>` LNA gain in dB. Valid values are: `0, 3, 6, list`. `list` lists valid values and exits. (default `3`)
  - `v1gain=<x>` VGA1 gain in dB. Valid values are: `5, 6, 7, 8 ,9 ,10, 11 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, list`. `list` lists valid values and exits. (default `20`)  
  - `v2gain=<x>` VGA2 gain in dB. Valid values are: `0, 3, 6, 9, 12, 15, 18, 21, 24, 27, 30, list`. `list` lists valid values and exits. (default `9`)  


<h1>License</h1>

**NGSoftFM**, copyright (C) 2015, Edouard Griffiths, F4EXB

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, see http://www.gnu.org/licenses/gpl-2.0.html
