[//]: # (-*- coding: utf-8 -*-)

# changes and known issues of airspy-fmradion

## Features under development

* Since 20210427-0, C++17 is required (instead of previous C++11). Modern compilers of Raspberry Pi OS, Ubuntu, and macOS do support C++17 extensions.
* FM Pilot PLL is under revision and reconstruction. Initial analysis result is available at doc/fm-pll-filtereval.py (requires Python 3, SciPy, matplotlib, and NumPy).

## Known limitations

* MacOS build is tested with 10.15.7 Catalina with Xcode 12.4 Command Line Tools. MacOS Big Sur 11.x versions haven't been tested yet.
* For Raspberry Pi 3 and 4, Airspy R2 10Mbps and Airspy Mini 6Mbps sampling rates are *not supported* due to the hardware limitation. Use in 2.5Mbps for R2, 3Mbps for Mini.

## Changes (including requirement changes)

* FM Pilot PLL threshold level has been lowered from 0.01 to 0.001 since 20210607-0, for preventing unwanted unlocking.
* The 2nd-order LPF of FM Pilot PLL had been applied twice since 20210116-0 to 20210427-0, but rolled back to once (as in original SoftFM) since 20210607-0.
* FM ppm display shows ppb (0.001ppm) digits since 20210206-0.
* Timestamp file format has been changed since 20201204-0.
* PortAudio is required since Version 20201023-0. Use PortAudio v19. Former ALSA output driver is replaced by more versatile PortAudio driver, which is compatible both for Linux and macOS.
* libvolk is required since v0.8.0. If you don't want to install libvolk, use v0.7.8 instead. Use the latest master branch of libvolk. Configure the `volk_config` file with `volk_profile -b` for the maximum performance. See [INSTALL-latest-libvolk.md](INSTALL-latest-libvolk.md) for the details.
* v0.8.5 and the earlier versions set the compilation flag of `-ffast-math`, which disabled the processing of NaN. This will cause a latch-up bug when the multipath filter coefficients diverge. Removed `-ffast-math` for the stable operation.
* v0.9.0-test1 to v0.9.5 had calculation error due to `volk_32f_expfast_32f()` in `IfAgc::process()` method. Fixed this by replacing to the more accurate calculation code of `volk_32f_exp_32f()`.

## No more semantic versioning

* Current version number scheme: YYYYMMDD-N (N: subnumber, starting from 0, unsigned integer)
* The semantic versioning scheme of airspy-fmradion has utterly failed.

