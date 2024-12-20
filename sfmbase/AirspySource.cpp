// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2024 Kenji Rikitake, JJ1BDX
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <thread>
#include <unistd.h>

#include "AirspySource.h"
#include "ConfigParser.h"
#include "Utility.h"

// #define DEBUG_AIRSPYSOURCE 1

AirspySource *AirspySource::m_this = 0;
const std::vector<int> AirspySource::m_lgains({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                               11, 12, 13, 14});
const std::vector<int> AirspySource::m_mgains({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                               11, 12, 13, 14, 15});
const std::vector<int> AirspySource::m_vgains({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10,
                                               11, 12, 13, 14, 15});

// Note:
// The following code requires the following WIP functions:
// * working airspy_open_sn()
// * working airspy_list_devices()

// Open Airspy device.
AirspySource::AirspySource(int dev_index)
    : m_dev(0), m_sampleRate(10000000), m_frequency(100000000), m_lnaGain(8),
      m_mixGain(0), m_vgaGain(10), m_biasAnt(false), m_lnaAGC(false),
      m_mixAGC(false), m_running(false), m_thread(0) {

  // Get library version number first.
  airspy_lib_version(&m_libv);
#ifdef DEBUG_AIRSPYSOURCE
  fmt::println(stderr, "Airspy Library Version: {}.{}.{}", m_libv.major_version,
               m_libv.minor_version, m_libv.revision);
#endif

  // Scan all devices, return how many are attached.
  m_ndev = airspy_list_devices(0, 0);
  if (m_ndev <= 0) {
    m_error = "No Airspy device found";
    m_dev = 0;
    m_this = this;
    return;
  }

  // List all available devices.
  m_serials.resize(m_ndev);
  if (m_ndev != airspy_list_devices(m_serials.data(), m_ndev)) {
    m_error = "Failed to obtain Airspy device serial numbers";
    m_dev = 0;
    m_this = this;
    return;
  }

  // Open the matched device.
  airspy_error rc = (airspy_error)airspy_open_sn(&m_dev, m_serials[dev_index]);
  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format(
        "Failed to open Airspy device for the first time at device index {}",
        dev_index);
    m_dev = 0;
  }

  if (m_dev) {
    uint32_t nbSampleRates;
    uint32_t *sampleRates;

    airspy_get_samplerates(m_dev, &nbSampleRates, 0);

    sampleRates = new uint32_t[nbSampleRates];

    airspy_get_samplerates(m_dev, sampleRates, nbSampleRates);

    if (nbSampleRates == 0) {
      m_error = "Failed to get Airspy device sample rate list";
      airspy_close(m_dev);
      m_dev = 0;
    } else {
      for (uint32_t i = 0; i < nbSampleRates; i++) {
        m_srates.push_back(sampleRates[i]);
      }
    }

    delete[] sampleRates;

    m_sratesStr = fmt::format("{}", fmt::join(m_srates, ", "));

    rc = (airspy_error)airspy_set_sample_type(m_dev, AIRSPY_SAMPLE_FLOAT32_IQ);

    if (rc != AIRSPY_SUCCESS) {
      m_error = "AirspyInput::start: could not set sample type to FLOAT32_IQ";
    }
  }

  m_lgainsStr = fmt::format("{}", fmt::join(m_lgains, ", "));
  m_mgainsStr = fmt::format("{}", fmt::join(m_mgains, ", "));
  m_vgainsStr = fmt::format("{}", fmt::join(m_vgains, ", "));

  m_this = this;
}

AirspySource::~AirspySource() {
#ifdef DEBUG_AIRSPYSOURCE
  fmt::println(stderr, "AirspySource::~AirspySource()");
#endif
  if (m_dev) {
    airspy_close(m_dev);
  }
  m_this = 0;
}

void AirspySource::get_device_names(std::vector<std::string> &devices) {
  int ndev;
  std::vector<uint64_t> serials;

  // Scan all devices, return how many are attached.
  ndev = airspy_list_devices(0, 0);
#ifdef DEBUG_AIRSPYSOURCE
  fmt::println(stderr, "AirspySource::get_device_names: "
                       "try to get available device numbers");
#endif
  if (ndev <= 0) {
    fmt::println(stderr, "AirspySource::get_device_names: no available device");
  }
  // List all available devices.
  serials.resize(ndev);
  if (ndev != airspy_list_devices(serials.data(), ndev)) {
    fmt::println(stderr, "AirspySource::get_device_names: "
                         "unable to obtain device list");
  } else {
    // Use obtained info during AirspySource object construction.
    for (int i = 0; i < ndev; i++) {
      std::string devname_ostr = fmt::format("Serial {:08x}", serials[i]);
      devices.push_back(devname_ostr);
    }
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr,
                 "AirspySource::get_device_names: "
                 "enumerated {} device(s)",
                 ndev);
#endif
  }
}

