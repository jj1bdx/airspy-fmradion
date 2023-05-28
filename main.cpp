// airspy-fmradion - Software decoder for FM broadcast radio with RTL-SDR
//
// Copyright (C) 2013, Joris van Rantwijk.
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2018-2022 Kenji Rikitake, JJ1BDX
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
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <memory>
#include <signal.h>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include "AirspyHFSource.h"
#include "AirspySource.h"
#include "AmDecode.h"
#include "AudioOutput.h"
#include "DataBuffer.h"
#include "FileSource.h"
#include "Filter.h"
#include "FilterParameters.h"
#include "FineTuner.h"
#include "FmDecode.h"
#include "FourthConverterIQ.h"
#include "MovingAverage.h"
#include "NbfmDecode.h"
#include "RtlSdrSource.h"
#include "SoftFM.h"
#include "Utility.h"

// define this for enabling coefficient monitor functions
// #undef COEFF_MONITOR

// define this for monitoring DataBuffer queue status
// #undef DATABUFFER_QUEUE_MONITOR

#define AIRSPY_FMRADION_VERSION "20230528-2"

// Flag to set graceful termination
// in process_signals()
static std::atomic_bool stop_flag(false);

// Get data from output buffer and write to output stream.
// This code runs in a separate thread.
static void write_output_data(AudioOutput *output, DataBuffer<Sample> *buf) {

#ifdef DATABUFFER_QUEUE_MONITOR
  unsigned int max_queue_length = 0;
#endif // DATABUFFER_QUEUE_MONITOR

  while (!stop_flag.load()) {

    if (buf->pull_end_reached()) {
      // Reached end of stream.
      break;
    }

    // Get samples from buffer and write to output.
    SampleVector samples = buf->pull();
    output->write(samples);
    if (!(*output)) {
      fprintf(stderr, "ERROR: AudioOutput: %s\n", output->error().c_str());
      // Setting stop_flag to true, suggested by GitHub @montgomeryb
      stop_flag.store(true);
    }

#ifdef DATABUFFER_QUEUE_MONITOR
    unsigned int queue_size = (unsigned int)buf->queue_size();
    if (queue_size > max_queue_length) {
      max_queue_length = queue_size;
      fprintf(stderr, "Max queue length: %u\n", max_queue_length);
    }
#endif // DATABUFFER_QUEUE_MONITOR
  }
}

static void usage() {
  fprintf(
      stderr,
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
      "  -P device_num  Play audio via PortAudio device index number\n"
      "                 use string '-' to specify the default PortAudio "
      "device\n"
      "  -T filename    Write pulse-per-second timestamps\n"
      "                 use filename '-' to write to stdout\n"
      "  -b seconds     (ignored, remained for a compatibility reason)\n"
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
      "                 (For stable reception only:\n"
      "                  turn off if reception becomes unstable)\n"
      "  -r ppm         Set IF offset in ppm (range: +-1000000ppm)\n"
      "                 (This option affects output pitch and timing:\n"
      "                  use for the output timing compensation only!)\n"
      "  -A             (FM only) experimental 10Hz-step IF AFC\n"
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
      "                 valid values: 192k to 31M, and 60M to 260M\n"
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
      "\n");
}

