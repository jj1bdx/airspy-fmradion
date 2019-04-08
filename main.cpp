// airspy-fmradion - Software decoder for FM broadcast radio with RTL-SDR
//
// Copyright (C) 2013, Joris van Rantwijk.
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2018, 2019 Kenji Rikitake, JJ1BDX
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
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <getopt.h>
#include <memory>
#include <sys/time.h>
#include <thread>
#include <unistd.h>

#include "AirspyHFSource.h"
#include "AirspySource.h"
#include "AmDecode.h"
#include "AudioOutput.h"
#include "DataBuffer.h"
#include "FilterParameters.h"
#include "FmDecode.h"
#include "FourthDownconverterIQ.h"
#include "IfDownsampler.h"
#include "MovingAverage.h"
#include "RtlSdrSource.h"
#include "SoftFM.h"
#include "util.h"

#define AIRSPY_FMRADION_VERSION "v0.6.3-dev2"

/** Flag is set on SIGINT / SIGTERM. */
static std::atomic_bool stop_flag(false);

/** Simple linear gain adjustment. */
inline void adjust_gain(SampleVector &samples, double gain) {
  for (unsigned int i = 0, n = samples.size(); i < n; i++) {
    samples[i] *= gain;
  }
}

/**
 * Get data from output buffer and write to output stream.
 *
 * This code runs in a separate thread.
 */
void write_output_data(AudioOutput *output, DataBuffer<Sample> *buf,
                       unsigned int buf_minfill) {
  while (!stop_flag.load()) {

    if (buf->queued_samples() == 0) {
      // The buffer is empty. Perhaps the output stream is consuming
      // samples faster than we can produce them. Wait until the buffer
      // is back at its nominal level to make sure this does not happen
      // too often.
      buf->wait_buffer_fill(buf_minfill);
    }

    if (buf->pull_end_reached()) {
      // Reached end of stream.
      break;
    }

    // Get samples from buffer and write to output.
    SampleVector samples = buf->pull();
    output->write(samples);
    if (!(*output)) {
      fprintf(stderr, "ERROR: AudioOutput: %s\n", output->error().c_str());
    }
  }
}

/** Handle Ctrl-C and SIGTERM. */
static void handle_sigterm(int sig) {
  stop_flag.store(true);

  std::string msg = "\nGot signal ";
  msg += strsignal(sig);
  msg += ", stopping ...\n";

  const char *s = msg.c_str();
  ssize_t size = write(STDERR_FILENO, s, strlen(s));
  size++; // dummy
}

void usage() {
  fprintf(
      stderr,
      "Usage: airspy-fmradion [options]\n"
      "  -m modtype     Modulation type:\n"
      "                   - fm (default)\n"
      "                   - am \n"
      "  -t devtype     Device type:\n"
      "                   - rtlsdr: RTL-SDR devices\n"
      "                   - airspy: Airspy R2\n"
      "                   - airspyhf: Airspy HF+\n"
      "  -q             Quiet mode\n"
      "  -c config      Comma separated key=value configuration pairs or just "
      "key for switches\n"
      "                 See below for valid values per device type\n"
      "  -d devidx      Device index, 'list' to show device list (default 0)\n"
      "  -r pcmrate     Audio sample rate in Hz (default 48000 Hz)\n"
      "  -M             Disable stereo decoding\n"
      "  -R filename    Write audio data as raw S16_LE samples\n"
      "                 use filename '-' to write to stdout\n"
      "  -F filename    Write audio data as raw FLOAT_LE samples\n"
      "                 use filename '-' to write to stdout\n"
      "  -W filename    Write audio data to .WAV file\n"
      "  -P [device]    Play audio via ALSA device (default 'default')\n"
      "  -T filename    Write pulse-per-second timestamps\n"
      "                 use filename '-' to write to stdout\n"
      "  -b seconds     Set audio buffer size in seconds\n"
      "  -X             Shift pilot phase (for Quadrature Multipath Monitor)\n"
      "                 (-X is ignored under mono mode (-M))\n"
      "  -U             Set deemphasis to 75 microseconds (default: 50)\n"
      "  -f filtername  AM Filter type:\n"
      "                   - default: +-6kHz\n"
      "                   - middle:  +-4kHz\n"
      "                   - narrow:  +-3kHz\n"
      "\n"
      "Configuration options for RTL-SDR devices\n"
      "  freq=<int>     Frequency of radio station in Hz (default 100000000)\n"
      "                 valid values: 10M to 2.2G (working range depends on "
      "device)\n"
      "  srate=<int>    IF sample rate in Hz (default 937500)\n"
      "                 (valid ranges: [225001, 300000], [900001, 3200000]))\n"
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
      "                 valid values: 60M to 260M\n"
      "  srate=<int>    IF sample rate in Hz."
      "Depends on Airspy HF firmware and libairspyhf support\n"
      "                 Airspy HF firmware and library must support dynamic "
      "sample rate query. (default 768000)\n"
      "\n");
}

