NGSoftFM
========

**NGSoftFM** is a command line software decoder for FM broadcast radio with stereo support 

<h1>Introduction</h1>

NGSoftFM is a software-defined radio receiver for FM broadcast radio. Stereo
decoding is supported. It is written in C++. It is a derivative work of SoftFM (https://github.com/jorisvr/SoftFM) with a new application design and new features.

For the moment only RTL-SDR based (RTL2832-based) hardware is suppoeted and uses the librtlsdr library to interface with the RTL-SDR hardware.

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
 - supported RTL-SDR DVB-T receiver
 - medium-fast computer (SoftFM takes 25% CPU time on a 1.6 GHz Core i3, ~12% of one core of a Core i7 5700HQ @ 2.7 GHz)
 - medium-strong FM radio signal. However the R820T2 based dongles give much better results than the former R820T based dongles 

For the latest version, see https://github.com/f4exb/ngsoftfm

Branches:

  - _master_ is the "production" branch with the most stable release
  - _dev_ is the development branch that contains current developments that will be eventually released in the master branch


<h1>Prerequisites</h1>

The Osmocom RTL-SDR library must be installed before you can build NGSoftFM.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more information.
NGSoftFM has been tested successfully with RTL-SDR 0.5.3. Normally your distribution should provide the appropriate librtlsdr package

  - `sudo apt-get install libusb-1.0-0-dev librtlsdr-dev libasound2-dev libboost-all-dev`
  
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

 - `./softfm -c freq=94600000` Tunes to 94.6 MHz

Specify gain:

 - `./softfm -c freq=94600000,gain=22.9` Tunes to 94.6 MHz and sets gain to 22.9 dB

<h2>All options</h2>

 - `-c config` Comma separated list of configuration options as key=value pairs or just key for switches. Depends on device type (see next paragraph).
 - `-d devidx` RTL-SDR device index, 'list' to show device list (default 0)
 - `-r pcmrate` Audio sample rate in Hz (default 48000 Hz)
 - `-M ` Disable stereo decoding
 - `-R filename` Write audio data as raw S16_LE samples. Uuse filename `-` to write to stdout
 - `-W filename` Write audio data to .WAV file
 - `-P [device]` Play audio via ALSA device (default `default`). Use `aplay -L` to get the list of devices for your system
 - `-T filename` Write pulse-per-second timestamps. Use filename '-' to write to stdout
 - `-b seconds` Set audio buffer size in seconds

<h2>Device type specific configuration options</h2>

<h3>RTL-SDR</h3>

  - `freq=<int>` Desired tune frequency in Hz (default `100000000`)
  - `gain=<x>` (default `auto`)
    - `auto` Selects gain automatically
    - `list` Lists available gains and exit
    - `<float>` gain in dB. Possible gains in dB are: `0.0, 0.9, 1.4, 2.7, 3.7, 7.7, 8.7, 12.5, 14.4, 15.7, 16.6, 19.7, 20.7, 22.9, 25.4, 28.0, 29.7, 32.8, 33.8, 36.4, 37.2, 38.6, 40.2, 42.1, 43.4, 43.9, 44.5, 48.0, 49.6`
  - `srate=<int>` Device sample rate (default `1000000`)
  - `blklen=<int>` Device block length in bytes (default RTL-SDR default i.e. 64k)
  - `agc` Activates device AGC (default off)

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