static void badarg(const char *label) {
  usage();
  fprintf(stderr, "ERROR: Invalid argument for %s\n", label);
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
      fprintf(stderr, "ERROR: invalid device index %d\n", devidx);
    }

    fprintf(stderr, "Found %u devices:\n", (unsigned int)devnames.size());

    for (unsigned int i = 0; i < devnames.size(); i++) {
      fprintf(stderr, "%2u: %s\n", i, devnames[i].c_str());
    }

    return false;
  }

  fprintf(stderr, "using device %d: %s\n", devidx, devnames[devidx].c_str());

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
      fprintf(stderr, "ERROR: sigwait failed, (%s)\n", strerror(err));
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
  // bufsecs is ignored right now
  double bufsecs = -1;
  bool enable_squelch = false;
  double squelch_level_db = 150.0;
  bool pilot_shift = false;
  bool deemphasis_na = false;
  int multipathfilter_stages = 0;
  bool ifrate_offset_enable = false;
  double ifrate_offset_ppm = 0;
  bool enable_fm_afc = false;
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
    fprintf(stderr, "ERROR: can not mask signals (%s)\n", strerror(err));
    exit(1);
  }
  // Start thread to catch the masked signals.
  err = pthread_create(&sigmask_thread_id, NULL, process_signals, 0);
  if (err != 0) {
    fprintf(stderr, "ERROR: unable to create pthread of process_signals(%s)\n",
            strerror(err));
    exit(1);
  }

  // Print starting messages.
  fprintf(stderr, "airspy-fmradion " AIRSPY_FMRADION_VERSION "\n");
  fprintf(stderr, "Software FM/AM radio for ");
  fprintf(stderr, "Airspy R2, Airspy HF+, and RTL-SDR\n");

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
      {"buffer", required_argument, nullptr, 'b'},
      {"pilotshift", no_argument, nullptr, 'X'},
      {"usa", no_argument, nullptr, 'U'},
      {"filtertype", optional_argument, nullptr, 'f'},
      {"squelch", required_argument, nullptr, 'l'},
      {"multipathfilter", required_argument, nullptr, 'E'},
      {"ifrateppm", optional_argument, nullptr, 'r'},
      {"afc", optional_argument, nullptr, 'A'},
      {nullptr, no_argument, nullptr, 0}};

  int c, longindex;
  while ((c = getopt_long(argc, argv, "m:t:c:d:MR:F:W:G:f:l:P:T:b:qXUE:r:A",
                          longopts, &longindex)) >= 0) {
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
    case 'b':
      if (!Utility::parse_dbl(optarg, bufsecs) || bufsecs < 0) {
        badarg("-b");
      }
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
    case 'A':
      enable_fm_afc = true;
      break;
    default:
      usage();
      fprintf(stderr, "ERROR: Invalid command line options\n");
      exit(1);
    }
  }

  if (optind < argc) {
    usage();
    fprintf(stderr, "ERROR: Unexpected command line options\n");
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
    fprintf(
        stderr,
        "ERROR: wrong device type (-t option) must be one of the following:\n");
    fprintf(stderr, "        rtlsdr, airspy, airspyhf, filesource\n");
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
    fprintf(stderr, "Modulation type string unsuppored\n");
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
    fprintf(stderr, "Filter type string unsuppored\n");
    exit(1);
  }

  // Open PPS file.
  if (!ppsfilename.empty()) {
    if (ppsfilename == "-") {
      fprintf(stderr, "writing pulse-per-second markers to stdout\n");
      ppsfile = stdout;
    } else {
      fprintf(stderr, "writing pulse-per-second markers to '%s'\n",
              ppsfilename.c_str());
      ppsfile = fopen(ppsfilename.c_str(), "w");

      if (ppsfile == nullptr) {
        fprintf(stderr, "ERROR: can not open '%s' (%s)\n", ppsfilename.c_str(),
                strerror(errno));
        exit(1);
      }
    }

    switch (modtype) {
    case ModType::FM:
      fprintf(ppsfile, "# pps_index sample_index unix_time if_level\n");
      break;
    case ModType::NBFM:
    case ModType::AM:
    case ModType::DSB:
    case ModType::USB:
    case ModType::LSB:
    case ModType::CW:
    case ModType::WSPR:
      fprintf(ppsfile, "# block unix_time if_level\n");
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
    fprintf(stderr,
            "writing raw 16-bit integer little-endian audio samples to '%s'\n",
            filename.c_str());
    break;
  case OutputMode::RAW_FLOAT32:
    audio_output.reset(
        new SndfileOutput(filename, pcmrate, stereo,
                          SF_FORMAT_RAW | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE));
    fprintf(stderr,
            "writing raw 32-bit float little-endian audio samples to '%s'\n",
            filename.c_str());
    break;
  case OutputMode::WAV_INT16:
    audio_output.reset(new SndfileOutput(filename, pcmrate, stereo,
                                         SF_FORMAT_RF64 | SF_FORMAT_PCM_16 |
                                             SF_ENDIAN_LITTLE));
    fprintf(stderr, "writing RF64/WAV int16 audio samples to '%s'\n",
            filename.c_str());
    break;
  case OutputMode::WAV_FLOAT32:
    audio_output.reset(
        new SndfileOutput(filename, pcmrate, stereo,
                          SF_FORMAT_RF64 | SF_FORMAT_FLOAT | SF_ENDIAN_LITTLE));
    fprintf(stderr, "writing RF64/WAV float32 audio samples to '%s'\n",
            filename.c_str());
    break;
  case OutputMode::PORTAUDIO:
    audio_output.reset(new PortAudioOutput(portaudiodev, pcmrate, stereo));
    if (portaudiodev == -1) {
      fprintf(stderr, "playing audio to PortAudio default device: ");
    } else {
      fprintf(stderr, "playing audio to PortAudio device %d: ", portaudiodev);
    }
    fprintf(stderr, "name '%s'\n", audio_output->get_device_name().c_str());
    break;
  }

  if (!(*audio_output)) {
    fprintf(stderr, "ERROR: AudioOutput: %s\n", audio_output->error().c_str());
    exit(1);
  }

  if (!get_device(devnames, devtype, &srcsdr, devidx)) {
    exit(1);
  }

  if (!(*srcsdr)) {
    fprintf(stderr, "ERROR source: %s\n", srcsdr->error().c_str());
    delete srcsdr;
    exit(1);
  }

  // Configure device and start streaming.
  if (!srcsdr->configure(config_str)) {
    fprintf(stderr, "ERROR: configuration: %s\n", srcsdr->error().c_str());
    delete srcsdr;
    exit(1);
  }

  double freq = srcsdr->get_configured_frequency();
  fprintf(stderr, "tuned for %.7g [MHz]", freq * 1.0e-6);
  double tuner_freq = srcsdr->get_frequency();
  if (tuner_freq != freq) {
    fprintf(stderr, ", device tuned for %.7g [MHz]", tuner_freq * 1.0e-6);
  }
  fprintf(stderr, "\n");

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
    if_blocksize = 65536;
    break;
  case DevType::FileSource:
    if_blocksize = 2048;
    break;
  }

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
  fprintf(stderr, "Decoding modulation type: %s\n", modtype_str.c_str());
  if (enable_squelch) {
    fprintf(stderr, "IF Squelch level: %.9g [dB]\n", 20 * log10(squelch_level));
  }

  double demodulator_rate = ifrate / if_decimation_ratio;
  double total_decimation_ratio = ifrate / pcmrate;
  double audio_decimation_ratio = demodulator_rate / pcmrate;

  // Display ifrate compensation if applicable.
  if (ifrate_offset_enable) {
    fprintf(stderr, "IF sample rate shifted by: %.9g [ppm]\n",
            ifrate_offset_ppm);
  }

  // Display filter configuration.
  fprintf(stderr, "IF sample rate: %.9g [Hz], ", ifrate);
  fprintf(stderr, "IF decimation: / %.9g\n", if_decimation_ratio);
  fprintf(stderr, "Demodulator rate: %.8g [Hz], ", demodulator_rate);
  fprintf(stderr, "audio decimation: / %.9g\n", audio_decimation_ratio);

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
    fprintf(stderr, "ERROR: source: %s\n", up_srcsdr->error().c_str());
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
    fprintf(stderr, "audio sample rate: %u [Hz],", pcmrate);
    fprintf(stderr, " audio bandwidth: %u [Hz]\n",
            (unsigned int)FmDecoder::bandwidth_pcm);
    fprintf(stderr, "audio totally decimated from IF by: %.9g\n",
            total_decimation_ratio);
    break;
  case ModType::AM:
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    fprintf(stderr, "AM demodulator deemphasis: %.9g [µs]\n",
            AmDecoder::deemphasis_time);
    break;
  }
  if (modtype == ModType::FM) {
    fprintf(stderr, "FM demodulator deemphasis: %.9g [µs]\n", deemphasis);
    if (multipathfilter_stages > 0) {
      fprintf(stderr, "FM IF multipath filter enabled, stages: %d\n",
              multipathfilter_stages);
    }
  }
  fprintf(stderr, "Filter type: %s\n", filtertype_str.c_str());

  // Initialize moving average object for FM ppm monitoring.
  const unsigned int ppm_average_stages = 100;
  MovingAverage<float> ppm_average(ppm_average_stages, 0.0f);

  // Initialize moving average object for FM AFC.
  const unsigned int fm_afc_average_stages = 1000;
  MovingAverage<float> fm_afc_average(fm_afc_average_stages, 0.0f);
  const unsigned int fm_afc_hz_step = 10;
  FineTuner fm_afc_finetuner((unsigned int)fm_target_rate / fm_afc_hz_step);
  float fm_afc_offset_sum = 0.0;

  // If buffering enabled, start background output thread.
  DataBuffer<Sample> output_buffer;
  std::thread output_thread;
  // Always use output_thread for smooth output.
  output_thread =
      std::thread(write_output_data, audio_output.get(), &output_buffer);
  SampleVector audiosamples;
  float audio_level = 0;
  bool got_stereo = false;

  double block_time = Utility::get_time();

  // TODO: ~0.1sec / display (should be tuned)
  unsigned int stat_rate =
      lrint(5120 / (if_blocksize / total_decimation_ratio));
  unsigned int discarding_blocks;
  switch (modtype) {
  case ModType::FM:
  case ModType::NBFM:
    discarding_blocks = stat_rate * 4;
    break;
  case ModType::AM:
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    discarding_blocks = stat_rate * 2;
    break;
  }

  float if_level = 0;

