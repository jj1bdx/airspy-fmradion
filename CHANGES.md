[//]: # (-*- coding: utf-8 -*-)

# changes and known issues of airspy-fmradion

## libusb-1.0.25 glitch

* Note: This problem has been fixed by the latest implementation of Airspy HF+ driver after [this commit](https://github.com/airspy/airspyhf/commit/3b823ad8fa729358e0729e6c1ca60ac5dfcd656e).
* The author has noticed [libusb-1.0.25 on macOS 12.2 causes segfault when stopping the code with SIGINT or SIGTERM with Airspy HF+ Discovery](https://github.com/jj1bdx/airspy-fmradion/issues/35). 
* A proper fix of this is to [fix the Airspy HF+ driver](https://github.com/airspy/airspyhf/pull/31).
* [A similar case of SDR++ with ArchLinux](https://github.com/libusb/libusb/issues/1059#issuecomment-1030638617) is also reported.
* Since Version 20220205-0, a workaround is implemented to prevent data loss for this bug: the main() loop closes the audio output before calling the function which might cause this segmentation fault (SIGSEGV), which is the stopping function of the SDR source driver. 
* Airspy R2 and Mini are not affected. Use the latest driver with [this fix](https://github.com/airspy/airspyone_host/commit/41c439f16818d931c4d0f8a620413ea5131c0bd6).
* You can still use 20220205-1 if you need to; there is no functional difference between 20220205-1 and 20220206-0.

## Platforms tested

* Mac mini 2018, macOS 12.5 x86\_64, Xcode 13.4 Command Line Tools
* MacBook Air 13" Apple Silicon (M1) 2020, macOS 12.5 arm64, Xcode 13.4 Command Line Tools
* Ubuntu 22.04 LTS x86\_64, gcc 11.2

## Features under development

* Since 20220810-1, IF AGC max gain for FM is raised to 10^5, AM/DSB/USB/LSB/WSPR/CW is raised to 10^6.
* Since 20220810-0, AGC algorithms are refactored.
* Since 20220808-3, Tisserand-Berviller AGC algorithm is implemented also to AF AGC.
* Since 20220808-0, Tisserand-Berviller AGC algorithm is implemented to IF AGC.
* Since 20210427-0, C++17 is required (instead of previous C++11). Modern compilers of Raspberry Pi OS, Ubuntu, and macOS do support C++17 extensions.
* FM Pilot PLL is under revision and reconstruction. Initial analysis result is available at doc/fm-pll-filtereval.py (requires Python 3, SciPy, matplotlib, and NumPy).

## Known limitations

* For Raspberry Pi 3 and 4, Airspy R2 10Msps and Airspy Mini 6Msps sampling rates are *not supported* due to the hardware limitation. Use in 2.5Msps for R2, 3Msps for Mini.

## Changes (including requirement changes)

* 20220809-0: Source code comments and documentation changes only.
* 20220808-3: AF AGC is replaced by the Tisserand-Berviller AGC algorithm. This is still experimental and more evaluation is needed. Output level of the AM (including USB/LSB/DSB/CW/WSPR) modes may increase by 2dB to 3dB, due to the algorithm change.
* 20220808-1: commit 40e342b2cf0e6710800c578272caf515a8b83add: IF AGC distortion rate reduced to improve multipath filter result.
* 20220808-0: IF AGC is replaced by the Tisserand-Berviller AGC algorithm. This is still experimental and more evaluation is needed.
* 20220412-0: Re-enabled experimental FM AFC code after the continuous-phase frequency shifting was implemented in the commit 37742981c34e53eb8083af07c0bc518491dc18ee.
* 20220313-1: Removed experimental FM AFC code due to periodical noise generation. `-A` option is removed as well.
* 20220313-0: Moved FineTuner object into independent files. Added experimental 10Hz-step IF AFC for FM broadcast (use `-A` option to enable). Simplified INSTALL-latest-libvolk.md.
* 20220221-0: Shortened polling periods for Airspy R2/Mini and Airspy HF+ from 1 second to 100 milliseconds. Also reduced AGC output levels for CW and WSPR to prevent output overdrive.
* 20220206-0: Rolled back the workaround of exit(0) in 20220205-1, because this is no longer necessary when a proper fix is done on Airspy HF+ driver.
* 20220205-1: Rolled back Airspy HF+ source driver stop/close semantics. Add exit(0) at the end of program to force-exit the code to avoid causing segfault.
* 20220205-0: Signal handling is now performed on a dedicated thread. SIGQUIT is also captured and will terminate the program gracefully as SIGINT and SIGTERM does. Redundant initialization sequences removed from Airspy HF+ source driver.
* 20220203-0: Explicitly state that pipe is not supported for `-W` and `-G` RIFF/WAV file output options.
* 20211209-0: Support for Apple Silicon M1: Add more default dirctories to CMakeLists.txt, add VOLK 2.5 installation instruction
* 20211101-0: `handle_sigterm()` now uses `psignal()` instead of `strsignal()` for the thread safety of Linux. Also fixed the bug of not saving `errno` in the signal handler. This bug was found by the ThreadSanitizer of macOS clang.
* 20211022-0: minor bugfix of COEFF\_MONITOR coefficient display code.
* Since 20210718-0, receiving block number is uint64\_t, and 12 digits are displayed.
* Since 20210709-0, all file output is controlled under libsndfile. Previous output formats are compatible with the older version of airspy-fmradion.
* Since 20210709-0, WAV file output is RF64 compatible, and automatically degraded to WAV if the output is less than WAV file length limit (4GB), controlled by libsndfile.
* Since 20210709-0, -G option is added for RF64/WAV FLOAT\_LE output.
* Since 20210702-0, if IF rate is 3.1MHz (3100kHz) or larger, a decimation LPF of +-400kHz width by decimation ratio 4 is inserted after the Fs/4 shifter to reduce the ratio of fractional resampler for increasing the output stability to prevent FM stereo PLL unlocking. This function increases the CPU usage, so for a lower CPU usage use a lower sampling rate.
* Since 20210702-0, halfband filter kernel for Airspy R2/Mini is no longer used.
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

