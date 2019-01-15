
  SoftFM - Software decoder for FM broadcast radio with RTL-SDR
 ---------------------------------------------------------------

SoftFM is a software-defined radio receiver for FM broadcast radio.
It is written in C++ and uses RTL-SDR to interface with RTL2832-based
hardware.

This program is mostly an experiment rather than a useful tool.
The purposes of SoftFM are
 * experimenting with digital signal processing and software radio;
 * investigating the stability of the 19 kHz pilot;
 * doing the above while listening to my favorite radio station.

Having said that, SoftFM actually produces pretty good stereo sound
when receiving a strong radio station.  Weak stations are noisy,
but SoftFM gets much better results than rtl_fm (bundled with RTL-SDR)
and the few GNURadio-based FM receivers I have seen.

SoftFM provides:
 * mono or stereo decoding of FM broadcasting stations
 * real-time playback to soundcard or dumping to file
 * command-line interface (no GUI, no visualization, nothing fancy)

SoftFM requires:
 * Linux
 * C++11
 * RTL-SDR library (http://sdr.osmocom.org/trac/wiki/rtl-sdr)
 * supported DVB-T receiver
 * medium-fast computer (SoftFM takes 25% CPU time on my 1.6 GHz Core i3)
 * medium-strong FM radio signal

For the latest version, see https://github.com/jorisvr/SoftFM


  Installing
  ----------

The Osmocom RTL-SDR library must be installed before you can build SoftFM.
See http://sdr.osmocom.org/trac/wiki/rtl-sdr for more information.
SoftFM has been tested successfully with RTL-SDR 0.5.3.

To install SoftFM, download and unpack the source code and go to the
top level directory. Then do like this:

 $ mkdir build
 $ cd build
 $ cmake ..

 # CMake tries to find librtlsdr. If this fails, you need to specify
 # the location of the library in one the following ways:
 #
 #  $ cmake .. -DCMAKE_INSTALL_PREFIX=/path/rtlsdr
 #  $ cmake .. -DRTLSDR_INCLUDE_DIR=/path/rtlsdr/include -DRTLSDR_LIBRARY_PATH=/path/rtlsdr/lib/librtlsdr.a
 #  $ PKG_CONFIG_PATH=/path/rtlsdr/lib/pkgconfig cmake ..

 $ make

 $ ./softfm -f <radio-frequency-in-Hz>

 # ( enjoy music )


  License
  -------

SoftFM, copyright (C) 2013, Joris van Rantwijk

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

--
