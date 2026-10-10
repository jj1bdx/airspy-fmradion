// airspy-fmradion - Software decoder for FM broadcast radio with RTL-SDR
//
// Copyright (C) 2013, Joris van Rantwijk.
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2018-2026 Kenji Rikitake, JJ1BDX
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

#include <algorithm>
#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <getopt.h>
#include <memory>
#include <signal.h>
#include <sys/time.h>
#include <unistd.h>

#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif // __APPLE__

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
#include "StatusPrinter.h"
#include "Utility.h"
#include "git.h"

// define this for enabling coefficient monitor functions
// #define COEFF_MONITOR 1

#define AIRSPY_FMRADION_VERSION "20260725-0"

// Flag to set graceful termination
// in process_signals()
static std::atomic_bool stop_flag(false);

static void usage() {
  std::string usage_string =
      "Usage: airspy-fmradion [options]\n"
      "  -m modtype     Modulation type:\n"
      "                   - fm (default)\n"
      "                   - nbfm\n"
      "                   - am\n"
      "                   - dsb\n"
      "                   - usb\n"
      "                   - lsb\n"
      "                   - cw (zeroed-in pitch: 500Hz)\n"
      "                   - wspr (USB 1500Hz +- 100Hz)\n"
      "  -t devtype     Device type:\n"
      "                   - rtlsdr: RTL-SDR devices\n"
      "                   - airspy: Airspy R2\n"
      "                   - airspyhf: Airspy HF+\n"
      "                   - filesource: File Source\n"
      "  -q             Quiet mode\n"
      "  -v             Show output/queue/SDR drop counters on the status "
      "line\n"
      "                 (a summary is printed at exit unless -q is used)\n"
      "  -c config      Comma separated key=value configuration pairs or just "
      "key for switches\n"
      "                 See below for valid values per device type\n"
      "  -d devidx      Device index, 'list' to show device list (default 0)\n"
      "  -M             Disable stereo decoding\n"
      "  -R filename    Write audio data as raw S16_LE samples\n"
      "                 use filename '-' to write to stdout\n"
      "  -F filename    Write audio data as raw FLOAT_LE samples\n"
      "                 use filename '-' to write to stdout\n"
      "  -W filename    Write audio data to RF64/WAV S16_LE file\n"
      "                 use filename '-' to write to stdout\n"
      "                 (Pipe is not supported)\n"
      "  -G filename    Write audio data to RF64/WAV FLOAT_LE file\n"
      "                 use filename '-' to write to stdout\n"
      "                 (Pipe is not supported)\n"
#if defined(LIBSNDFILE_MP3_ENABLED)
      "  -C filename    Write audio data to MP3 file\n"
      "                 of VBR -V 1 (experimental)\n"
      "                 use filename '-' to write to stdout\n"
#endif // LIBSNDFILE_MP3_ENABLED
      "  -P device_num  Play audio via PortAudio device index number\n"
      "                 use string '-' to specify the default PortAudio "
      "device\n"
      "  -L ms          Set PortAudio output suggested latency in "
      "milliseconds\n"
      "                 valid range: 1 to 100\n"
      "                 (default: floored at 40 milliseconds)\n"
      "                 (-L is ignored unless PortAudio output (-P) is "
      "used)\n"
      "  --portaudio-prefill ms\n"
      "                 Write ms of silence into the PortAudio output once,\n"
      "                 before the first audio block (a fixed cushion\n"
      "                 against stalls; adds ms of latency)\n"
      "                 valid range: 0 to 100 (default: 0 = off)\n"
      "                 clamped to the granted capacity minus one audio "
      "block\n"
      "                 (use with -L 8 to -L 10; ignored unless -P is used)\n"
      "  --portaudio-prefill-adapt ms\n"
      "                 On each PortAudio underflow, add 10 ms more silence\n"
      "                 until the total prefill reaches ms (never shrinks)\n"
      "                 valid range: 0 to 100 (default: 0 = off)\n"
      "                 (ignored unless PortAudio output (-P) is used)\n"
      "  -T filename    Write pulse-per-second timestamps\n"
      "                 use filename '-' to write to stdout\n"
      "  -X             Shift pilot phase (for Quadrature Multipath Monitor)\n"
      "                 (-X is ignored under mono mode (-M))\n"
      "  -U             Set deemphasis to 75 microseconds (default: 50)\n"
      "  -f filtername  Filter type:\n"
      "                 For FM:\n"
      "                   - wide: same as default\n"
      "                   - default: none after conversion\n"
      "                   - medium:  +-156kHz\n"
      "                   - narrow:  +-121kHz\n"
      "                 For AM:\n"
      "                   - wide: +-9kHz\n"
      "                   - default: +-6kHz\n"
      "                   - medium:  +-4.5kHz\n"
      "                   - narrow:  +-3kHz\n"
      "                 For NBFM:\n"
      "                   - wide: +-20kHz, with +-17kHz deviation\n"
      "                   - default: +-10kHz\n"
      "                   - medium:  +-8kHz\n"
      "                   - narrow:  +-6.25kHz\n"
      "  -l dB          Set IF squelch level to minus given value of dB\n"
      "  -E stages      Enable multipath filter for FM\n"
      "                 Size this to the echo delay spread\n"
      "                 (For stable reception only:\n"
      "                  turn off if reception becomes unstable)\n"
      "                 The value is between 1 to 1024\n"
      "  -r ppm         Set IF offset in ppm (range: within +-1000000ppm,\n"
      "                 exclusive)\n"
      "                 (This option affects output pitch and timing:\n"
      "                  use for the output timing compensation only!)\n"
      "\n"
      "Configuration options for RTL-SDR devices\n"
      "  freq=<int>     Frequency of radio station in Hz (default 100000000)\n"
      "                 valid values: 10M to 2.2G (working range depends on "
      "device)\n"
      "  srate=<int>    IF sample rate in Hz (default 1152000)\n"
      "                 (valid ranges: [900001, 3200000]))\n"
      "  gain=<float>   Set LNA gain in dB, or 'auto',\n"
      "                 or 'list' to just get a list of valid values (default "
      "auto)\n"
      "  blklen=<int>   Set audio buffer size in seconds (default RTL-SDR "
      "default)\n"
      "  agc            Enable RTL AGC mode (default disabled)\n"
      "  antbias        Enable antenna bias (default disabled)\n"
      "\n"
      "Configuration options for Airspy devices:\n"
      "  freq=<int>     Frequency of radio station in Hz (default 100000000)\n"
      "                 valid values: 24M to 1.8G\n"
      "  srate=<int>    IF sample rate in Hz. Depends on Airspy firmware and "
      "libairspy support\n"
      "                 Airspy firmware and library must support dynamic "
      "sample rate query. (default 10000000)\n"
      "  lgain=<int>    LNA gain in dB. 'list' to just get a list of valid "
      "values: (default 8)\n"
      "  mgain=<int>    Mixer gain in dB. 'list' to just get a list of valid "
      "values: (default 8)\n"
      "  vgain=<int>    VGA gain in dB. 'list' to just get a list of valid "
      "values: (default 8)\n"
      "  antbias        Enable antenna bias (default disabled)\n"
      "  lagc           Enable LNA AGC (default disabled)\n"
      "  magc           Enable mixer AGC (default disabled)\n"
      "\n"
      "Configuration options for Airspy HF devices:\n"
      "  freq=<int>     Frequency of radio station in Hz (default 100000000)\n"
      "                 valid values: 1k to 31M, and 60M to 260M\n"
      "  srate=<int>    IF sample rate in Hz.\n"
      "                 Depends on Airspy HF firmware and libairspyhf support\n"
      "                 Airspy HF firmware and library must support dynamic\n"
      "                 sample rate query. (default 384000)\n"
      "  hf_att=<int>   HF attenuation level and AGC control\n"
      "                 0: enable AGC, no attenuation\n"
      "                 1 ~ 8: disable AGC, apply attenuation of value * 6dB\n"
      "\n"
      "Configuration options for (experimental) FileSource devices:\n"
      "  freq=<int>        Frequency of radio station in Hz\n"
      "  srate=<int>       IF sample rate in Hz.\n"
      "  filename=<string> Source file name.\n"
      "                    Supported encodings: FLOAT, S24_LE, S16_LE\n"
      "  zero_offset       Set if the source file is in zero offset,\n"
      "                    which requires Fs/4 IF shifting.\n"
      "  blklen=<int>      Set block length in samples.\n"
      "  raw               Set if the file is raw binary.\n"
      "  format=<string>   Set the file format for the raw binary file.\n"
      "                    (formats: U8_LE, S8_LE, S16_LE, S24_LE, FLOAT)\n"
      "\n";

  fmt::print(stderr, "{}", usage_string);
}

