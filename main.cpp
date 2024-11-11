// airspy-fmradion - Software decoder for FM broadcast radio with RTL-SDR
//
// Copyright (C) 2013, Joris van Rantwijk.
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2018-2024 Kenji Rikitake, JJ1BDX
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, see http://www.gnu.org/licenses/gpl-2.0.html

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fmt/base.h>
#include <getopt.h>
#include <memory>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#include "AirspyHFSource.h"
#include "AirspySource.h"
#include "AmDecode.h"
#include "AudioOutput.h"
#include "DataBuffer.h"
#include "FileSource.h"
#include "FilterParameters.h"
#include "FineTuner.h"
#include "FmDecode.h"
#include "FourthConverterIQ.h"
#include "MovingAverage.h"
#include "NbfmDecode.h"
#include "RtlSdrSource.h"
#include "SoftFM.h"
#include "Utility.h"
#include "git.h"

// define this for enabling coefficient monitor functions
// #undef COEFF_MONITOR

#define AIRSPY_FMRADION_VERSION "20240424-0"

// Flag to set graceful termination
// in process_signals()
static std::atomic_bool stop_flag(false);

static void usage() {
  std::string s = "";

  s.append("Usage: airspy-fmradion [options]\n");
  s.append("  -m modtype     Modulation type:\n");
  s.append("                   - fm (default)\n");
  s.append("                   - nbfm\n");
  s.append("                   - am\n");
  s.append("                   - dsb\n");
  s.append("                   - usb\n");
  s.append("                   - lsb\n");
  s.append("                   - cw (zeroed-in pitch: 500Hz)\n");
  s.append("                   - wspr (USB 1500Hz +- 100Hz)\n");
  s.append("  -t devtype     Device type:\n");
  s.append("                   - rtlsdr: RTL-SDR devices\n");
  s.append("                   - airspy: Airspy R2\n");
  s.append("                   - airspyhf: Airspy HF+\n");
  s.append("                   - filesource: File Source\n");
  s.append("  -q             Quiet mode\n");
  s.append("  -c config      Comma separated key=value configuration pairs or "
           "just ");
  s.append("key for switches\n");
  s.append("                 See below for valid values per device type\n");
  s.append("  -d devidx      Device index, 'list' to show device list (default "
           "0)\n");
  s.append("  -M             Disable stereo decoding\n");
  s.append("  -R filename    Write audio data as raw S16_LE samples\n");
  s.append("                 use filename '-' to write to stdout\n");
  s.append("  -F filename    Write audio data as raw FLOAT_LE samples\n");
  s.append("                 use filename '-' to write to stdout\n");
  s.append("  -W filename    Write audio data to RF64/WAV S16_LE file\n");
  s.append("                 use filename '-' to write to stdout\n");
  s.append("                 (Pipe is not supported)\n");
  s.append("  -G filename    Write audio data to RF64/WAV FLOAT_LE file\n");
  s.append("                 use filename '-' to write to stdout\n");
  s.append("                 (Pipe is not supported)\n");
#if defined(LIBSNDFILE_MP3_ENABLED)
  s.append("  -C filename    Write audio data to MP3 file\n");
  s.append("                 of VBR -V 1 (experimental)\n");
  s.append("                 use filename '-' to write to stdout\n");
#endif // LIBSNDFILE_MP3_ENABLED
  s.append("  -P device_num  Play audio via PortAudio device index number\n");
  s.append("                 use string '-' to specify the default PortAudio ");
  s.append("device\n");
  s.append("  -T filename    Write pulse-per-second timestamps\n");
  s.append("                 use filename '-' to write to stdout\n");
  s.append("  -X             Shift pilot phase (for Quadrature Multipath "
           "Monitor)\n");
  s.append("                 (-X is ignored under mono mode (-M))\n");
  s.append(
      "  -U             Set deemphasis to 75 microseconds (default: 50)\n");
  s.append("  -f filtername  Filter type:\n");
  s.append("                 For FM:\n");
  s.append("                   - wide: same as default\n");
  s.append("                   - default: none after conversion\n");
  s.append("                   - medium:  +-156kHz\n");
  s.append("                   - narrow:  +-121kHz\n");
  s.append("                 For AM:\n");
  s.append("                   - wide: +-9kHz\n");
  s.append("                   - default: +-6kHz\n");
  s.append("                   - medium:  +-4.5kHz\n");
  s.append("                   - narrow:  +-3kHz\n");
  s.append("                 For NBFM:\n");
  s.append("                   - wide: +-20kHz, with +-17kHz deviation\n");
  s.append("                   - default: +-10kHz\n");
  s.append("                   - medium:  +-8kHz\n");
  s.append("                   - narrow:  +-6.25kHz\n");
  s.append(
      "  -l dB          Set IF squelch level to minus given value of dB\n");
  s.append("  -E stages      Enable multipath filter for FM\n");
  s.append("                 (For stable reception only:\n");
  s.append("                  turn off if reception becomes unstable)\n");
  s.append("  -r ppm         Set IF offset in ppm (range: +-1000000ppm)\n");
  s.append("                 (This option affects output pitch and timing:\n");
  s.append("                  use for the output timing compensation only!)\n");
  s.append("\n");
  s.append("Configuration options for RTL-SDR devices\n");
  s.append("  freq=<int>     Frequency of radio station in Hz (default "
           "100000000)\n");
  s.append(
      "                 valid values: 10M to 2.2G (working range depends on ");
  s.append("device)\n");
  s.append("  srate=<int>    IF sample rate in Hz (default 1152000)\n");
  s.append("                 (valid ranges: [900001, 3200000]))\n");
  s.append("  gain=<float>   Set LNA gain in dB, or 'auto',\n");
  s.append("                 or 'list' to just get a list of valid values "
           "(default ");
  s.append("auto)\n");
  s.append(
      "  blklen=<int>   Set audio buffer size in seconds (default RTL-SDR ");
  s.append("default)\n");
  s.append("  agc            Enable RTL AGC mode (default disabled)\n");
  s.append("  antbias        Enable antenna bias (default disabled)\n");
  s.append("\n");
  s.append("Configuration options for Airspy devices:\n");
  s.append("  freq=<int>     Frequency of radio station in Hz (default "
           "100000000)\n");
  s.append("                 valid values: 24M to 1.8G\n");
  s.append(
      "  srate=<int>    IF sample rate in Hz. Depends on Airspy firmware and ");
  s.append("libairspy support\n");
  s.append(
      "                 Airspy firmware and library must support dynamic ");
  s.append("sample rate query. (default 10000000)\n");
  s.append(
      "  lgain=<int>    LNA gain in dB. 'list' to just get a list of valid ");
  s.append("values: (default 8)\n");
  s.append(
      "  mgain=<int>    Mixer gain in dB. 'list' to just get a list of valid ");
  s.append("values: (default 8)\n");
  s.append(
      "  vgain=<int>    VGA gain in dB. 'list' to just get a list of valid ");
  s.append("values: (default 8)\n");
  s.append("  antbias        Enable antenna bias (default disabled)\n");
  s.append("  lagc           Enable LNA AGC (default disabled)\n");
  s.append("  magc           Enable mixer AGC (default disabled)\n");
  s.append("\n");
  s.append("Configuration options for Airspy HF devices:\n");
  s.append("  freq=<int>     Frequency of radio station in Hz (default "
           "100000000)\n");
  s.append("                 valid values: 192k to 31M, and 60M to 260M\n");
  s.append("  srate=<int>    IF sample rate in Hz.\n");
  s.append("                 Depends on Airspy HF firmware and libairspyhf "
           "support\n");
  s.append(
      "                 Airspy HF firmware and library must support dynamic\n");
  s.append("                 sample rate query. (default 384000)\n");
  s.append("  hf_att=<int>   HF attenuation level and AGC control\n");
  s.append("                 0: enable AGC, no attenuation\n");
  s.append("                 1 ~ 8: disable AGC, apply attenuation of value * "
           "6dB\n");
  s.append("\n");
  s.append("Configuration options for (experimental) FileSource devices:\n");
  s.append("  freq=<int>        Frequency of radio station in Hz\n");
  s.append("  srate=<int>       IF sample rate in Hz.\n");
  s.append("  filename=<string> Source file name.\n");
  s.append("                    Supported encodings: FLOAT, S24_LE, S16_LE\n");
  s.append("  zero_offset       Set if the source file is in zero offset,\n");
  s.append("                    which requires Fs/4 IF shifting.\n");
  s.append("  blklen=<int>      Set block length in samples.\n");
  s.append("  raw               Set if the file is raw binary.\n");
  s.append(
      "  format=<string>   Set the file format for the raw binary file.\n");
  s.append(
      "                    (formats: U8_LE, S8_LE, S16_LE, S24_LE, FLOAT)\n");
  s.append("\n");

  fmt::print(stderr, "{}", s);
}

