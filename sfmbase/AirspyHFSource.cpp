// airspyhf-fmradion
// Software decoder for FM broadcast radio with Airspy HF
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

#include <cstdlib>
#include <cstring>
#include <format>
#include <print>
#include <thread>
#include <unistd.h>
#include <utility>

#include "AirspyHFSource.h"
#include "ConfigParser.h"
#include "Utility.h"

// #define DEBUG_AIRSPYHFSOURCE 1

AirspyHFSource *AirspyHFSource::m_this = 0;

// Open Airspy HF device.
AirspyHFSource::AirspyHFSource(int dev_index)
    : m_dev(0), m_sampleRate(0), m_frequency(0), m_running(false), m_thread(0) {
  // Get library version number first.
  airspyhf_lib_version(&m_libv);
#ifdef DEBUG_AIRSPYHFSOURCE
  std::println(stderr, "AirspyHF Library Version: {}.{}.{}",
               m_libv.major_version, m_libv.minor_version, m_libv.revision);
#endif

  // Scan all devices, return how many are attached.
  m_ndev = airspyhf_list_devices(0, 0);
  if (m_ndev <= 0) {
    m_error = "No Airspy HF device found";
    m_dev = 0;
    m_this = this;
    return;
  }

  // List all available devices.
  m_serials.resize(m_ndev);
  if (m_ndev != airspyhf_list_devices(m_serials.data(), m_ndev)) {
    m_error = "Failed to obtain Airspy HF device serial numbers";
    m_dev = 0;
    m_this = this;
    return;
  }

  // Open the matched device.
  airspyhf_error rc =
      (airspyhf_error)airspyhf_open_sn(&m_dev, m_serials[dev_index]);
  if (rc != AIRSPYHF_SUCCESS) {
    m_error = std::format(
        "Failed to open Airspy HF device for the first time at device index {}",
        dev_index);
    m_dev = 0;
  }

  if (m_dev) {
    uint32_t nbSampleRates;
    uint32_t *sampleRates;

    airspyhf_get_samplerates(m_dev, &nbSampleRates, 0);
    sampleRates = new uint32_t[nbSampleRates];
    airspyhf_get_samplerates(m_dev, sampleRates, nbSampleRates);
#ifdef DEBUG_AIRSPYHFSOURCE
    std::println(stderr, "nbSampleRates = {}", nbSampleRates);
    std::println(stderr, "sampleRates[0] = {}", sampleRates[0]);
#endif

    if (nbSampleRates == 0) {
      m_error = "Failed to get Airspy HF device sample rate list";
      airspyhf_close(m_dev);
      m_dev = 0;
    } else {
      for (uint32_t i = 0; i < nbSampleRates; i++) {
        m_srates.push_back(sampleRates[i]);
      }
    }

    delete[] sampleRates;

    m_sratesStr = std::format("{:n}", m_srates);
  }

  m_this = this;
}

AirspyHFSource::~AirspyHFSource() {
#ifdef DEBUG_AIRSPYHFSOURCE
  std::println(stderr, "AirspyHFSource::~AirspyHFSource()");
#endif
  if (m_dev) {
    airspyhf_close(m_dev);
  }
  m_this = 0;
}

void AirspyHFSource::get_device_names(std::vector<std::string> &devices) {
  int ndev;
  std::vector<uint64_t> serials;

  // Scan all devices, return how many are attached.
  ndev = airspyhf_list_devices(0, 0);
#ifdef DEBUG_AIRSPYHFSOURCE
  std::println(stderr, "AirspyHFSource::get_device_names: "
                       "try to get available device numbers");
#endif
  if (ndev <= 0) {
    std::println(stderr,
                 "AirspyHFSource::get_device_names: no available device");
  }
  // List all available devices.
  serials.resize(ndev);
  if (ndev != airspyhf_list_devices(serials.data(), ndev)) {
    std::println(stderr, "AirspyHFSource::get_device_names: "
                         "unable to obtain device list");
  } else {
    // Use obtained info during AirspyHFSource object construction.
    for (int i = 0; i < ndev; i++) {
      std::string devname_ostr = std::format("Serial {:08x}", serials[i]);
      devices.push_back(devname_ostr);
    }
#ifdef DEBUG_AIRSPYHFSOURCE
    std::println(stderr,
                 "AirspyHFSource::get_device_names: "
                 "enumerated {} device(s)",
                 ndev);
#endif
  }
}