static void badarg(const char *label) {
  usage();
  fmt::println(stderr, "ERROR: Invalid argument for {}", label);
  exit(1);
}

static bool get_device(std::vector<std::string> &devnames, DevType devtype,
                       std::unique_ptr<Source> &up_srcsdr, int devidx) {
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
    up_srcsdr = std::make_unique<RtlSdrSource>(devidx);
    break;
  case DevType::Airspy:
    up_srcsdr = std::make_unique<AirspySource>(devidx);
    break;
  case DevType::AirspyHF:
    up_srcsdr = std::make_unique<AirspyHFSource>(devidx);
    break;
  case DevType::FileSource:
    up_srcsdr = std::make_unique<FileSource>(devidx);
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
#ifdef __APPLE__
  // This thread only waits for signals; keep it off the high-QoS cores.
  pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif // __APPLE__
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

// Long-only option codes (no short option letter).
enum { OPT_PA_PREFILL = 0x100, OPT_PA_PREFILL_ADAPT };

// Print the one-line exit summary of the loss/underrun counters.
static void print_summary(const AudioOutput &output,
                          const DataBuffer<IQSample> &buffer,
                          const Source &source) {
  fmt::print(stderr, "Summary: ");
  const AudioOutput::Telemetry t = output.telemetry();
  if (t.capacity_frames >= 0 && t.min_free_frames >= 0) {
    fmt::print(
        stderr, "PortAudio underflows={} max ring fill={:.1f}/{:.1f}ms; ",
        t.underflows,
        frames_to_ms(t.capacity_frames - t.min_free_frames, t.sample_rate),
        frames_to_ms(t.capacity_frames, t.sample_rate));
    if (t.prefill_frames > 0) {
      fmt::print(stderr, "prefill={:.1f}ms; ",
                 frames_to_ms(t.prefill_frames, t.sample_rate));
    }
  }
  fmt::println(stderr,
               "DataBuffer queue={} max={} dropped blocks={}; "
               "SDR dropped samples={}",
               buffer.queue_size(), buffer.max_queue_size(),
               buffer.dropped_blocks(), source.dropped_samples());
}

// Main program.

int main(int argc, char **argv) {
#ifdef __APPLE__
  // Before any thread exists, so threads created later inherit the QoS class.
  if (const int qos_err =
          pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0)) {
    fmt::println(stderr, "WARNING: can not set thread QoS ({})",
                 strerror(qos_err));
  }
#endif // __APPLE__

  int devidx = 0;
  int pcmrate = FmDecoder::sample_rate_pcm;
  bool stereo = true;
  OutputMode outmode = OutputMode::RAW_INT16;
  std::string filename("-");
  int portaudiodev = -1;
  // Sentinel: no -L/--portaudio-latency given, use PortAudio's
  // platform-default suggestedLatency (computed inside PortAudioOutput).
  double portaudio_latency_ms = -1.0;
  // PortAudio prefill (--portaudio-prefill) and its adaptive cap
  // (--portaudio-prefill-adapt) in milliseconds; 0 = off.
  double portaudio_prefill_ms = 0.0;
  double portaudio_prefill_adapt_ms = 0.0;
  bool quietmode = false;
  bool verbosemode = false;
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
  // The source is moved into the thread (unique_ptr); to share it with the main
  // thread, use shared_ptr without move.
  std::unique_ptr<Source> up_srcsdr = nullptr;
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
  fmt::println(stderr, "airspy-fmradion Version {}", AIRSPY_FMRADION_VERSION);
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
      {"modtype", required_argument, nullptr, 'm'},
      {"devtype", required_argument, nullptr, 't'},
      {"quiet", no_argument, nullptr, 'q'},
      {"verbose", no_argument, nullptr, 'v'},
      {"config", required_argument, nullptr, 'c'},
      {"dev", required_argument, nullptr, 'd'},
      {"mono", no_argument, nullptr, 'M'},
      {"raw", required_argument, nullptr, 'R'},
      {"float", required_argument, nullptr, 'F'},
      {"wav", required_argument, nullptr, 'W'},
      {"wavfloat", required_argument, nullptr, 'G'},
      {"play", required_argument, nullptr, 'P'},
      {"portaudio-latency", required_argument, nullptr, 'L'},
      {"portaudio-prefill", required_argument, nullptr, OPT_PA_PREFILL},
      {"portaudio-prefill-adapt", required_argument, nullptr,
       OPT_PA_PREFILL_ADAPT},
      {"pps", required_argument, nullptr, 'T'},
      {"pilotshift", no_argument, nullptr, 'X'},
      {"usa", no_argument, nullptr, 'U'},
      {"filtertype", required_argument, nullptr, 'f'},
      {"squelch", required_argument, nullptr, 'l'},
      {"multipathfilter", required_argument, nullptr, 'E'},
      {"ifrateppm", required_argument, nullptr, 'r'},
#if defined(LIBSNDFILE_MP3_ENABLED)
      {"mp3fmaudio", required_argument, nullptr, 'C'},
#endif // LIBSNDFILE_MP3_ENABLED
      {nullptr, no_argument, nullptr, 0}};

  int c, longindex;