static void badarg(const char *label) {
  usage();
  fmt::println(stderr, "ERROR: Invalid argument for {}", label);
  exit(1);
}

static bool get_device(std::vector<std::string> &devnames, DevType devtype,
                       Source **srcsdr, int devidx) {
  // Get device names.
  switch (devtype) {
  case DevType::RTLSDR:
    RtlSdrSource::get_device_names(devnames);
    break;
  case DevType::Airspy:
    AirspySource::get_device_names(devnames);
    break;
  case DevType::AirspyHF:
    AirspyHFSource::get_device_names(devnames);
    break;
  case DevType::FileSource:
    FileSource::get_device_names(devnames);
    break;
  }

  if (devidx < 0 || (unsigned int)devidx >= devnames.size()) {
    if (devidx != -1) {
      fmt::println(stderr, "ERROR: invalid device index {}", devidx);
    }

    fmt::println(stderr, "Found {} devices:", (unsigned int)devnames.size());

    for (unsigned int i = 0; i < devnames.size(); i++) {
      fmt::println(stderr, "{:2}: {}", i, devnames[i]);
    }

    return false;
  }

  fmt::println(stderr, "using device {}: {}", devidx, devnames[devidx]);

  // Open receiver devices.
  switch (devtype) {
  case DevType::RTLSDR:
    *srcsdr = new RtlSdrSource(devidx);
    break;
  case DevType::Airspy:
    *srcsdr = new AirspySource(devidx);
    break;
  case DevType::AirspyHF:
    *srcsdr = new AirspyHFSource(devidx);
    break;
  case DevType::FileSource:
    *srcsdr = new FileSource(devidx);
    break;
  }

  return true;
}