std::uint32_t AirspyHFSource::get_sample_rate() { return m_sampleRate; }

std::uint32_t AirspyHFSource::get_frequency() { return m_frequency; }

bool AirspyHFSource::is_low_if() { return m_low_if; }

void AirspyHFSource::print_specific_parms() {}

bool AirspyHFSource::configure(int sampleRateIndex, uint8_t hfAttLevel,
                               uint32_t frequency) {
  airspyhf_error rc;

  if (!m_dev) {
    return false;
  }

  uint32_t sampleRate = m_srates[sampleRateIndex];

  rc = (airspyhf_error)airspyhf_set_samplerate(m_dev, sampleRate);

  if (rc != AIRSPYHF_SUCCESS) {
    m_error =
        std::format("Could not set center sample rate to {} Hz", sampleRate);
    return false;
  } else {
    m_sampleRate = sampleRate;
  }

  int low_if = airspyhf_is_low_if(m_dev);
  if (low_if != 0) {
    m_low_if = true;
  } else {
    m_low_if = false;
  }

  // Shift down frequency by Fs/4 if NOT using low_if
  if (m_low_if) {
    m_frequency = frequency;
  } else {
    m_frequency = frequency - 0.25 * m_sampleRate;
  }

  rc = (airspyhf_error)airspyhf_set_freq(m_dev,
                                         static_cast<uint32_t>(m_frequency));

  if (rc != AIRSPYHF_SUCCESS) {
    m_error =
        std::format("Could not set center frequency to {} Hz", m_frequency);
    return false;
  }

  if (hfAttLevel > 0) {

    rc = (airspyhf_error)airspyhf_set_hf_agc(m_dev, 0);

    if (rc != AIRSPYHF_SUCCESS) {
      m_error = "Could not turn off HF AGC";
      return false;
    }

    rc = (airspyhf_error)airspyhf_set_hf_att(m_dev, hfAttLevel);

    if (rc != AIRSPYHF_SUCCESS) {
      m_error = std::format("Could not set HF attenuation level to {} dB",
                            hfAttLevel);
      return false;
    }

  } else {

    rc = (airspyhf_error)airspyhf_set_hf_agc(m_dev, 1);

    if (rc != AIRSPYHF_SUCCESS) {
      m_error = "Could not turn on HF AGC";
      return false;
    }

    rc = (airspyhf_error)airspyhf_set_hf_att(m_dev, 0);

    if (rc != AIRSPYHF_SUCCESS) {
      m_error = "Could not set HF attenuation level to zero dB";
      return false;
    }
  }

  return true;
}

int32_t AirspyHFSource::check_sampleRateIndex(uint32_t sampleRate) {
  uint32_t i;
  for (i = 0; i < m_srates.size(); i++) {
    if (m_srates[i] == static_cast<int>(sampleRate)) {
      return static_cast<int32_t>(i);
    }
  }
  // Unable to find
  return -1;
}