#if defined(LIBSNDFILE_MP3_ENABLED)
  const char *optstring = "m:t:c:d:MR:F:W:G:f:l:P:L:T:qvXUE:r:C:";
#else  // !LIBSNDFILE_MP3_ENABLED
  const char *optstring = "m:t:c:d:MR:F:W:G:f:l:P:L:T:qvXUE:r:";
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
    case 'L':
      if (!Utility::parse_dbl(optarg, portaudio_latency_ms) ||
          portaudio_latency_ms < 1.0 || portaudio_latency_ms > 100.0) {
        badarg("-L");
      }
      break;
    case OPT_PA_PREFILL:
      if (!Utility::parse_dbl(optarg, portaudio_prefill_ms) ||
          portaudio_prefill_ms < 0.0 || portaudio_prefill_ms > 100.0) {
        badarg("--portaudio-prefill");
      }
      break;
    case OPT_PA_PREFILL_ADAPT:
      if (!Utility::parse_dbl(optarg, portaudio_prefill_adapt_ms) ||
          portaudio_prefill_adapt_ms < 0.0 ||
          portaudio_prefill_adapt_ms > 100.0) {
        badarg("--portaudio-prefill-adapt");
      }
      break;
    case 'T':
      ppsfilename = optarg;
      break;
    case 'q':
      quietmode = true;
      break;
    case 'v':
      verbosemode = true;
      break;
    case 'X':
      pilot_shift = true;
      break;
    case 'U':
      deemphasis_na = true;
      break;
    case 'E':
      if (!Utility::parse_int(optarg, multipathfilter_stages) ||
          multipathfilter_stages < 1 || multipathfilter_stages > 1024) {
        badarg("-E");
      }
      break;
    case 'r':
      ifrate_offset_enable = true;
      // Reject the degenerate endpoints: -1000000 ppm would multiply
      // the IF rate by exactly zero.
      if (!Utility::parse_dbl(optarg, ifrate_offset_ppm) ||
          std::fabs(ifrate_offset_ppm) >= 1000000.0) {
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
      int pps_fd =
          open(ppsfilename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
      if (pps_fd < 0) {
        fmt::println(stderr, "ERROR: can not open '{}' ({})", ppsfilename,
                     strerror(errno));
        exit(1);
      }
      ppsfile = fdopen(pps_fd, "w");
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

  // Convert the user-supplied PortAudio latency from milliseconds (-L)
  // to seconds (PaTime); -1.0 means "unset", signaling PortAudioOutput
  // to fall back to its platform-default-plus-floor logic.
  PaTime portaudio_suggested_latency =
      (portaudio_latency_ms >= 0.0) ? (portaudio_latency_ms / 1000.0) : -1.0;

  // Set output device first, then print the configuration to stderr.
  switch (outmode) {
  case OutputMode::RAW_INT16:
    audio_output = std::make_unique<SndfileOutput>(
        filename, pcmrate, stereo,
        SF_FORMAT_RAW | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE);
    fmt::println(
        stderr,
        "writing raw 16-bit integer little-endian audio samples to '{}'",
        filename);
    break;
  case OutputMode::RAW_FLOAT32:
    audio_output = std::make_unique<SndfileOutput>(
        filename, pcmrate, stereo,
        SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE);
    fmt::println(stderr,
                 "writing raw 32-bit float little-endian audio samples to '{}'",
                 filename);
    break;
  case OutputMode::WAV_INT16:
    audio_output = std::make_unique<SndfileOutput>(
        filename, pcmrate, stereo,
        SF_FORMAT_RF64 | SF_FORMAT_PCM_16 | SF_ENDIAN_LITTLE);
    fmt::println(stderr, "writing RF64/WAV int16 audio samples to '{}'",
                 filename);
    break;
  case OutputMode::WAV_FLOAT32:
    audio_output = std::make_unique<SndfileOutput>(
        filename, pcmrate, stereo,
        SF_FORMAT_RF64 | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE);
    fmt::println(stderr, "writing RF64/WAV float32 audio samples to '{}'",
                 filename);
    break;
  case OutputMode::PORTAUDIO:
    audio_output = std::make_unique<PortAudioOutput>(
        portaudiodev, pcmrate, stereo, portaudio_suggested_latency,
        portaudio_prefill_ms, portaudio_prefill_adapt_ms);
    if (portaudiodev == -1) {
      fmt::print(stderr, "playing audio to PortAudio default device: ");
    } else {
      fmt::print(stderr,
                 "playing audio to PortAudio device {}: ", portaudiodev);
    }
    fmt::println(stderr, "name '{}'", audio_output->get_device_name());
    if (portaudio_prefill_ms > 0) {
      fmt::println(stderr, "PortAudio prefill: {} [ms]", portaudio_prefill_ms);
    }
    if (portaudio_prefill_adapt_ms > 0) {
      fmt::println(stderr, "PortAudio prefill adapt: {} [ms]",
                   portaudio_prefill_adapt_ms);
    }
    break;
#if defined(LIBSNDFILE_MP3_ENABLED)
  case OutputMode::MP3_FMAUDIO:
    audio_output = std::make_unique<SndfileOutput>(
        filename, pcmrate, stereo, SF_FORMAT_MPEG | SF_FORMAT_MPEG_LAYER_III);
    fmt::println(stderr, "writing MP3 FM-broadcast audio samples to '{}'",
                 filename);
    break;
#endif // LIBSNDFILE_MP3_ENABLED
  }

  if (!(*audio_output)) {
    fmt::println(stderr, "ERROR: AudioOutput: {}", audio_output->error());
    exit(1);
  }

  if (!get_device(devnames, devtype, up_srcsdr, devidx)) {
    exit(1);
  }

  if (!(*up_srcsdr)) {
    fmt::println(stderr, "ERROR source: {}", up_srcsdr->error());
    exit(1);
  }

  // Configure device and start streaming.
  if (!up_srcsdr->configure(config_str)) {
    fmt::println(stderr, "ERROR: configuration: {}", up_srcsdr->error());
    exit(1);
  }

  const double freq = up_srcsdr->get_configured_frequency();
  fmt::print(stderr, "tuned for {:.7g} [MHz]", freq * 1.0e-6);
  const double tuner_freq = up_srcsdr->get_frequency();
  if (std::fabs(tuner_freq - freq) > 1.0) {
    fmt::print(stderr, ", device tuned for {:.7g} [MHz]", tuner_freq * 1.0e-6);
  }
  fmt::println(stderr, "");

  double ifrate = up_srcsdr->get_sample_rate();

  unsigned int if_blocksize;

  const bool enable_fs_fourth_downconverter = !(up_srcsdr->is_low_if());

  bool enable_downsampling = true;
  double if_decimation_ratio = 1.0;
  const double fm_target_rate = FmDecoder::sample_rate_if;
  const double am_target_rate = AmDecoder::internal_rate_pcm;
  const double nbfm_target_rate = NbfmDecoder::internal_rate_pcm;

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
  const unsigned int stat_rate =
      std::max(1u, (unsigned int)((double)ifrate / (double)if_blocksize / 9.0));
  fmt::println(stderr, "stat_rate = {}", stat_rate);

  // IF rate compensation if requested.
  if (ifrate_offset_enable) {
    ifrate *= 1.0 + (ifrate_offset_ppm / 1000000.0);
  }
  // The resamplers require strictly positive sample rates.
  if (!(ifrate > 0)) {
    fmt::println(stderr, "ERROR: IF sample rate must be positive: {:.9g} [Hz]",
                 ifrate);
    exit(1);
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

  const double demodulator_rate = ifrate / if_decimation_ratio;
  const double total_decimation_ratio = ifrate / pcmrate;
  const double audio_decimation_ratio = demodulator_rate / pcmrate;

  // Guard the IF resampler against extreme upsampling ratios (e.g. -r): r8brain
  // buffer sizes scale with the ratio and its int lengths overflow; 64x is far
  // above any legitimate use. See doc/FIXES_CLAUDE_20260610.md (X2).
  constexpr double max_if_upsampling_ratio = 64.0;
  if (demodulator_rate / ifrate > max_if_upsampling_ratio) {
    fmt::println(stderr,
                 "ERROR: IF sample rate {:.9g} [Hz] is too low for "
                 "demodulator rate {:.9g} [Hz]",
                 ifrate, demodulator_rate);
    exit(1);
  }

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

  up_srcsdr->print_specific_parms();

  // Create source data queue.
  DataBuffer<IQSample> source_buffer;

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

  // Status line and stereo messages are printed by a separate thread
  // (not started in quiet mode).
  std::unique_ptr<StatusPrinter> up_printer;
  if (!quietmode) {
    up_printer = std::make_unique<StatusPrinter>(
        modtype, verbosemode, audio_output.get(), &source_buffer,
        up_srcsdr.get());
  }
  StatusSnapshot status_snapshot;
  bool status_pending = false;

  // Loop buffers, declared once so they keep their allocations across
  // blocks (see doc/MAIN_CPP_SWAP_20260924.md).
  IQSampleVector if_shifted_samples;
  IQSampleVector if_samples;
  SampleVector audiosamples;

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

    IQSampleVector if_downsampled_samples;

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
    const size_t if_samples_size = if_samples.size();
    const bool if_exists = if_samples_size > 0;
    double if_rms = 0.0;

    if (!if_exists) {
      // go to the end of the for loop
      continue;
    }

    // Valid data exists in if_samples
    // from here in the for loop

    // Guard against division by zero when the tuner frequency is unknown
    // (e.g. filesource with freq=0).
    if (tuner_freq > 0.0) {
      if (modtype == ModType::FM) {
        // the minus factor is to show the ppm correction
        // to make and not the one which has already been made
        ppm_average.feed((fm.get_tuning_offset() / tuner_freq) * -1.0e6);
      } else if (modtype == ModType::NBFM) {
        ppm_average.feed((nbfm.get_tuning_offset() / tuner_freq) * -1.0e6);
      }
    }

    // Add 1e-9 to log10() to prevent generating NaN
    const float if_level_db = 20 * log10(if_level + 1e-9);

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

    const size_t audiosamples_size = audiosamples.size();
    const bool audio_exists = audiosamples_size > 0;

    if (!audio_exists) {
      // go to the end of the for loop
      continue;
    }

    // Valid audio data exists in audiosamples
    // from here in the for loop

    // Measure audio level
    float audio_mean, audio_rms;
    Utility::samples_mean_rms(audiosamples, audio_mean, audio_rms);
    audio_level = 0.95 * audio_level + 0.05 * audio_rms;

    // Set nominal audio volume (-6dB) when IF squelch is open,
    // set to zero volume if the squelch is closed.
    Utility::adjust_gain(audiosamples, if_rms >= squelch_level ? 0.5 : 0.0);
    // Write samples to output.
    // Treat a write failure as fatal and exit the main loop cleanly.
    if (!audio_output->write(audiosamples)) {
      fmt::println(stderr, "\nERROR: AudioOutput: {}", audio_output->error());
      stop_flag.store(true);
      break;
    }

    // Publish status values for the printer thread if not in quiet mode.
    if (up_printer) {
      if ((block % stat_rate) == 0) {
        // Stereo detection display
        // Use a state machine here
        if (modtype == ModType::FM) {
          const float pilot_level = fm.get_pilot_level();
          pilot_level_average.feed(pilot_level);
          const bool stereo_status = fm.stereo_detected();
          switch (pilot_status) {
          case PilotState::NotDetected:
            if (stereo_status) {
              status_snapshot.stereo_events++;
              pilot_status = PilotState::Detected;
              pilot_level_average.fill(0.0f);
            }
            break;
          case PilotState::Detected:
            if (!stereo_status) {
              status_snapshot.stereo_events++;
              pilot_status = PilotState::NotDetected;
            }
            break;
          }
        }
        // Collect per-block statistics.
        // Add 1e-9 to log10() to prevent generating NaN
        status_snapshot.block = block;
        status_snapshot.if_db = if_level_db;
        status_snapshot.af_db = 20 * log10(audio_level + 1e-9) + 3.01;

        switch (modtype) {
        case ModType::FM:
          status_snapshot.ppm = ppm_average.average();
          status_snapshot.pilot = pilot_level_average.average();
          break;
        case ModType::NBFM:
          status_snapshot.ppm = ppm_average.average();
          break;
        case ModType::AM:
        case ModType::DSB:
        case ModType::USB:
        case ModType::LSB:
        case ModType::CW:
        case ModType::WSPR:
          // Statistics without ppm offset.
          // Add 1e-9 to log10() to prevent generating NaN
          status_snapshot.agc_db =
              20 * log10(am.get_if_agc_current_gain() + 1e-9);
          break;
        }
        status_pending = true;
      }
      // Never wait for the printer: retry on the next block if it is busy.
      if (status_pending && up_printer->try_publish(status_snapshot)) {
        status_pending = false;
      }
    }

#ifdef COEFF_MONITOR
    // Outside the quiet-mode branch on purpose: this is a debug dump with its
    // own cadence and its own guards, not part of the per-block status
    // display, and -q must not silence it.
    if ((modtype == ModType::FM) && (multipathfilter_stages > 0) &&
        (block % (stat_rate * 10)) == 0) {
      double mf_error = fm.get_multipath_error();
      const MfCoeffVector &mf_coeff = fm.get_multipath_coefficients();
      fmt::print(stderr, "block,{},mf_error,{:.9f},mf_coeff,", block, mf_error);
      for (unsigned int i = 0; i < mf_coeff.size(); i++) {
        MfCoeff val = mf_coeff[i];
        fmt::print(stderr, "{},{:.9f},{:.9f},", i, val.real(), val.imag());
      }
      fmt::print(stderr, "\n");
      fflush(stderr);
    }
#endif

    // Write PPS markers.
    if (ppsfile != nullptr) {
      switch (modtype) {
      case ModType::FM:
        for (const PilotPhaseLock::PpsEvent &ev : fm.get_pps_events()) {
          double ts = prev_block_time;
          ts += ev.block_position * (block_time - prev_block_time);
          fmt::println(ppsfile, "{:>8} {:>14} {:18.6f} {:+9.3f}", ev.pps_index,
                       ev.sample_index, ts, if_level_db);
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
  // Join the printer before anything else is written to stderr.
  if (up_printer) {
    if (status_pending) {
      up_printer->publish(status_snapshot);
    }
    up_printer->stop();
  }
  fmt::println(stderr, "");

  // Close audio output.
  audio_output->output_close();
  // Close the PPS output file if opened (and not stdout).
  if ((ppsfile != nullptr) && (ppsfile != stdout)) {
    fclose(ppsfile);
  }
  // Terminate receiver thread.
  up_srcsdr->stop();

  if (!quietmode) {
    print_summary(*audio_output, source_buffer, *up_srcsdr);
  }
  fmt::println(stderr, "airspy-fmradion terminated");

  // Destructors of the source driver and other objects
  // will perform the proper cleanup.
}

// end