void badarg(const char *label) {
  usage();
  fprintf(stderr, "ERROR: Invalid argument for %s\n", label);
  exit(1);
}

bool parse_int(const char *s, int &v, bool allow_unit = false) {
  char *endp;
  long t = strtol(s, &endp, 10);
  if (endp == s) {
    return false;
  }
  if (allow_unit && *endp == 'k' && t > INT_MIN / 1000 && t < INT_MAX / 1000) {
    t *= 1000;
    endp++;
  }
  if (*endp != '\0' || t < INT_MIN || t > INT_MAX) {
    return false;
  }
  v = t;
  return true;
}

/** Return Unix time stamp in seconds. */
double get_time() {
  struct timeval tv;
  gettimeofday(&tv, NULL);
  return tv.tv_sec + 1.0e-6 * tv.tv_usec;
}

// Device type identifiers.
enum DevType { DEV_AIRSPY, DEV_AIRSPYHF, DEV_RTLSDR };

static bool get_device(std::vector<std::string> &devnames, DevType devtype,
                       Source **srcsdr, int devidx) {
  // Get device names.
  switch (devtype) {
  case DEV_RTLSDR:
    RtlSdrSource::get_device_names(devnames);
    break;
  case DEV_AIRSPY:
    AirspySource::get_device_names(devnames);
    break;
  case DEV_AIRSPYHF:
    AirspyHFSource::get_device_names(devnames);
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
  case DEV_RTLSDR:
    *srcsdr = new RtlSdrSource(devidx);
    break;
  case DEV_AIRSPY:
    *srcsdr = new AirspySource(devidx);
    break;
  case DEV_AIRSPYHF:
    *srcsdr = new AirspyHFSource(devidx);
    break;
  }

  return true;
}