// Signal masks to let these signals handled by a dedicated thread.
static sigset_t old_signalmask, signalmask;

// Signal handling thread code, started from main().
// See APUE 3rd Figure 12.16.
static void *process_signals(void *arg) {
  int err, signum;
  for (;;) {
    // wait for a signal
    err = sigwait(&signalmask, &signum);
    if (err != 0) {
      fmt::println(stderr, "ERROR: sigwait failed, ({})", strerror(err));
      exit(1);
    }
    switch (signum) {
    case SIGINT:
    case SIGQUIT:
    case SIGTERM:
      stop_flag.store(true);
      psignal(signum, "\nStopping by getting signal");
      break;
    default:
      psignal(signum, "\nERROR: unexpected signal");
      exit(1);
    }
  }
}

// Main program.

int main(int argc, char **argv) {

  int devidx = 0;
  int pcmrate = FmDecoder::sample_rate_pcm;
  bool stereo = true;
  OutputMode outmode = OutputMode::RAW_INT16;
  std::string filename("-");
  int portaudiodev = -1;
  bool quietmode = false;
  std::string ppsfilename;
  FILE *ppsfile = nullptr;
  bool enable_squelch = false;
  double squelch_level_db = 150.0;
  bool pilot_shift = false;
  bool deemphasis_na = false;
  int multipathfilter_stages = 0;
  bool ifrate_offset_enable = false;
  double ifrate_offset_ppm = 0;
  std::string config_str;
  std::string devtype_str;
  DevType devtype;
  std::string modtype_str("fm");
  ModType modtype = ModType::FM;
  std::string filtertype_str("default");
  FilterType filtertype = FilterType::Default;
  std::vector<std::string> devnames;
  Source *srcsdr = 0;
  int err;
  pthread_t sigmask_thread_id;

  // Perform signal mask on SIGINT, SIGQUIT, and SIGTERM.
  // See APUE 3rd Figure 12.16.
  sigemptyset(&signalmask);
  sigaddset(&signalmask, SIGINT);
  sigaddset(&signalmask, SIGQUIT);
  sigaddset(&signalmask, SIGTERM);
  if ((err = pthread_sigmask(SIG_BLOCK, &signalmask, &old_signalmask)) != 0) {
    fmt::println(stderr, "ERROR: can not mask signals ({})", strerror(err));
    exit(1);
  }
  // Start thread to catch the masked signals.
  err = pthread_create(&sigmask_thread_id, NULL, process_signals, 0);
  if (err != 0) {
    fmt::println(stderr,
                 "ERROR: unable to create pthread of process_signals({})",
                 strerror(err));
    exit(1);
  }

  // Print starting messages.
  fmt::println(stderr, "airspy-fmradion 20240424-0");
  fmt::print(stderr, "Software FM/AM radio for ");
  fmt::println(stderr, "Airspy R2, Airspy HF+, and RTL-SDR");
  if (git::IsPopulated()) {
    fmt::print(stderr, "Git Commit SHA1: {:.{}}", git::CommitSHA1().data(),
               static_cast<int>(git::CommitSHA1().length()));
    if (git::AnyUncommittedChanges()) {
      fmt::print(stderr, " with uncommitted changes");
    }
    fmt::println(stderr, "");
    fmt::println(stderr, "Git branch: {:.{}}", git::Branch().data(),
                 static_cast<int>(git::Branch().length()));
  } else {
    fmt::println(stderr, "Git commit unknown");
  }
  fmt::println(stderr, "VOLK Version = {}.{}.{}", VOLK_VERSION_MAJOR,
               VOLK_VERSION_MINOR, VOLK_VERSION_MAINT);
#if defined(LIBSNDFILE_MP3_ENABLED)
  fmt::println(stderr, "libsndfile MP3 support enabled");
#endif // LIBSNDFILE_MP3_ENABLED

  const struct option longopts[] = {
      {"modtype", optional_argument, nullptr, 'm'},
      {"devtype", optional_argument, nullptr, 't'},
      {"quiet", required_argument, nullptr, 'q'},
      {"config", optional_argument, nullptr, 'c'},
      {"dev", required_argument, nullptr, 'd'},
      {"mono", no_argument, nullptr, 'M'},
      {"raw", required_argument, nullptr, 'R'},
      {"float", required_argument, nullptr, 'F'},
      {"wav", required_argument, nullptr, 'W'},
      {"wavfloat", required_argument, nullptr, 'G'},
      {"play", optional_argument, nullptr, 'P'},
      {"pps", required_argument, nullptr, 'T'},
      {"pilotshift", no_argument, nullptr, 'X'},
      {"usa", no_argument, nullptr, 'U'},
      {"filtertype", optional_argument, nullptr, 'f'},
      {"squelch", required_argument, nullptr, 'l'},
      {"multipathfilter", required_argument, nullptr, 'E'},
      {"ifrateppm", optional_argument, nullptr, 'r'},
#if defined(LIBSNDFILE_MP3_ENABLED)
      {"mp3fmaudio", required_argument, nullptr, 'C'},
#endif // LIBSNDFILE_MP3_ENABLED
      {nullptr, no_argument, nullptr, 0}};

  int c, longindex;

#if defined(LIBSNDFILE_MP3_ENABLED)
  const char *optstring = "m:t:c:d:MR:F:W:G:f:l:P:T:qXUE:r:C:";
#else  // !LIBSNDFILE_MP3_ENABLED
  const char *optstring = "m:t:c:d:MR:F:W:G:f:l:P:T:qXUE:r:";
#endif // LIBSNDFILE_MP3_ENABLED

  while ((c = getopt_long(argc, argv, optstring, longopts, &longindex)) >= 0) {
    switch (c) {
    case 'm':
      modtype_str.assign(optarg);
      break;
    case 't':
      devtype_str.assign(optarg);
      break;
    case 'c':
      config_str.assign(optarg);
      break;
    case 'd':
      if (!Utility::parse_int(optarg, devidx)) {
        devidx = -1;
      }
      break;
    case 'M':
      stereo = false;
      break;
    case 'R':
      outmode = OutputMode::RAW_INT16;
      filename = optarg;
      break;
    case 'F':
      outmode = OutputMode::RAW_FLOAT32;
      filename = optarg;
      break;
    case 'W':
      outmode = OutputMode::WAV_INT16;
      filename = optarg;
      break;
    case 'G':
      outmode = OutputMode::WAV_FLOAT32;
      filename = optarg;
      break;
    case 'f':
      filtertype_str.assign(optarg);
      break;
    case 'l':
      if (!Utility::parse_dbl(optarg, squelch_level_db) ||
          squelch_level_db < 0) {
        badarg("-l");
      }
      enable_squelch = true;
      break;
    case 'P':
      outmode = OutputMode::PORTAUDIO;
      if (0 == strncmp(optarg, "-", 1)) {
        portaudiodev = -1;
      } else if (!Utility::parse_int(optarg, portaudiodev) ||
                 portaudiodev < 0) {
        badarg("-P");
      }
      break;
    case 'T':
      ppsfilename = optarg;
      break;
    case 'q':
      quietmode = true;
      break;
    case 'X':
      pilot_shift = true;
      break;
    case 'U':
      deemphasis_na = true;
      break;
    case 'E':
      if (!Utility::parse_int(optarg, multipathfilter_stages) ||
          multipathfilter_stages < 1) {
        badarg("-E");
      }
      break;
    case 'r':
      ifrate_offset_enable = true;
      if (!Utility::parse_dbl(optarg, ifrate_offset_ppm) ||
          std::fabs(ifrate_offset_ppm) > 1000000.0) {
        badarg("-r");
      }
      break;
#if defined(LIBSNDFILE_MP3_ENABLED)
    case 'C':
      outmode = OutputMode::MP3_FMAUDIO;
      filename = optarg;
      break;
#endif // LIBSNDFILE_MP3_ENABLED
    default:
      usage();
      fmt::println(stderr, "ERROR: Invalid command line options");
      exit(1);
    }
  }

  if (optind < argc) {
    usage();
    fmt::println(stderr, "ERROR: Unexpected command line options");
    exit(1);
  }

  double squelch_level;
  if (enable_squelch) {
    squelch_level = pow(10.0, -(squelch_level_db / 20.0));
  } else {
    squelch_level = 0;
  }

  if (strcasecmp(devtype_str.c_str(), "rtlsdr") == 0) {
    devtype = DevType::RTLSDR;
  } else if (strcasecmp(devtype_str.c_str(), "airspy") == 0) {
    devtype = DevType::Airspy;
  } else if (strcasecmp(devtype_str.c_str(), "airspyhf") == 0) {
    devtype = DevType::AirspyHF;
  } else if (strcasecmp(devtype_str.c_str(), "filesource") == 0) {
    devtype = DevType::FileSource;
  } else {
    fmt::println(
        stderr,
        "ERROR: wrong device type (-t option) must be one of the following:");
    fmt::println(stderr, "        rtlsdr, airspy, airspyhf, filesource");
    exit(1);
  }

  if (strcasecmp(modtype_str.c_str(), "fm") == 0) {
    modtype = ModType::FM;
  } else if (strcasecmp(modtype_str.c_str(), "nbfm") == 0) {
    modtype = ModType::NBFM;
    stereo = false;
  } else if (strcasecmp(modtype_str.c_str(), "am") == 0) {
    modtype = ModType::AM;
    stereo = false;
  } else if (strcasecmp(modtype_str.c_str(), "dsb") == 0) {
    modtype = ModType::DSB;
    stereo = false;
  } else if (strcasecmp(modtype_str.c_str(), "usb") == 0) {
    modtype = ModType::USB;
    stereo = false;
  } else if (strcasecmp(modtype_str.c_str(), "lsb") == 0) {
    modtype = ModType::LSB;
    stereo = false;
  } else if (strcasecmp(modtype_str.c_str(), "cw") == 0) {
    modtype = ModType::CW;
    stereo = false;
  } else if (strcasecmp(modtype_str.c_str(), "wspr") == 0) {
    modtype = ModType::WSPR;
    stereo = false;
  } else {
    fmt::println(stderr, "Modulation type string unsuppored");
    exit(1);
  }

  if (strcasecmp(filtertype_str.c_str(), "default") == 0) {
    filtertype = FilterType::Default;
  } else if (strcasecmp(filtertype_str.c_str(), "medium") == 0) {
    filtertype = FilterType::Medium;
  } else if (strcasecmp(filtertype_str.c_str(), "narrow") == 0) {
    filtertype = FilterType::Narrow;
  } else if (strcasecmp(filtertype_str.c_str(), "wide") == 0) {
    filtertype = FilterType::Wide;
  } else {
    fmt::println(stderr, "Filter type string unsuppored");
    exit(1);
  }

  // Open PPS file.
  if (!ppsfilename.empty()) {
    if (ppsfilename == "-") {
      fmt::println(stderr, "writing pulse-per-second markers to stdout");
      ppsfile = stdout;
    } else {
      fmt::println(stderr, "writing pulse-per-second markers to '{}'",
                   ppsfilename);
      ppsfile = fopen(ppsfilename.c_str(), "w");

      if (ppsfile == nullptr) {
        fmt::println(stderr, "ERROR: can not open '{}' ({})", ppsfilename,
                     strerror(errno));
        exit(1);
      }
    }

    switch (modtype) {
    case ModType::FM:
      fmt::println(ppsfile, "# pps_index sample_index unix_time if_level");
      break;
    case ModType::NBFM:
    case ModType::AM:
    case ModType::DSB:
    case ModType::USB:
    case ModType::LSB:
    case ModType::CW:
    case ModType::WSPR:
      fmt::println(ppsfile, "# block unix_time if_level");
      break;
    }
    fflush(ppsfile);
  }

  // Prepare output writer.
  std::unique_ptr<AudioOutput> audio_output;

  // Set output device first, then print the configuration to stderr.
  switch (outmode) {
  case OutputMode::RAW_INT16:
    audio_output.reset(
        new SndfileOutput(filename, pcmrate, stereo,
                          SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE));
    fmt::println(
        stderr,
        "writing raw 16-bit integer little-endian audio samples to '{}'",
        filename);
    break;
  case OutputMode::RAW_FLOAT32:
    audio_output.reset(
        new SndfileOutput(filename, pcmrate, stereo,
                          SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE));
    fmt::println(stderr,
                 "writing raw 32-bit float little-endian audio samples to '{}'",
                 filename);
    break;
  case OutputMode::WAV_INT16:
    audio_output.reset(new SndfileOutput(filename, pcmrate, stereo,
                                         SF_FORMAT_RF64 | SF_FORMAT_PCM_16 |
                                             SF_ENDIAN_LITTLE));
    fmt::println(stderr, "writing RF64/WAV int16 audio samples to '{}'",
                 filename);
    break;
  case OutputMode::WAV_FLOAT32:
    audio_output.reset(
        new SndfileOutput(filename, pcmrate, stereo,
                          SF_FORMAT_RF64 | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE));
    fmt::println(stderr, "writing RF64/WAV float32 audio samples to '{}'",
                 filename);
    break;
  case OutputMode::PORTAUDIO:
    audio_output.reset(new PortAudioOutput(portaudiodev, pcmrate, stereo));
    if (portaudiodev == -1) {
      fmt::print(stderr, "playing audio to PortAudio default device: ");
    } else {
      fmt::print(stderr,
                 "playing audio to PortAudio device {}: ", portaudiodev);
    }
    fmt::println(stderr, "name '{}'", audio_output->get_device_name());
    break;
#if defined(LIBSNDFILE_MP3_ENABLED)
  case OutputMode::MP3_FMAUDIO:
    audio_output.reset(new SndfileOutput(
        filename, pcmrate, stereo, SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III));
    fmt::println(stderr, "writing MP3 FM-broadcast audio samples to '{}'",
                 filename);
    break;
#endif // LIBSNDFILE_MP3_ENABLED
  }

  if (!(*audio_output)) {
    fmt::println(stderr, "ERROR: AudioOutput: {}", audio_output->error());
    exit(1);
  }

  if (!get_device(devnames, devtype, &srcsdr, devidx)) {
    exit(1);
  }

  if (!(*srcsdr)) {
    fmt::println(stderr, "ERROR source: {}", srcsdr->error());
    delete srcsdr;
    exit(1);
  }

  // Configure device and start streaming.
  if (!srcsdr->configure(config_str)) {
    fmt::println(stderr, "ERROR: configuration: {}", srcsdr->error());
    delete srcsdr;
    exit(1);
  }

  double freq = srcsdr->get_configured_frequency();
  fmt::print(stderr, "tuned for {:.7g} [MHz]", freq * 1.0e-6);
  double tuner_freq = srcsdr->get_frequency();
  if (tuner_freq != freq) {
    fmt::print(stderr, ", device tuned for {:.7g} [MHz]", tuner_freq * 1.0e-6);
  }
  fmt::println(stderr, "");

  double ifrate = srcsdr->get_sample_rate();

  unsigned int if_blocksize;

  bool enable_fs_fourth_downconverter = !(srcsdr->is_low_if());

  bool enable_downsampling = true;
  double if_decimation_ratio = 1.0;
  double fm_target_rate = FmDecoder::sample_rate_if;
  double am_target_rate = AmDecoder::internal_rate_pcm;
  double nbfm_target_rate = NbfmDecoder::internal_rate_pcm;

  // Configure blocksize.
  switch (devtype) {
  case DevType::Airspy:
    if_blocksize = 65536;
    break;
  case DevType::AirspyHF:
    // Common settings.
    if_blocksize = 2048;
    break;
  case DevType::RTLSDR:
    if_blocksize = 16384;
    break;
  case DevType::FileSource:
    if_blocksize = 2048;
    break;
  }

  // Status refresh rate.
  // TODO: ~0.1sec / display (should be tuned)
  unsigned int stat_rate =
      (unsigned int)((double)ifrate / (double)if_blocksize / 9.0);
  fmt::println(stderr, "stat_rate = {}", stat_rate);

  // IF rate compensation if requested.
  if (ifrate_offset_enable) {
    ifrate *= 1.0 + (ifrate_offset_ppm / 1000000.0);
  }

  // Configure if_decimation_ratio.
  switch (modtype) {
  case ModType::FM:
    if_decimation_ratio = ifrate / fm_target_rate;
    break;
  case ModType::NBFM:
    if_decimation_ratio = ifrate / nbfm_target_rate;
    break;
  case ModType::AM:
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    if_decimation_ratio = ifrate / am_target_rate;
    break;
  }

  // Show decoding modulation type.
  fmt::println(stderr, "Decoding modulation type: {}", modtype_str);
  if (enable_squelch) {
    fmt::println(stderr, "IF Squelch level: {:.9g} [dB]",
                 20 * log10(squelch_level));
  }

  double demodulator_rate = ifrate / if_decimation_ratio;
  double total_decimation_ratio = ifrate / pcmrate;
  double audio_decimation_ratio = demodulator_rate / pcmrate;

  // Display ifrate compensation if applicable.
  if (ifrate_offset_enable) {
    fmt::println(stderr, "IF sample rate shifted by: {:.9g} [ppm]",
                 ifrate_offset_ppm);
  }

  // Display filter configuration.
  fmt::print(stderr, "IF sample rate: {:.9g} [Hz], ", ifrate);
  fmt::println(stderr, "IF decimation: / {:.9g}", if_decimation_ratio);
  fmt::print(stderr, "Demodulator rate: {:.8g} [Hz], ", demodulator_rate);
  fmt::println(stderr, "audio decimation: / {:.9g}", audio_decimation_ratio);

  srcsdr->print_specific_parms();

  // Create source data queue.
  DataBuffer<IQSample> source_buffer;

  // ownership will be transferred to thread therefore the unique_ptr with
  // move is convenient if the pointer is to be shared with the main thread
  // use shared_ptr (and no move) instead
  std::unique_ptr<Source> up_srcsdr(srcsdr);

  // Start reading from device in separate thread.
  up_srcsdr->start(&source_buffer, &stop_flag);

  // Reported by GitHub @bstalk: (!up_srcadr) doesn't work for gcc of Debian.
  if (!(*up_srcsdr)) {
    fmt::println(stderr, "ERROR: source: {}", up_srcsdr->error());
    exit(1);
  }

  // Prevent aliasing at very low output sample rates.
  double deemphasis = deemphasis_na ? FmDecoder::deemphasis_time_na
                                    : FmDecoder::deemphasis_time_eu;

  // Prepare Fs/4 downconverter.
  FourthConverterIQ fourth_downconverter(false);

  IfResampler if_resampler(ifrate,          // input_rate
                           demodulator_rate // output_rate
  );
  enable_downsampling = (ifrate != demodulator_rate);

  IQSampleCoeff amfilter_coeff;
  bool fmfilter_enable;
  IQSampleCoeff fmfilter_coeff;
  IQSampleCoeff nbfmfilter_coeff;

  switch (filtertype) {
  case FilterType::Default:
    amfilter_coeff = FilterParameters::jj1bdx_am_48khz_default;
    fmfilter_enable = false;
    fmfilter_coeff = FilterParameters::delay_3taps_only_iq;
    nbfmfilter_coeff = FilterParameters::jj1bdx_nbfm_48khz_default;
    break;
  case FilterType::Medium:
    amfilter_coeff = FilterParameters::jj1bdx_am_48khz_medium;
    fmfilter_enable = true;
    fmfilter_coeff = FilterParameters::jj1bdx_fm_384kHz_medium;
    nbfmfilter_coeff = FilterParameters::jj1bdx_nbfm_48khz_medium;
    break;
  case FilterType::Narrow:
    amfilter_coeff = FilterParameters::jj1bdx_am_48khz_narrow;
    fmfilter_enable = true;
    fmfilter_coeff = FilterParameters::jj1bdx_fm_384kHz_narrow;
    nbfmfilter_coeff = FilterParameters::jj1bdx_nbfm_48khz_narrow;
    break;
  case FilterType::Wide:
    amfilter_coeff = FilterParameters::jj1bdx_am_48khz_wide;
    fmfilter_enable = false;
    fmfilter_coeff = FilterParameters::delay_3taps_only_iq;
    nbfmfilter_coeff = FilterParameters::jj1bdx_nbfm_48khz_wide;
    break;
  }

  // Prepare AM decoder.
  AmDecoder am(amfilter_coeff, // amfilter_coeff
               modtype         // mode
  );

  // Prepare FM decoder.
  FmDecoder fm(fmfilter_enable, // fmfilter_enable
               fmfilter_coeff,  // fmfilter_coeff
               stereo,          // stereo
               deemphasis,      // deemphasis,
               pilot_shift,     // pilot_shift
               static_cast<unsigned int>(multipathfilter_stages)
               // multipath_stages
  );

  // Prepare narrow band FM decoder.
  NbfmDecoder nbfm(nbfmfilter_coeff,            // nbfmfilter_coeff
                   NbfmDecoder::freq_dev_normal // freq_dev
  );

  // Initialize moving average object for FM ppm monitoring.
  switch (modtype) {
  case ModType::FM:
  case ModType::NBFM:
    fmt::print(stderr, "audio sample rate: {} [Hz],", pcmrate);
    fmt::println(stderr, " audio bandwidth: {} [Hz]",
                 (unsigned int)FmDecoder::bandwidth_pcm);
    fmt::println(stderr, "audio totally decimated from IF by: {:.9g}",
                 total_decimation_ratio);
    break;
  case ModType::AM:
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    fmt::println(stderr, "AM demodulator deemphasis: {:.9g} [µs]",
                 AmDecoder::deemphasis_time);
    break;
  }
  if (modtype == ModType::FM) {
    fmt::println(stderr, "FM demodulator deemphasis: {:.9g} [µs]", deemphasis);
    if (multipathfilter_stages > 0) {
      fmt::println(stderr, "FM IF multipath filter enabled, stages: {}",
                   multipathfilter_stages);
    }
  }
  fmt::println(stderr, "Filter type: {}", filtertype_str);

  // Initialize moving average object for FM ppm monitoring.
  const unsigned int ppm_average_stages = 100;
  MovingAverage<float> ppm_average(ppm_average_stages, 0.0f);

  // Initialize moving average object for FM stereo pilot level monitoring.
  const unsigned int pilot_level_average_stages = 10;
  MovingAverage<float> pilot_level_average(pilot_level_average_stages, 0.0f);

  float audio_level = 0;
  double block_time = Utility::get_time();

  float if_level = 0;

  PilotState pilot_status = PilotState::NotDetected;

  ///////////////////////////////////////
  // NOTE: main processing loop from here
  ///////////////////////////////////////
  for (uint64_t block = 0; !stop_flag.load(); block++) {

    // If the end has been reached at the source buffer,
    // exit the main processing loop.
    if (source_buffer.pull_end_reached()) {
      stop_flag.store(true);
      break;
    }

    // Pull next block from source buffer.
    IQSampleVector iqsamples = source_buffer.pull();

    IQSampleVector if_shifted_samples;
    IQSampleVector if_downsampled_samples;
    IQSampleVector if_samples;

    // Initialize audio samples
    SampleVector audiosamples(0);

    // If no IF data is sent,
    // go back and wait again
    if (iqsamples.empty()) {
      // go to the end of the for loop
      continue;
    }

    double prev_block_time = block_time;
    block_time = Utility::get_time();

    // Fine tuning is not needed
    // so long as the stability of the receiver device is
    // within the range of +- 1ppm (~100Hz or less).

    if (enable_fs_fourth_downconverter) {
      // Fs/4 downconvering is required
      // to avoid frequency zero offset
      // because Airspy HF+ and RTL-SDR are Zero IF receivers
      fourth_downconverter.process(iqsamples, if_shifted_samples);
    } else {
      if_shifted_samples = std::move(iqsamples);
    }

    // Downsample IF for the decoder.
    if (enable_downsampling) {
      if_resampler.process(if_shifted_samples, if_samples);
    } else {
      if_samples = std::move(if_shifted_samples);
    }

    // Downsample IF for the decoder.
    size_t if_samples_size = if_samples.size();
    bool if_exists = if_samples_size > 0;
    double if_rms = 0.0;

    if (!if_exists) {
      // go to the end of the for loop
      continue;
    }

    // Valid data exists in if_samples
    // from here in the for loop

    if (modtype == ModType::FM) {
      // the minus factor is to show the ppm correction
      // to make and not the one which has already been made
      ppm_average.feed((fm.get_tuning_offset() / tuner_freq) * -1.0e6);
    } else if (modtype == ModType::NBFM) {
      ppm_average.feed((nbfm.get_tuning_offset() / tuner_freq) * -1.0e6);
    }

    // Add 1e-9 to log10() to prevent generating NaN
    float if_level_db = 20 * log10(if_level + 1e-9);

    // Decode signal from if_samples.
    switch (modtype) {
    case ModType::FM:
      // Decode FM signal.
      fm.process(if_samples, audiosamples);
      if_rms = fm.get_if_rms();
      break;
    case ModType::NBFM:
      // Decode narrow band FM signal.
      nbfm.process(if_samples, audiosamples);
      if_rms = nbfm.get_if_rms();
      break;
    case ModType::AM:
    case ModType::DSB:
    case ModType::USB:
    case ModType::LSB:
    case ModType::CW:
    case ModType::WSPR:
      // Decode AM/DSB/USB/LSB/CW signals.
      am.process(if_samples, audiosamples);
      if_rms = am.get_if_rms();
      break;
    }
    // Measure (unsigned int)the average IF level.
    if_level = 0.75 * if_level + 0.25 * if_rms;

    size_t audiosamples_size = audiosamples.size();
    bool audio_exists = audiosamples_size > 0;

    if (!audio_exists) {
      // go to the end of the for loop
      continue;
    }

    // Valid audio data exists in audiosamples
    // from here in the for loop

    // Measure audio level
    float audio_mean, audio_rms;
    IQSampleDecodedVector audiosamples_float;
    audiosamples_float.resize(audiosamples_size);
    volk_64f_convert_32f(audiosamples_float.data(), audiosamples.data(),
                         audiosamples_size);
    Utility::samples_mean_rms(audiosamples_float, audio_mean, audio_rms);
    audio_level = 0.95 * audio_level + 0.05 * audio_rms;

    // Set nominal audio volume (-6dB) when IF squelch is open,
    // set to zero volume if the squelch is closed.
    Utility::adjust_gain(audiosamples, if_rms >= squelch_level ? 0.5 : 0.0);
    // Write samples to output.
    audio_output->write(std::move(audiosamples));

    // Show status messages for each block if not in quiet mode.
    if (!quietmode) {
      if ((block % stat_rate) == 0) {
        // Stereo detection display
        // Use a state machine here
        if (modtype == ModType::FM) {
          float pilot_level = fm.get_pilot_level();
          pilot_level_average.feed(pilot_level);
          bool stereo_status = fm.stereo_detected();
          switch (pilot_status) {
          case PilotState::NotDetected:
            if (stereo_status) {
              fmt::println(stderr, "\ngot stereo signal");
              pilot_status = PilotState::Detected;
              pilot_level_average.fill(0.0f);
            }
            break;
          case PilotState::Detected:
            if (!stereo_status) {
              fmt::println(stderr, "\nlost stereo signal");
              pilot_status = PilotState::NotDetected;
            }
            break;
          }
        }
        // Show per-block statistics.
        // Add 1e-9 to log10() to prevent generating NaN
        float audio_level_db = 20 * log10(audio_level + 1e-9) + 3.01;

        switch (modtype) {
        case ModType::FM:
          fmt::print(stderr,
                     "\rblk={:11}:ppm={:+7.3f}:IF={:+6.1f}dB:AF={:+6.1f}dB:"
                     "Pilot= {:8.6f}",
                     block, ppm_average.average(), if_level_db, audio_level_db,
                     pilot_level_average.average());
          fflush(stderr);
          break;
        case ModType::NBFM:
          fmt::print(stderr,
                     "\rblk={:11}:ppm={:+7.3f}:IF={:+6.1f}dB:AF={:+6.1f}dB",
                     block, ppm_average.average(), if_level_db, audio_level_db);
          fflush(stderr);
          break;
        case ModType::AM:
        case ModType::DSB:
        case ModType::USB:
        case ModType::LSB:
        case ModType::CW:
        case ModType::WSPR:
          // Show statistics without ppm offset.
          // Add 1e-9 to log10() to prevent generating NaN
          double if_agc_gain_db =
              20 * log10(am.get_if_agc_current_gain() + 1e-9);
          fmt::print(stderr,
                     "\rblk={:11}:IF={:+6.1f}dB:AGC={:+6.1f}dB:AF={:+6.1f}dB",
                     block, if_level_db, if_agc_gain_db, audio_level_db);
          fflush(stderr);
          break;
        }
      }

#ifdef COEFF_MONITOR
      if ((modtype == ModType::FM) && (multipathfilter_stages > 0) &&
          (block % (stat_rate * 10)) == 0) {
        double mf_error = fm.get_multipath_error();
        const MfCoeffVector &mf_coeff = fm.get_multipath_coefficients();
        fmt::print(stderr, "block,{},mf_error,{:.9f},mf_coeff,", block,
                   mf_error);
        for (unsigned int i = 0; i < mf_coeff.size(); i++) {
          MfCoeff val = mf_coeff[i];
          fmt::print(stderr, "{},{:.9f},{:.9f},", i, val.real(), val.imag());
        }
        fmt::print(stderr, "\n");
        fflush(stderr);
      }
#endif
    }

    // Write PPS markers.
    if (ppsfile != nullptr) {
      switch (modtype) {
      case ModType::FM:
        for (const PilotPhaseLock::PpsEvent &ev : fm.get_pps_events()) {
          double ts = prev_block_time;
          ts += ev.block_position * (block_time - prev_block_time);
          fmt::println(ppsfile, "{:>8} {:>14} {:18.6f} {:+9.3f}",
                       std::to_string(ev.pps_index),
                       std::to_string(ev.sample_index), ts, if_level_db);
          fflush(ppsfile);
          // Erase the marked event.
          fm.erase_first_pps_event();
        }
        break;
      case ModType::NBFM:
      case ModType::AM:
      case ModType::DSB:
      case ModType::USB:
      case ModType::LSB:
      case ModType::CW:
      case ModType::WSPR:
        if ((block % (stat_rate * 10)) == 0) {
          fmt::println(ppsfile, "{:11} {:18.6f} {:+9.3f}", block,
                       prev_block_time, if_level_db);
          fflush(ppsfile);
        }
        break;
      }
    }
    /////////////////////////////////////////
    // NOTE: end of main processing loop here
    /////////////////////////////////////////
  }

  // Exit and cleanup
  fmt::println(stderr, "");

  // Close audio output.
  audio_output->output_close();
  // Terminate receiver thread.
  up_srcsdr->stop();

  fmt::println(stderr, "airspy-fmradion terminated");

  // Destructors of the source driver and other objects
  // will perform the proper cleanup.
}

// end
