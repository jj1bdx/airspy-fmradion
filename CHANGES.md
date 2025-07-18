[//]: # (-*- coding: utf-8 -*-)

# changes and known issues of airspy-fmradion

## Git submodules required

The following submodule is required:

* [r8brain-free-src](https://github.com/avaneev/r8brain-free-src)

## External version control code required

The following Git repository is required:

* [jj1bdx's fork of cmake-git-version-tracking](https://github.com/jj1bdx/cmake-git-version-tracking)

## Platforms tested

* Mac mini 2023 Apple Silicon (M2 Pro), macOS 15.5, Apple clang version 17.0.0 (clang-1700.0.13.5)
* Ubuntu 24.04 LTS x86\_64, gcc 14.2.0
* Raspberry Pi 5 with Raspberry Pi OS 64bit Lite (Debian Bookworm)

## Features under development

* FM Pilot PLL is under revision and reconstruction. Initial analysis result is available at doc/fm-pll-filtereval.py (requires Python 3, SciPy, matplotlib, and NumPy).

## Known limitations

* `{fmt}` aka libfmt 11.0.2 or later must be installed for formatted text printing.
  * This is required for the planned future C++23 `std::print()` functionality requirement.
* libsndfile 1.1 or later must be installed to support MP3 file output.
* For Raspberry Pi 3 and 4, Airspy R2 10Msps and Airspy Mini 6Msps sampling rates are *not supported* due to the hardware limitation. Use in 2.5Msps for R2, 3Msps for Mini.
* Since 20231227-0, the buffer length option `-b` is no longer handled and will generate an error. The audio sample data sent to AudioOutput base classes are no longer pre-buffered.
* The author observed anomalies of being unable to run PortAudio with the `snd_aloop` loopback device while testing on Raspberry Pi OS 32bit Debian *Bullseye*. Portaudio anomaly support is out of our development scope.

### Intel Mac support is dropped

Intel Mac hardware is no longer supported by airspy-fmradion, although the author makes the best effort to prevent introducing anything against the compilation on the Intel Macs. Please open an issue on the GitHub repository if you find anything incompatible on Intel Macs.

## Changes (including requirement changes)

* 20250714-0: no major functionality changes from 20241208-0.
* 20241208-0: [Use {fmt} as the output library.](https://github.com/jj1bdx/airspy-fmradion/pull/83)
  * {fmt} 11.0.2 or later is required.
* 20240424-0: Made the following changes:
  * [Add libairspyhf latest version document.](https://github.com/jj1bdx/airspy-fmradion/pull/80)
    * Airspy HF+ Firmware R3.0.7 and R4.0.8 both work OK on libairspyhf 1.6.8.
    * For the Firmware R4.0.8, use libairspy 1.8 to have full compatibility.
  * [Use shared libraries for airspy, airspyhf, and rtl-sdr.](https://github.com/jj1bdx/airspy-fmradion/pull/79)
  * Tested `airspy_set_packing()` for Airspy R2, but this increased CPU usage on Apple Silicon M2 Pro, so the change was not incorporated.
* 20240316-0: Made the following changes:
  * Raspberry Pi 4 with Raspberry Pi OS 64bit lite is now officially tested.
  * *Note well: Raspberry Pi OS 32bit is not supported*.
  * [`-A` AFC option is removed.](https://github.com/jj1bdx/airspy-fmradion/pull/70)
  * [Change VOLK version display format.](https://github.com/jj1bdx/airspy-fmradion/pull/71)
  * [Documentation update](https://github.com/jj1bdx/airspy-fmradion/pull/72):
    * Reduce text length of README.md.
    * Old README.md is now located at [`doc/old-README-until-2023.md`](doc/old-README-until-2023.md).
  * [For PortAudio, the minimum output latency is explicitly set to 40 milliseconds.](https://github.com/jj1bdx/airspy-fmradion/pull/73)
  * [Use libsndfile MP3 output capability to generate the MP3 file directly as the audio output, when supported.](https://github.com/jj1bdx/airspy-fmradion/pull/74)
    * libsndfile 1.1 or later is required for the MP3 support.
    * A conditional compilation flag `LIBSNDFILE_MP3_ENABLED`, set by cmake, is introduced.
    * See [`libsndfile.md`](libsndfile.md) for how to installing the latest libsndfile, suggested for Ubuntu 22.04.4 LTS.
    * [See also the related GitHub issue.](https://github.com/jj1bdx/airspy-fmradion/issues/47)
  * Apply [cmake-format](https://github.com/cheshirekow/cmake_format) for `CMakeLists.txt`.
    * Default style: `.cmake-format.py`
* 20240107-0: Made the following changes:
  * For broadcasting FM, show stereo 19kHz pilot signal level when detected.
  * Remove displaying whether FM stereo pilot signal level is stable or unstable.
  * Add Git info into the binary program built, with [cmake-git-version-tracking](https://github.com/andrew-hardin/cmake-git-version-tracking.git) (using jj1bdx's fork).
  * Add compile command database support on CMakeLists.txt.
  * Cleaned up old documents.
  * Fixed the following bugs detected by clang-tidy:
    * [ERR34-C. Detect errors when converting a string to a number](https://wiki.sei.cmu.edu/confluence/display/c/ERR34-C.+Detect+errors+when+converting+a+string+to+a+number)
      * Use `Utility::parse_int()` instead of raw `atoi()`
    * [DCL51-CPP. Do not declare or define a reserved identifier](https://wiki.sei.cmu.edu/confluence/display/cplusplus/DCL51-CPP.+Do+not+declare+or+define+a+reserved+identifier)
      * Remove unused `_FILE_OFFSET_BITS`
  * Fixed the bug of FileSource playback: the code did not terminate after the end of playback.
    * main.cpp: add checking pull_end_reached() in the main loop.
  * Set RtlSdrSource's default_block_length from 65536 to 16384, to prevent popping cracking sound (observed on Mac mini 2023).
  * stat_rate calculation is redesigned by observation of actual SDR units (:i.e., Airspy HF+, Airspy R2, and RTL-SDR).
* 20231227-0: Made the following changes:
  * Split class PilotPhaseLock from FmDecode.
  * Removed submodule readerwriterqueue.
  * Re-introduced DataBuffer from commit <https://github.com/jj1bdx/airspy-fmradion/commit/49faddbae1354bcb7bfcd2b24db458b770273cb5>.
  * PhaseDiscriminator now contains NaN-removal code.
  * Introduced accurate m_pilot_level computation for PilotPhaseLock.
  * Introduced enum PilotState and the state machine for more precisely showing stereo pilot signal detection and the signal levels.
  * Removed buffer option `-b` and `--buffer` finally.
* 20231216-0: Removed recording buffer thread. This will simplify the audio output operation. Also, lowered the output level of AM/CW/USB/LSB/WSPR decoder to prevent audio clipping, and changed the IF AGC constants for longer transition timing.
* 20231215-0: Fix the following known bugs and refactor the code to streamline the functioning:
  * Bug: a hung process during the startup period before valid audio signals are coming out
  * Bug: displaying `-nan` in the output level meter in broadcast FM and NBFM
    * The NaN is presumably generated by volk_32fc_s32f_atan2_32f() in PhaseDiscriminator::process()
    * This NaN issue was presumably the root cause of the multipath filter anomaly first fixed in 20231213-1
  * Enhancement: streamlining processing flow in the main for loop of `main()`
  * Enhancement: removing the initial waiting period for startup; the output is now activated from the block number 1
  * Utility addition: adding `Utility::remove_nans()`, a function to check and substitute NaNs and infinity values in IQSamplesDecodedVector
* 20231213-1: Fixed a NaN issue caused by 0+0j (true zero) output of the multipath filter; the true zero output now causes resetting the filter. This is presumably also one of the reasons that caused the audio disruption issue in 20231212-1 and 20231213-0.
* 20231213-0: Fixed an uninitialized variable `m_save_phase` in PhaseDiscriminator as in [the pull request](https://github.com/jj1bdx/airspy-fmradion/pull/43) by Clayton Smith.
* 20231212-1: FAILED: tried to make API compatible with [VOLK 3.1.0 change for s32fc functions](https://github.com/gnuradio/volk/pull/695), for `volk_32fc_x2_s32fc_multiply_conjugate_add_32fc()`, but this didn't work on Ubuntu 22.04.3.
* 20231212-0: Updated r8-brain-free-src to Version 6.5.
* 20230923: failed changes: low latency setting for buffering-based PortAudio didn't work well. Discarded changes of 20230910-1 to 20230910-4 from the dev branch.
* 20230910-0: Updated r8brain-free-src to Version 6.4.
* 20230528-2: DataBuffer class is reimplemented as a wrapper of `moodycamel::BlockReaderWriterQueue`, which allows efficient blocking operation and removes the requirements of busy waiting by using `moodycamel::BlockReaderWriterQueue::wait_dequeue()`.
* 20230528-1: DataBuffer class is now implemented as a wrapper of `moodycamel::ReaderWriterQueue` class in <https://github.com/cameron314/readerwriterqueue>. All lock-based synchronization functions from DataBuffer class are removed because they are no longer necessary. The repository readerwriterqueue is added as a git submodule. Also, sample length count is removed from the DataBuffer class because of their rare usage.
* 20230528-1: All DataBuffer queue length measurement code in main.cpp are bundled under a compilation flag `DATABUFFER_QUEUE_MONITOR`, which is not necessary for the production code. The actual maximum queue length measured in Mac mini 2018 executions are less than 10, even when the output glitch occurs due to a higher-priority process invocation, such as a web browser. The new DataBuffer class sets the default allocated queue length to 128.
* 20230526-0: Explicitly skip IF Resampler in class FmDecoder to reduce CPU usage for typical settings (i.e., IF sample rate is set to 384 ksamples/sec for Airspy HF+).
* 20230430-0: Forcefully set the coefficient of the reference point of FM multipath filter to 1 + 0j (unity). This may change how the filter behaves. Field testing since 20230214-test shows no notable anomalies.
* 20221215-1: Updated r8brain-free-src to Version 6.2.
* 20221215-0: Fixed AF and IF AGC anomaly when the current gain becomes NaN/Inf. Set workaround by adding a small value (1e-9) for log10() calculation generating the output value.
* 20220911-0: Refactored status message calculation, tested with libvolk 2.5.2.
* 20220903-0: Refactored include/DataBuffer.h for streamlining handling locks and mutexes, using C++17 std::scoped\_lock.
* 20220819-1: Restricted RTL-SDR sampling rate to [900001, 3200000] [Hz]. Also the default IF sample rate of RTL-SDR is set to 1152000Hz. AudioResampler and IfResampler maximum input length check is implemented.
* 20220819-0: /4 downsampling above 3.1MHz/3100kHz in 20210702-0 has been removed. The new IF resampler based on r8brain-free-src works well without preresampling.
* 20220818-1: Added r8brain-free-src options for gaining performance.
* 20220818-0: Implemented r8brain-free-src also for IfResampler. libsamplerate is removed. [r8brain-free-src](https://github.com/avaneev/r8brain-free-src) is used instead of libsoxr. r8brain-free-src is a sample rate converter designed by Aleksey Vaneev of Voxengo.
* 20220817-1: Introduced r8brain-free-src for AudioResampler.
* 20220817-0: Introduced libsamplerate aka Secret Rabbit Code for IfResampler.
* 20220810-1: IF AGC max gain for FM is raised to 10^5, AM/DSB/USB/LSB/WSPR/CW is raised to 10^6.
* 20220810-0: AGC algorithms are refactored.
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
* Obsoleted: Since 20210702-0, if IF rate is 3.1MHz (3100kHz) or larger, a decimation LPF of +-400kHz width by decimation ratio 4 is inserted after the Fs/4 shifter to reduce the ratio of fractional resampler for increasing the output stability to prevent FM stereo PLL unlocking. This function increases the CPU usage, so for a lower CPU usage use a lower sampling rate.
* Since 20210702-0, halfband filter kernel for Airspy R2/Mini is no longer used.
* FM Pilot PLL threshold level has been lowered from 0.01 to 0.001 since 20210607-0, for preventing unwanted unlocking.
* The 2nd-order LPF of FM Pilot PLL had been applied twice since 20210116-0 to 20210427-0, but rolled back to once (as in original SoftFM) since 20210607-0.
* Since 20210427-0, C++17 is required (instead of previous C++11). Modern compilers of Raspberry Pi OS, Ubuntu, and macOS all do support C++17 extensions.
* FM ppm display shows ppb (0.001ppm) digits since 20210206-0.
* Timestamp file format has been changed since 20201204-0.
* PortAudio is required since Version 20201023-0. Use PortAudio v19. Former ALSA output driver is replaced by more versatile PortAudio driver, which is compatible both for Linux and macOS.
* libvolk is required since v0.8.0. If you don't want to install libvolk, use v0.7.8 instead. Use the latest master branch of libvolk. Configure the `volk_config` file with `volk_profile -b` for the maximum performance. See [INSTALL-latest-libvolk.md](INSTALL-latest-libvolk.md) for the details.
* v0.8.5 and the earlier versions set the compilation flag of `-ffast-math`, which disabled the processing of NaN. This will cause a latch-up bug when the multipath filter coefficients diverge. Removed `-ffast-math` for the stable operation.
* v0.9.0-test1 to v0.9.5 had calculation error due to `volk_32f_expfast_32f()` in `IfAgc::process()` method. Fixed this by replacing to the more accurate calculation code of `volk_32f_exp_32f()`.

## No more semantic versioning

* Current version number scheme: YYYYMMDD-N (N: subnumber, starting from 0, unsigned integer)
* The semantic versioning scheme of airspy-fmradion has utterly failed.

## FYI: libusb-1.0.25 glitch

* Note: This problem has been fixed by the latest implementation of Airspy HF+ driver after [this commit](https://github.com/airspy/airspyhf/commit/3b823ad8fa729358e0729e6c1ca60ac5dfcd656e).
* The author has noticed [libusb-1.0.25 on macOS 12.2 causes segfault when stopping the code with SIGINT or SIGTERM with Airspy HF+ Discovery](https://github.com/jj1bdx/airspy-fmradion/issues/35).
* A proper fix of this is to [fix the Airspy HF+ driver](https://github.com/airspy/airspyhf/pull/31).
* [A similar case of SDR++ with ArchLinux](https://github.com/libusb/libusb/issues/1059#issuecomment-1030638617) is also reported.
* Since Version 20220205-0, a workaround is implemented to prevent data loss for this bug: the main() loop closes the audio output before calling the function which might cause this segmentation fault (SIGSEGV), which is the stopping function of the SDR source driver.
* Airspy R2 and Mini are not affected. Use the latest driver with [this fix](https://github.com/airspy/airspyone_host/commit/41c439f16818d931c4d0f8a620413ea5131c0bd6).
* You can still use 20220205-1 if you need to; there is no functional difference between 20220205-1 and 20220206-0.

[End of CHANGES.md]