std::uint32_t AirspySource::get_sample_rate() { return m_sampleRate; }

std::uint32_t AirspySource::get_frequency() { return m_frequency; }

// Airspy R2/Mini are always using Low-IF by definition
bool AirspySource::is_low_if() { return true; }

void AirspySource::print_specific_parms() {
  fmt::println(stderr, "LNA/Mix/VGA gain: {}, {}, {} dB", m_lnaGain, m_mixGain,
               m_vgaGain);
  fmt::print(stderr, "Antenna bias: {}", m_biasAnt ? "on" : "off");
  fmt::print(stderr, " / LNA AGC: {}", m_lnaAGC ? "on" : "off");
  fmt::println(stderr, " / Mixer AGC: {}", m_mixAGC ? "on" : "off");
}

bool AirspySource::configure(int sampleRateIndex, uint32_t frequency,
                             bool bias_ant, int lna_gain, int mix_gain,
                             int vga_gain, bool lna_agc, bool mix_agc) {
  m_frequency = frequency;
  m_biasAnt = bias_ant;
  m_lnaGain = lna_gain;
  m_mixGain = mix_gain;
  m_vgaGain = vga_gain;
  m_lnaAGC = lna_agc;
  m_mixAGC = mix_agc;

  airspy_error rc;

  if (!m_dev) {
    return false;
  }

  rc = (airspy_error)airspy_set_freq(m_dev, static_cast<uint32_t>(m_frequency));

  if (rc != AIRSPY_SUCCESS) {
    m_error =
        fmt::format("Could not set center frequency to {} Hz", m_frequency);
    return false;
  }

  rc = (airspy_error)airspy_set_samplerate(
      m_dev, static_cast<airspy_samplerate_t>(sampleRateIndex));

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set center sample rate to {} Hz",
                          m_srates[sampleRateIndex]);
    return false;
  } else {
    m_sampleRate = m_srates[sampleRateIndex];
  }

  rc = (airspy_error)airspy_set_lna_gain(m_dev, m_lnaGain);

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set LNA gain to {} dB", m_lnaGain);
    return false;
  }

  rc = (airspy_error)airspy_set_mixer_gain(m_dev, m_mixGain);

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set mixer gain to {} dB", m_mixGain);
    return false;
  }

  rc = (airspy_error)airspy_set_vga_gain(m_dev, m_vgaGain);

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set VGA gain to {} dB", m_vgaGain);
    return false;
  }

  rc = (airspy_error)airspy_set_rf_bias(m_dev, (m_biasAnt ? 1 : 0));

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set bias antenna to {}", m_biasAnt);
    return false;
  }

  rc = (airspy_error)airspy_set_lna_agc(m_dev, (m_lnaAGC ? 1 : 0));

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set LNA AGC to {}", m_lnaAGC);
    return false;
  }

  rc = (airspy_error)airspy_set_mixer_agc(m_dev, (m_mixAGC ? 1 : 0));

  if (rc != AIRSPY_SUCCESS) {
    m_error = fmt::format("Could not set mixer AGC to {}", m_mixAGC);
    return false;
  }

  return true;
}