int main(int argc, char **argv) {

  enum OutputMode { MODE_RAW_INT16, MODE_RAW_FLOAT32, MODE_WAV, MODE_ALSA };
  enum ModType { MOD_FM, MOD_AM };
  enum AmFilterType { AMFILTER_DEFAULT, AMFILTER_MIDDLE, AMFILTER_NARROW };

  int devidx = 0;
  int pcmrate = FmDecoder::sample_rate_pcm;
  bool stereo = true;
#ifdef USE_ALSA
  OutputMode outmode = MODE_ALSA;
  std::string filename;
#else  // !USE_ALSA
  OutputMode outmode = MODE_RAW_INT16;
  std::string filename("-");
#endif // USE_ALSA
  std::string alsadev("default");
  bool quietmode = false;
  std::string ppsfilename;
  FILE *ppsfile = NULL;
  double bufsecs = -1;
  bool pilot_shift = false;
  bool deemphasis_na = false;
  std::string config_str;
  std::string devtype_str;
  DevType devtype;
  std::string modtype_str("fm");
  ModType modtype = MOD_FM;
  std::string amfiltertype_str("default");
  AmFilterType amfiltertype = AMFILTER_DEFAULT;
  std::vector<std::string> devnames;
  Source *srcsdr = 0;

  fprintf(stderr, "airspy-fmradion " AIRSPY_FMRADION_VERSION "\n");
  fprintf(stderr, "Software FM/AM radio for ");
  fprintf(stderr, "Airspy R2, Airspy HF+, and RTL-SDR\n");

  const struct option longopts[] = {
      {"modtype", 2, NULL, 'm'},      {"devtype", 2, NULL, 't'},
      {"config", 2, NULL, 'c'},       {"dev", 1, NULL, 'd'},
      {"mono", 0, NULL, 'M'},         {"raw", 1, NULL, 'R'},
      {"float", 1, NULL, 'F'},        {"wav", 1, NULL, 'W'},
      {"amfiltertype", 2, NULL, 'f'}, {"play", 2, NULL, 'P'},
      {"pps", 1, NULL, 'T'},          {"buffer", 1, NULL, 'b'},
      {"quiet", 1, NULL, 'q'},        {"pilotshift", 0, NULL, 'X'},
      {"usa", 0, NULL, 'U'},          {NULL, 0, NULL, 0}};

  int c, longindex;
  while ((c = getopt_long(argc, argv, "m:t:c:d:MR:F:W:f:P::T:b:qXU", longopts,
                          &longindex)) >= 0) {
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
      if (!parse_int(optarg, devidx)) {
        devidx = -1;
      }
      break;
    case 'M':
      stereo = false;
      break;
    case 'R':
      outmode = MODE_RAW_INT16;
      filename = optarg;
      break;
    case 'F':
      outmode = MODE_RAW_FLOAT32;
      filename = optarg;
      break;
    case 'W':
      outmode = MODE_WAV;
      filename = optarg;
      break;
    case 'f':
      amfiltertype_str.assign(optarg);
      break;
    case 'P':
      outmode = MODE_ALSA;
      if (optarg != NULL) {
        alsadev = optarg;
      }
      break;
    case 'T':
      ppsfilename = optarg;
      break;
    case 'b':
      if (!parse_dbl(optarg, bufsecs) || bufsecs < 0) {
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

  if (strcasecmp(devtype_str.c_str(), "rtlsdr") == 0) {
    devtype = DEV_RTLSDR;
  } else if (strcasecmp(devtype_str.c_str(), "airspy") == 0) {
    devtype = DEV_AIRSPY;
  } else if (strcasecmp(devtype_str.c_str(), "airspyhf") == 0) {
    devtype = DEV_AIRSPYHF;
  } else {
    fprintf(
        stderr,
        "ERROR: wrong device type (-t option) must be one of the following:\n");
    fprintf(stderr, "        rtlsdr, airspy, airspyhf\n");
    exit(1);
  }

  if (strcasecmp(modtype_str.c_str(), "fm") == 0) {
    modtype = MOD_FM;
  } else if (strcasecmp(modtype_str.c_str(), "am") == 0) {
    modtype = MOD_AM;
    stereo = false;
  } else {
    fprintf(stderr, "Modulation type string unsuppored\n");
    exit(1);
  }

  if (strcasecmp(amfiltertype_str.c_str(), "default") == 0) {
    amfiltertype = AMFILTER_DEFAULT;
  } else if (strcasecmp(amfiltertype_str.c_str(), "middle") == 0) {
    amfiltertype = AMFILTER_MIDDLE;
  } else if (strcasecmp(amfiltertype_str.c_str(), "narrow") == 0) {
    amfiltertype = AMFILTER_NARROW;
  } else {
    fprintf(stderr, "AM filter type string unsuppored\n");
    exit(1);
  }

  // Catch Ctrl-C and SIGTERM
  struct sigaction sigact;
  sigact.sa_handler = handle_sigterm;
  sigemptyset(&sigact.sa_mask);
  sigact.sa_flags = SA_RESETHAND;

  if (sigaction(SIGINT, &sigact, NULL) < 0) {
    fprintf(stderr, "WARNING: can not install SIGINT handler (%s)\n",
            strerror(errno));
  }

  if (sigaction(SIGTERM, &sigact, NULL) < 0) {
    fprintf(stderr, "WARNING: can not install SIGTERM handler (%s)\n",
            strerror(errno));
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

      if (ppsfile == NULL) {
        fprintf(stderr, "ERROR: can not open '%s' (%s)\n", ppsfilename.c_str(),
                strerror(errno));
        exit(1);
      }
    }

    switch (modtype) {
    case MOD_FM:
      fprintf(ppsfile, "#pps_index sample_index   unix_time\n");
      break;
    case MOD_AM:
      fprintf(ppsfile, "#  block   unix_time\n");
      break;
    }
    fflush(ppsfile);
  }

  // Calculate number of samples in audio buffer.
  unsigned int outputbuf_samples = 0;

  if (bufsecs < 0 &&
      (outmode == MODE_ALSA || (outmode == MODE_RAW_INT16 && filename == "-") ||
       (outmode == MODE_RAW_FLOAT32 && filename == "-"))) {
    // Set default buffer to 1 second for interactive output streams.
    outputbuf_samples = pcmrate;
  } else if (bufsecs > 0) {
    // Calculate nr of samples for configured buffer length.
    outputbuf_samples = (unsigned int)(bufsecs * pcmrate);
  }
  fprintf(stderr, "output buffer length: %g [s]\n",
          outputbuf_samples / double(pcmrate));

  // Prepare output writer.
  std::unique_ptr<AudioOutput> audio_output;

  switch (outmode) {
  case MODE_RAW_INT16:
    fprintf(stderr,
            "writing raw 16-bit integer little-endian audio samples to '%s'\n",
            filename.c_str());
    audio_output.reset(new RawAudioOutput(filename));
    audio_output->SetConvertFunction(AudioOutput::samplesToInt16);
    break;
  case MODE_RAW_FLOAT32:
    fprintf(stderr,
            "writing raw 32-bit float little-endian audio samples to '%s'\n",
            filename.c_str());
    audio_output.reset(new RawAudioOutput(filename));
    audio_output->SetConvertFunction(AudioOutput::samplesToFloat32);
    break;
  case MODE_WAV:
    fprintf(stderr, "writing audio samples to '%s'\n", filename.c_str());
    audio_output.reset(new WavAudioOutput(filename, pcmrate, stereo));
    break;
  case MODE_ALSA:
#ifdef USE_ALSA
    fprintf(stderr, "playing audio to ALSA device '%s'\n", alsadev.c_str());
    audio_output.reset(new AlsaAudioOutput(alsadev, pcmrate, stereo));
    break;
#else  // !USE_ALSA
    fprintf(stderr, "ALSA not implemented\n");
    exit(1);
#endif // USE_ALSA
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

  bool enable_fs_fourth_downconverter = false;

  bool enable_two_downsampler_stages = false;

  unsigned int first_downsample = 1;
  std::vector<IQSample::value_type> first_coeff =
      FilterParameters::delay_3taps_only_iq;
  bool enable_second_downsampler = false;
  unsigned int second_downsample = 1;
  std::vector<IQSample::value_type> second_coeff =
      FilterParameters::delay_3taps_only_iq;

  unsigned int third_downsample = 1;
  std::vector<IQSample::value_type> third_coeff =
      FilterParameters::delay_3taps_only_iq;
  bool enable_fourth_downsampler = false;
  unsigned int fourth_downsample = 1;
  std::vector<IQSample::value_type> fourth_coeff =
      FilterParameters::delay_3taps_only_iq;

  // Configure filter and downsampler.
  switch (modtype) {
  case MOD_FM:
    // Configure FM mode constants.
    // Target frequency: 768~1250kHz
    switch (devtype) {
    case DEV_AIRSPY:
      if (ifrate == 10000000.0) {
        if_blocksize = 65536;
        enable_fs_fourth_downconverter = false;
        enable_two_downsampler_stages = false;
        first_downsample = 8;
        first_coeff = FilterParameters::jj1bdx_10000khz_div8;
        enable_second_downsampler = true;
        second_downsample = 4;
        second_coeff = FilterParameters::jj1bdx_1250khz_div4;
      } else if (ifrate == 2500000.0) {
        if_blocksize = 65536;
        enable_fs_fourth_downconverter = false;
        enable_two_downsampler_stages = false;
        first_downsample = 2;
        first_coeff = FilterParameters::jj1bdx_2500khz_div4;
        enable_second_downsampler = true;
        second_downsample = 2;
        second_coeff = FilterParameters::jj1bdx_600khz_625khz_div2;
      } else {
        fprintf(stderr, "Sample rate unsupported\n");
        fprintf(stderr, "Supported rate:\n");
        fprintf(stderr, "Airspy R2: 2500000, 10000000\n");
        delete srcsdr;
        exit(1);
      }
      break;
    case DEV_AIRSPYHF:
      if (ifrate == 768000.0) {
        if_blocksize = 16384;
        enable_fs_fourth_downconverter = true;
        enable_two_downsampler_stages = false;
        first_downsample = 2;
        first_coeff = FilterParameters::jj1bdx_768khz_div2;
        enable_second_downsampler = false;
      } else {
        fprintf(stderr, "Sample rate unsupported\n");
        fprintf(stderr, "Supported rate:\n");
        fprintf(stderr, "Airspy HF: 768000\n");
        delete srcsdr;
        exit(1);
      }
      break;
    case DEV_RTLSDR:
      if ((ifrate >= 900001.0) && (ifrate <= 937500.0)) {
        if_blocksize = 65536;
        enable_fs_fourth_downconverter = true;
        enable_two_downsampler_stages = false;
        first_downsample = 3;
        first_coeff = FilterParameters::jj1bdx_900khz_div3;
        enable_second_downsampler = false;
      } else {
        fprintf(stderr, "Sample rate unsupported\n");
        fprintf(stderr, "Supported rate:\n");
        fprintf(stderr, "RTL-SDR: 900001 ~ 937500 \n");
        delete srcsdr;
        exit(1);
      }
      break;
    default:
      fprintf(stderr, "For the device, fm modulation is unsupported\n");
      delete srcsdr;
      exit(1);
      break;
    }
    break;
  case MOD_AM:
    // Configure AM mode constants.
    switch (devtype) {
    case DEV_AIRSPY:
      if (ifrate == 10000000.0) {
        // 10000kHz: /2/3/5/7 -> 47.6190476kHz
        if_blocksize = 65536;
        enable_fs_fourth_downconverter = false;
        enable_two_downsampler_stages = true;
        first_downsample = 2;
        first_coeff = FilterParameters::jj1bdx_am_if_div2;
        enable_second_downsampler = true;
        second_downsample = 3;
        second_coeff = FilterParameters::jj1bdx_am_if_div3;
        third_downsample = 5;
        third_coeff = FilterParameters::jj1bdx_am_if_div5;
        enable_fourth_downsampler = true;
        fourth_downsample = 7;
        fourth_coeff = FilterParameters::jj1bdx_am_if_div7;
      } else if (ifrate == 2500000.0) {
        // 2500kHz: /3/4/5 -> 41.666666kHz
        if_blocksize = 65536;
        enable_fs_fourth_downconverter = false;
        enable_two_downsampler_stages = true;
        first_downsample = 3;
        first_coeff = FilterParameters::jj1bdx_am_if_div3;
        enable_second_downsampler = true;
        second_downsample = 4;
        second_coeff = FilterParameters::jj1bdx_am_if_div4;
        third_downsample = 5;
        third_coeff = FilterParameters::jj1bdx_am_if_div5;
        enable_fourth_downsampler = false;
      } else {
        fprintf(stderr, "Sample rate unsupported\n");
        fprintf(stderr, "Supported rate:\n");
        fprintf(stderr, "Airspy R2: 2500000, 10000000\n");
        delete srcsdr;
        exit(1);
      }
      break;
    case DEV_AIRSPYHF:
      if (ifrate == 768000.0) {
        // 768kHz: /4/4 -> 48kHz
        if_blocksize = 16384;
        enable_fs_fourth_downconverter = true;
        enable_two_downsampler_stages = false;
        first_downsample = 4;
        first_coeff = FilterParameters::jj1bdx_am_if_div4;
        enable_second_downsampler = true;
        second_downsample = 4;
        second_coeff = FilterParameters::jj1bdx_am_if_div4;
      } else {
        fprintf(stderr, "Sample rate unsupported\n");
        fprintf(stderr, "Supported rate:\n");
        fprintf(stderr, "Airspy HF: 768000\n");
        delete srcsdr;
        exit(1);
      }
      break;
    case DEV_RTLSDR:
      if ((ifrate >= 900001.0) && (ifrate <= 937500.0)) {
        // 900kHz: /4/5 -> 45kHz
        // No problem up to 960kHz
        if_blocksize = 65536;
        enable_fs_fourth_downconverter = true;
        enable_two_downsampler_stages = false;
        first_downsample = 4;
        first_coeff = FilterParameters::jj1bdx_am_if_div4;
        enable_second_downsampler = true;
        second_downsample = 5;
        second_coeff = FilterParameters::jj1bdx_am_if_div5;
      } else {
        fprintf(stderr, "Sample rate unsupported\n");
        fprintf(stderr, "Supported rate:\n");
        fprintf(stderr, "RTL-SDR: 900001 ~ 937500 \n");
        delete srcsdr;
        exit(1);
      }
      break;
    default:
      fprintf(stderr, "For the device, am modulation is unsupported\n");
      delete srcsdr;
      exit(1);
      break;
    }
    break;
  }

  unsigned int if_decimation_ratio = first_downsample * second_downsample *
                                     third_downsample * fourth_downsample;
  double demodulator_rate = ifrate / if_decimation_ratio;
  double total_decimation_ratio = ifrate / pcmrate;
  double audio_decimation_ratio = demodulator_rate / pcmrate;

  // Display filter configuration.
  fprintf(stderr, "IF sample rate: %.9g [Hz], ", ifrate);
  fprintf(stderr, "IF decimation: / %d\n", if_decimation_ratio);
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
  // std::thread source_thread(read_source_data, std::move(up_srcsdr),
  // &source_buffer);
  up_srcsdr->start(&source_buffer, &stop_flag);

  if (!up_srcsdr) {
    fprintf(stderr, "ERROR: source: %s\n", up_srcsdr->error().c_str());
    exit(1);
  }

  // Prevent aliasing at very low output sample rates.
  double deemphasis = deemphasis_na ? FmDecoder::default_deemphasis_na
                                    : FmDecoder::default_deemphasis_eu;

  // Prepare Fs/4 downconverter.
  FourthDownconverterIQ fourth_downconverter;

  // Prepare IF Downsamplers.
  IfDownsampler if_downsampler(
      first_downsample,          // first_downsample
      first_coeff,               // first_coeff
      enable_second_downsampler, // enable_second_downsampler
      second_downsample,         // second_downsample
      second_coeff               // second_coeff
  );

  IfDownsampler if_downsampler_second(
      third_downsample,          // first_downsample
      third_coeff,               // first_coeff
      enable_fourth_downsampler, // enable_second_downsampler
      fourth_downsample,         // second_downsample
      fourth_coeff               // second_coeff
  );

  // Prepare FM decoder.
  FmDecoder fm(demodulator_rate, // sample_rate_demod
               stereo,           // stereo
               deemphasis,       // deemphasis,
               pilot_shift       // pilot_shift
  );

  std::vector<IQSample::value_type> amfilter_coeff;

  switch (amfiltertype) {
  case AMFILTER_DEFAULT:
    amfilter_coeff = FilterParameters::delay_3taps_only_iq;
    break;
  case AMFILTER_MIDDLE:
    amfilter_coeff = FilterParameters::jj1bdx_am_12khz_middle;
    break;
  case AMFILTER_NARROW:
    amfilter_coeff = FilterParameters::jj1bdx_am_12khz_narrow;
    break;
  }

  // Prepare AM decoder.
  AmDecoder am(demodulator_rate, // sample_rate_demod
               amfilter_coeff    // amfilter_coeff
  );

  switch (modtype) {
  case MOD_FM:
    fprintf(stderr, "audio sample rate: %u [Hz],", pcmrate);
    fprintf(stderr, " audio bandwidth: %u [Hz]\n",
            (unsigned int)FmDecoder::bandwidth_pcm);
    fprintf(stderr, "audio totally decimated from IF by: %.9g\n",
            total_decimation_ratio);
    fprintf(stderr, "FM demodulator deemphasis: %.9g [µs]\n", deemphasis);
    break;
  case MOD_AM:
    fprintf(stderr, "AM filter type: %s\n", amfiltertype_str.c_str());
    fprintf(stderr, "AM demodulator deemphasis: %.9g [µs]\n",
            AmDecoder::default_deemphasis);
    break;
  }

  // Initialize moving average object for FM ppm monitoring.
  MovingAverage<float> ppm_average(100, 0.0f);

  // If buffering enabled, start background output thread.
  DataBuffer<Sample> output_buffer;
  std::thread output_thread;

  if (outputbuf_samples > 0) {
    unsigned int nchannel = stereo ? 2 : 1;
    output_thread = std::thread(write_output_data, audio_output.get(),
                                &output_buffer, outputbuf_samples * nchannel);
  }

  SampleVector audiosamples;
  bool inbuf_length_warning = false;
  double audio_level = 0;
  bool got_stereo = false;

  double block_time = get_time();

  // TODO: ~0.1sec / display (should be tuned)
  unsigned int stat_rate =
      lrint(5120 / (if_blocksize / total_decimation_ratio));
  unsigned int discarding_blocks;
  switch (modtype) {
  case MOD_FM:
    discarding_blocks = stat_rate * 4;
    break;
  case MOD_AM:
    discarding_blocks = 0;
    break;
  }

  // Main loop.
  for (unsigned int block = 0; !stop_flag.load(); block++) {

    // Check for overflow of source buffer.
    if (!inbuf_length_warning && source_buffer.queued_samples() > 10 * ifrate) {
      fprintf(stderr, "\nWARNING: Input buffer is growing (system too slow)\n");
      inbuf_length_warning = true;
    }

    // Pull next block from source buffer.
    IQSampleVector iqsamples = source_buffer.pull();

    IQSampleVector if_shifted_samples;
    IQSampleVector if_samples;
    IQSampleVector if_samples_second;

    if (iqsamples.empty()) {
      break;
    }

    double prev_block_time = block_time;
    block_time = get_time();

    // Fine tuning is not needed
    // so long as the stability of the receiver device is
    // within the range of +- 1ppm (~100Hz or less).

    if (enable_fs_fourth_downconverter) {
      // Fs/4 downconvering is required
      // to avoid frequency zero offset
      // because Airspy HF+ and RTL-SDR are Zero IF receivers
      fourth_downconverter.process(iqsamples, if_shifted_samples);
    } else {
      if_shifted_samples = iqsamples;
    }

    // Downsample IF for the decoder.

    if_downsampler.process(if_shifted_samples, if_samples_second);

    if (enable_two_downsampler_stages) {
      if_downsampler_second.process(if_samples_second, if_samples);
    } else {
      if_samples = if_samples_second;
    }

    // Decode signal.
    switch (modtype) {
    case MOD_FM:
      // Decode FM signal.
      fm.process(if_samples, audiosamples);
      break;
    case MOD_AM:
      // Decode AM signal.
      am.process(if_samples, audiosamples);
      break;
    }

    bool audio_exists = audiosamples.size() > 0;

    // Measure audio level when audio exists
    if (audio_exists) {
      double audio_mean, audio_rms;
      samples_mean_rms(audiosamples, audio_mean, audio_rms);
      audio_level = 0.95 * audio_level + 0.05 * audio_rms;

      // Set nominal audio volume (-6dB).
      adjust_gain(audiosamples, 0.5);
    }

    double ppm_value_average;

    if (modtype == MOD_FM) {
      // the minus factor is to show the ppm correction
      // to make and not the one made
      ppm_average.feed((fm.get_tuning_offset() / tuner_freq) * -1.0e6);
      ppm_value_average = ppm_average.average();
    }

    double if_level_db = 20 * log10(if_downsampler.get_if_level());
    double audio_level_db = 20 * log10(audio_level) + 3.01;

    double buflen_sec;
    unsigned int nchannel = stereo ? 2 : 1;
    std::size_t buflen = output_buffer.queued_samples();
    buflen_sec = buflen / nchannel / double(pcmrate);

    // Show status messages for each block if not in quiet mode.
    bool stereo_change = false;
    if (!quietmode) {
      switch (modtype) {
      case MOD_FM:
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
        // Show per-block statistics.
        if (stereo_change ||
            (((block % stat_rate) == 0) && (block > discarding_blocks))) {
          fprintf(stderr,
                  "\rblk=%8d:ppm=%+6.2f:IF=%+6.1fdB:AF=%+6.1fdB:buf=%.2fs",
                  block, ppm_value_average, if_level_db, audio_level_db,
                  buflen_sec);
          fflush(stderr);
        }
        break;
      case MOD_AM:
        // Show per-block statistics without ppm offset.
        if (((block % stat_rate) == 0) && (block > discarding_blocks)) {
          fprintf(stderr, "\rblk=%8d:IF=%+6.1fdB:AF=%+6.1fdB:buf=%.2fs", block,
                  if_level_db, audio_level_db, buflen_sec);
          fflush(stderr);
        }
        break;
      }
    }

    // Write PPS markers.
    if ((ppsfile != NULL) && audio_exists) {
      switch (modtype) {
      case MOD_FM:
        for (const PilotPhaseLock::PpsEvent &ev : fm.get_pps_events()) {
          double ts = prev_block_time;
          ts += ev.block_position * (block_time - prev_block_time);
          fprintf(ppsfile, "%8s %14s %18.6f\n",
                  std::to_string(ev.pps_index).c_str(),
                  std::to_string(ev.sample_index).c_str(), ts);
          fflush(ppsfile);
        }
        break;
      case MOD_AM:
        if ((block % (stat_rate * 10)) == 0) {
          fprintf(ppsfile, "%8d %18.6f\n", block, prev_block_time);
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
      if (outputbuf_samples > 0) {
        // Buffered write.
        output_buffer.push(move(audiosamples));
      } else {
        // Direct write.
        audio_output->write(audiosamples);
      }
    }
  }

  fprintf(stderr, "\n");

  // Join background threads.
  // source_thread.join();
  up_srcsdr->stop();

  if (outputbuf_samples > 0) {
    output_buffer.push_end();
    output_thread.join();
  }

  // No cleanup needed; everything handled by destructors

  return 0;
}

/* end */