#ifdef DATABUFFER_QUEUE_MONITOR
  // unsigned int nchannel = stereo ? 2 : 1;
  bool inbuf_length_warning = false;
  unsigned int max_source_buffer_length = 0;
#endif // DATABUFFER_QUEUE_MONITOR

  // Main loop.
  for (uint64_t block = 0; !stop_flag.load(); block++) {

#ifdef DATABUFFER_QUEUE_MONITOR
    // Check for overflow of source buffer.
    if (!inbuf_length_warning && source_buffer.queue_size() > 10 * ifrate) {
      fprintf(stderr, "\nWARNING: source buffer queue sizes exceeds 10 (system "
                      "too slow)\n");
      inbuf_length_warning = true;
    }
#endif // DATABUFFER_QUEUE_MONITOR

    // Pull next block from source buffer.
    IQSampleVector iqsamples = source_buffer.pull();

    IQSampleVector if_afc_samples;
    IQSampleVector if_shifted_samples;
    IQSampleVector if_downsampled_samples;
    IQSampleVector if_samples;

    if (iqsamples.empty()) {
      break;
    }

#ifdef DATABUFFER_QUEUE_MONITOR
    unsigned int source_buffer_length = source_buffer.queue_size();
    if (source_buffer_length > max_source_buffer_length) {
      max_source_buffer_length = source_buffer_length;
      fprintf(stderr, "Max source buffer length: %u\n",
              max_source_buffer_length);
    }
#endif // DATABUFFER_QUEUE_MONITOR

    double prev_block_time = block_time;
    block_time = Utility::get_time();

    // Fine tuning is not needed
    // so long as the stability of the receiver device is
    // within the range of +- 1ppm (~100Hz or less).

    // Experimental FM broadcast AFC code
    if (modtype == ModType::FM && enable_fm_afc) {
      // get the frequency offset
      fm_afc_average.feed(fm.get_tuning_offset());
      if ((block % fm_afc_average_stages) == 0) {
        fm_afc_offset_sum += 0.7 * fm_afc_average.average();
        fm_afc_finetuner.set_freq_shift(
            -((unsigned int)std::round(fm_afc_offset_sum / fm_afc_hz_step)));
      }
      fm_afc_finetuner.process(iqsamples, if_afc_samples);
    } else {
      if_afc_samples = std::move(iqsamples);
    }

    if (enable_fs_fourth_downconverter) {
      // Fs/4 downconvering is required
      // to avoid frequency zero offset
      // because Airspy HF+ and RTL-SDR are Zero IF receivers
      fourth_downconverter.process(if_afc_samples, if_shifted_samples);
    } else {
      if_shifted_samples = std::move(if_afc_samples);
    }

    // Downsample IF for the decoder.
    if (enable_downsampling) {
      if_resampler.process(if_shifted_samples, if_samples);
    } else {
      if_samples = std::move(if_shifted_samples);
    }

    // Downsample IF for the decoder.
    bool if_exists = if_samples.size() > 0;
    double if_rms = 0.0;

    if (if_exists) {
      // Decode signal.
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
    }

    size_t audiosamples_size = audiosamples.size();
    bool audio_exists = audiosamples_size > 0;

    // Measure audio level when audio exists
    if (audio_exists) {
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
    }

    if (modtype == ModType::FM) {
      // the minus factor is to show the ppm correction
      // to make and not the one which has already been made
      ppm_average.feed((fm.get_tuning_offset() / tuner_freq) * -1.0e6);
    } else if (modtype == ModType::NBFM) {
      ppm_average.feed((nbfm.get_tuning_offset() / tuner_freq) * -1.0e6);
    }

    // Add 1e-9 to log10() to prevent generating NaN
    float if_level_db = 20 * log10(if_level + 1e-9);

    // Show status messages for each block if not in quiet mode.
    bool stereo_change = false;
    if (!quietmode) {
      // Stereo detection display
      if (modtype == ModType::FM) {
        stereo_change = (fm.stereo_detected() != got_stereo);
        // Show stereo status.
        if (stereo_change) {
          got_stereo = fm.stereo_detected();
          if (got_stereo) {
            fprintf(stderr, "\ngot stereo signal, pilot level = %.7f\n",
                    fm.get_pilot_level());
          } else {
            fprintf(stderr, "\nlost stereo signal\n");
          }
        }
      }
      if (stereo_change ||
          (((block % stat_rate) == 0) && (block > discarding_blocks))) {
        // Show per-block statistics.
        // Add 1e-9 to log10() to prevent generating NaN
        float audio_level_db = 20 * log10(audio_level + 1e-9) + 3.01;
#ifdef DATABUFFER_QUEUE_MONITOR
        uint32_t quelen = (uint32_t)output_buffer.queue_size();
#endif // DATABUFFER_QUEUE_MONITOR

        switch (modtype) {
        case ModType::FM:
        case ModType::NBFM:
#ifdef DATABUFFER_QUEUE_MONITOR
          fprintf(stderr,
#ifdef COEFF_MONITOR
                  // DATABUFFER_QUEUE_MONITOR && COEFF_MONITOR
                  "blk=%11" PRIu64
                  ":ppm=%+7.3f:IF=%+6.1fdB:AF=%+6.1fdB:qlen=%" PRIu32 "\n",
#else
                  // DATABUFFER_QUEUE_MONITOR && !(COEFF_MONITOR)
                  "\rblk=%11" PRIu64
                  ":ppm=%+7.3f:IF=%+6.1fdB:AF=%+6.1fdB:qlen=%" PRIu32,
#endif // COEFF_MONITOR
                  block, ppm_average.average(), if_level_db, audio_level_db,
                  quelen);
#else
          // !(DATABUFFER_QUEUE_MONITOR) && !(COEFF_MONITOR)
          fprintf(stderr,
                  "\rblk=%11" PRIu64 ":ppm=%+7.3f:IF=%+6.1fdB:AF=%+6.1fdB",
                  block, ppm_average.average(), if_level_db, audio_level_db);
#endif // DATABUFFER_QUEUE_MONITOR
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
          fprintf(stderr,
#ifdef DATABUFFER_QUEUE_MONITOR
                  "\rblk=%11" PRIu64
                  ":IF=%+6.1fdB:AGC=%+6.1fdB:AF=%+6.1fdB:qlen=%" PRIu32,
                  block, if_level_db, if_agc_gain_db, audio_level_db, quelen);
#else
                  "\rblk=%11" PRIu64 ":IF=%+6.1fdB:AGC=%+6.1fdB:AF=%+6.1fdB",
                  block, if_level_db, if_agc_gain_db, audio_level_db);
#endif // DATABUFFER_QUEUE_MONITOR
          fflush(stderr);
          break;
        }
      }

#ifdef COEFF_MONITOR
      if ((modtype == ModType::FM) && (multipathfilter_stages > 0) &&
          ((block % (stat_rate * 10)) == 0) && (block > discarding_blocks)) {
        double mf_error = fm.get_multipath_error();
        const MfCoeffVector &mf_coeff = fm.get_multipath_coefficients();
        fprintf(stderr, "block,%" PRIu64 ",mf_error,%.9f,mf_coeff,", block,
                mf_error);
        for (unsigned int i = 0; i < mf_coeff.size(); i++) {
          MfCoeff val = mf_coeff[i];
          fprintf(stderr, "%d,%.9f,%.9f,", i, val.real(), val.imag());
        }
        fprintf(stderr, "\n");
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
          fprintf(ppsfile, "%8s %14s %18.6f %+9.3f\n",
                  std::to_string(ev.pps_index).c_str(),
                  std::to_string(ev.sample_index).c_str(), ts, if_level_db);
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
          fprintf(ppsfile, "%11" PRIu64 " %18.6f %+9.3f\n", block,
                  prev_block_time, if_level_db);
          fflush(ppsfile);
        }
        break;
      }
    }

    // Throw away first blocks before stereo pilot locking is completed.
    // They are noisy because IF filters are still starting up.
    // (Increased from one to support high sampling rates)
    if ((block > discarding_blocks) && audio_exists) {
      // Write samples to output.
      // Always use buffered write.
      output_buffer.push(std::move(audiosamples));
    }
  }

  // End of main loop
  fprintf(stderr, "\n");

  // Terminate background audio output thread first.
  output_buffer.push_end();
  output_thread.join();
  // Close audio output.
  audio_output->output_close();

  // Terminate receiver thread.
  // Note: libusb-1.0.25 with Airspy HF+ driver
  // may cause crashing of this function.
  up_srcsdr->stop();

  fprintf(stderr, "airspy-fmradion terminated\n");

  // Destructors of the source driver and other objects
  // will perform the proper cleanup.
}

// end