bool AirspyHFSource::configure(std::string configurationStr) {
  int sampleRateIndex;
  uint32_t frequency = 100000000;
  uint8_t hfAttLevel = 0;

  // Use 384ksps as the default value for the efficient FM broadcast
  // reception.
  sampleRateIndex = check_sampleRateIndex(384000);
  if (sampleRateIndex == -1) {
    m_error = "Invalid sample rate in AirspyHFSource::configure initialization";
    m_sampleRate = 0;
    return false;
  }

  ConfigParser cp;
  ConfigParser::map_type m;

  cp.parse_config_string(configurationStr, m);
  if (m.find("srate") != m.end()) {
#ifdef DEBUG_AIRSPYHFSOURCE
    std::println(stderr, "AirspyHFSource::configure: srate: {}", m["srate"]);
#endif
    if (strcasecmp(m["srate"].c_str(), "list") == 0) {
      m_error = "Available sample rates (Hz): " + m_sratesStr;
      return false;
    }

    int samplerate = 0;
    bool samplerate_ok =
        Utility::parse_int(m["srate"].c_str(), samplerate, true);
    m_sampleRate = static_cast<uint32_t>(samplerate);
    sampleRateIndex = check_sampleRateIndex(m_sampleRate);
    if (sampleRateIndex == -1 || !samplerate_ok) {
      m_error = "Invalid sample rate";
      m_sampleRate = 0;
      return false;
    }
  }

  if (m.find("freq") != m.end()) {
#ifdef DEBUG_AIRSPYHFSOURCE
    std::println(stderr, "AirspyHFSource::configure: freq: {}", m["freq"]);
#endif

    int freq = 0;
    bool freq_ok = Utility::parse_int(m["freq"].c_str(), freq, true);
    frequency = static_cast<uint32_t>(freq);

    if (!freq_ok || ((frequency > 31000000) && (frequency < 60000000)) ||
        (frequency > 260000000)) {
      m_error = "Invalid frequency";
      return false;
    }
  }

  if (m.find("hf_att") != m.end()) {
#ifdef DEBUG_AIRSPYHFSOURCE
    std::println(stderr, "AirspyHFSource::configure: hf_att: {}", m["hf_att"]);
#endif

    int attlevel = 0;
    bool attlevel_ok = Utility::parse_int(m["hf_att"].c_str(), attlevel);
    hfAttLevel = static_cast<uint8_t>(attlevel);
    if (!attlevel_ok || (hfAttLevel > 8) || (hfAttLevel < 0)) {
      m_error = "Invalid HF att level";
      return false;
    }
  }

  m_confFreq = frequency;
  return configure(sampleRateIndex, hfAttLevel, frequency);
}

bool AirspyHFSource::start(DataBuffer<IQSample> *buf,
                           std::atomic_bool *stop_flag) {
  m_buf = buf;
  m_stop_flag = stop_flag;

  if (m_thread == 0) {
#ifdef DEBUG_AIRSPYHFSOURCE
    std::println(stderr, "AirspyHFSource::start: starting");
#endif
    m_running = true;
    m_thread = new std::thread(run, m_dev, m_stop_flag);
    return true;
  } else {
    std::println(stderr, "AirspyHFSource::start: error");
    m_error = "Source thread already started";
    return false;
  }
}

void AirspyHFSource::run(airspyhf_device *dev, std::atomic_bool *stop_flag) {
#ifdef DEBUG_AIRSPYHFSOURCE
  std::println(stderr, "AirspyHFSource::run");
#endif
  airspyhf_error rc = (airspyhf_error)airspyhf_start(dev, rx_callback, nullptr);

  if (rc == AIRSPYHF_SUCCESS) {
    while (!stop_flag->load() && (airspyhf_is_streaming(dev) == true)) {
      Utility::millisleep(100);
    }
  } else {
    std::println(stderr, "AirspyHFSource::run: Cannot start Airspy HF Rx: {}",
                 std::to_underlying(rc));
  }
}

bool AirspyHFSource::stop() {
#ifdef DEBUG_AIRSPYHFSOURCE
  std::println(stderr, "AirspyHFSource::stop");
#endif
  airspyhf_error rc = (airspyhf_error)airspyhf_stop(m_dev);
  if (rc != AIRSPYHF_SUCCESS) {
    std::println(stderr, "AirspyHFSource::run: Cannot stop Airspy HF Rx: {}",
                 std::to_underlying(rc));
  }
  m_thread->join();
  delete m_thread;
  return true;
}

int AirspyHFSource::rx_callback(airspyhf_transfer_t *transfer) {
  int len = transfer->sample_count * 2; // interleaved I/Q samples

  if (m_this) {
    m_this->callback((float *)transfer->samples, len);
  }

  return 0;
}

void AirspyHFSource::callback(const float *buf, int len) {
  IQSampleVector iqsamples;

  iqsamples.resize(len / 2);

  for (int i = 0, j = 0; i < len; i += 2, j++) {
    float re = buf[i];
    float im = buf[i + 1];
    iqsamples[j] = IQSample(re, im);
  }

  m_buf->push(std::move(iqsamples));
}