bool AirspySource::configure(std::string configurationStr) {
  int sampleRateIndex = 0;
  uint32_t frequency = 100000000;
  int lnaGain = 8;
  int mixGain = 0;
  int vgaGain = 10;
  bool antBias = false;
  bool lnaAGC = false;
  bool mixAGC = false;
  ConfigParser cp;
  ConfigParser::map_type m;

  cp.parse_config_string(configurationStr, m);
  if (m.find("srate") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: srate: {}", m["srate"]);
#endif
    if (strcasecmp(m["srate"].c_str(), "list") == 0) {
      m_error = "Available sample rates (Hz): " + m_sratesStr;
      return false;
    }

    int samplerate = 0;
    bool samplerate_ok =
        Utility::parse_int(m["srate"].c_str(), samplerate, true);
    m_sampleRate = static_cast<uint32_t>(samplerate);

    uint32_t i;
    for (i = 0; i < m_srates.size(); i++) {
      if (m_srates[i] == static_cast<int>(m_sampleRate)) {
        sampleRateIndex = i;
        break;
      }
    }
    if (i == m_srates.size() || !samplerate_ok) {
      m_error = "Invalid sample rate";
      m_sampleRate = 0;
      return false;
    }
  }

  if (m.find("freq") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: freq: {}", m["freq"]);
#endif
    int freq = 0;
    bool freq_ok = Utility::parse_int(m["freq"].c_str(), freq, true);
    frequency = static_cast<uint32_t>(freq);

    if (!freq_ok || (frequency < 24000000) || (frequency > 1800000000)) {
      m_error = "Invalid frequency";
      return false;
    }
  }

  if (m.find("lgain") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: lgain: {}", m["lgain"]);
#endif
    if (strcasecmp(m["lgain"].c_str(), "list") == 0) {
      m_error = "Available LNA gains (dB): " + m_lgainsStr;
      return false;
    }

    bool lgain_ok = Utility::parse_int(m["lgain"].c_str(), lnaGain);
    if (!lgain_ok ||
        find(m_lgains.begin(), m_lgains.end(), lnaGain) == m_lgains.end()) {
      m_error = "LNA gain not supported. Available gains (dB): " + m_lgainsStr;
      return false;
    }
  }

  if (m.find("mgain") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: mgain: {}", m["mgain"]);
#endif
    if (strcasecmp(m["mgain"].c_str(), "list") == 0) {
      m_error = "Available mixer gains (dB): " + m_mgainsStr;
      return false;
    }

    bool mgain_ok = Utility::parse_int(m["mgain"].c_str(), mixGain);
    if (!mgain_ok ||
        find(m_mgains.begin(), m_mgains.end(), mixGain) == m_mgains.end()) {
      m_error =
          "Mixer gain not supported. Available gains (dB): " + m_mgainsStr;
      return false;
    }
  }

  if (m.find("vgain") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: vgain: {}", m["vgain"]);
#endif
    if (strcasecmp(m["vgain"].c_str(), "list") == 0) {
      m_error = "Available VGA gains (dB): " + m_vgainsStr;
      return false;
    }

    bool vgain_ok = Utility::parse_int(m["vgain"].c_str(), vgaGain);
    if (!vgain_ok ||
        find(m_vgains.begin(), m_vgains.end(), vgaGain) == m_vgains.end()) {
      m_error = "VGA gain not supported. Available gains (dB): " + m_vgainsStr;
      return false;
    }
  }

  if (m.find("antbias") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: antbias");
#endif
    antBias = true;
  }

  if (m.find("lagc") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: lagc");
#endif
    lnaAGC = true;
  }

  if (m.find("magc") != m.end()) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::configure: magc");
#endif
    mixAGC = true;
  }

  m_confFreq = frequency;
  // tuner_freq shift no longer required
  double tuner_freq = frequency;
  return configure(sampleRateIndex, tuner_freq, antBias, lnaGain, mixGain,
                   vgaGain, lnaAGC, mixAGC);
}

bool AirspySource::start(DataBuffer<IQSample> *buf,
                         std::atomic_bool *stop_flag) {
  m_buf = buf;
  m_stop_flag = stop_flag;

  if (m_thread == 0) {
#ifdef DEBUG_AIRSPYSOURCE
    fmt::println(stderr, "AirspySource::start: starting");
#endif
    m_running = true;
    m_thread = new std::thread(run, m_dev, stop_flag);
    return *this;
  } else {
    fmt::println(stderr, "AirspySource::start: error");
    m_error = "Source thread already started";
    return false;
  }
}

void AirspySource::run(airspy_device *dev, std::atomic_bool *stop_flag) {
#ifdef DEBUG_AIRSPYSOURCE
  fmt::println(stderr, "AirspySource::run");
#endif
  airspy_error rc = (airspy_error)airspy_start_rx(dev, rx_callback, 0);

  if (rc == AIRSPY_SUCCESS) {
    while (!stop_flag->load() && (airspy_is_streaming(dev) == AIRSPY_TRUE)) {
      Utility::millisleep(100);
    }

    rc = (airspy_error)airspy_stop_rx(dev);

    if (rc != AIRSPY_SUCCESS) {
      fmt::println(stderr,
                   "AirspySource::run: Cannot stop Airspy HF Rx: {}: {}",
                   fmt::underlying(rc), airspy_error_name(rc));
    }
  } else {
    fmt::println(stderr, "AirspySource::run: Cannot start Airspy HF Rx: {}: {}",
                 fmt::underlying(rc), airspy_error_name(rc));
  }
}

bool AirspySource::stop() {
#ifdef DEBUG_AIRSPYSOURCE
  fmt::println(stderr, "AirspySource::stop");
#endif
  m_thread->join();
  delete m_thread;
  return true;
}

int AirspySource::rx_callback(airspy_transfer_t *transfer) {
  int len = transfer->sample_count * 2; // interleaved I/Q samples

  if (m_this) {
    m_this->callback((float *)transfer->samples, len);
  }

  return 0;
}

void AirspySource::callback(const float *buf, int len) {
  IQSampleVector iqsamples;

  iqsamples.resize(len / 2);

  for (int i = 0, j = 0; i < len; i += 2, j++) {
    float re = buf[i];
    float im = buf[i + 1];
    iqsamples[j] = IQSample(re, im);
  }

  m_buf->push(std::move(iqsamples));
}
