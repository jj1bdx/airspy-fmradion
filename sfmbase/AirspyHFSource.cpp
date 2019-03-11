// airspyhf-fmradion
// Software decoder for FM broadcast radio with Airspy HF
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019 Kenji Rikitake, JJ1BDX
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
#include <iomanip>
#include <iostream>
#include <sstream>
#include <thread>

#include "AirspyHFSource.h"
#include "parsekv.h"
#include "util.h"

// #define DEBUG_AIRSPYHFSOURCE 1

AirspyHFSource *AirspyHFSource::m_this = 0;

// Open Airspy HF device.
AirspyHFSource::AirspyHFSource(int dev_index)
    : m_dev(0), m_sampleRate(768000), m_frequency(100000000), m_running(false),
      m_thread(0) {
  for (int i = 0; i < AIRSPYHF_MAX_DEVICE; i++) {
    airspyhf_error rc = (airspyhf_error)airspyhf_open(&m_dev);

    if (rc == AIRSPYHF_SUCCESS) {
      if (i == dev_index) {
        break;
      }
    } else {
      std::ostringstream err_ostr;
      err_ostr << "Failed to open Airspy HF device at sequence " << i;
      m_error = err_ostr.str();
      m_dev = 0;
    }
  }

  if (m_dev) {
    uint32_t nbSampleRates;
    uint32_t *sampleRates;

    airspyhf_get_samplerates(m_dev, &nbSampleRates, 0);
    sampleRates = new uint32_t[nbSampleRates];
    airspyhf_get_samplerates(m_dev, sampleRates, nbSampleRates);
#ifdef DEBUG_AIRSPYHFSOURCE
    std::cerr << "nbSampleRates = " << nbSampleRates << std::endl;
    std::cerr << "nbSampleRates[0] = " << sampleRates[0] << std::endl;
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

    std::ostringstream srates_ostr;

    for (int s : m_srates) {
      srates_ostr << s << " ";
    }

    m_sratesStr = srates_ostr.str();
  }

  std::ostringstream bwfilt_ostr;
  bwfilt_ostr << std::fixed << std::setprecision(2);

  m_this = this;
}

AirspyHFSource::~AirspyHFSource() {
  if (m_dev) {
    airspyhf_close(m_dev);
  }
  m_this = 0;
}

void AirspyHFSource::get_device_names(std::vector<std::string> &devices) {
  airspyhf_device *airspyhf_ptr;
  airspyhf_read_partid_serialno_t read_partid_serialno;
  uint32_t serial_msb = 0;
  uint32_t serial_lsb = 0;
  airspyhf_error rc;
  int i;

  for (i = 0; i < AIRSPYHF_MAX_DEVICE; i++) {
    rc = (airspyhf_error)airspyhf_open(&airspyhf_ptr);
#ifdef DEBUG_AIRSPYHFSOURCE
    std::cerr << "AirspyHFSource::get_device_names: try to get device " << i
              << " serial number" << std::endl;
#endif

    if (rc == AIRSPYHF_SUCCESS) {
#ifdef DEBUG_AIRSPYHFSOURCE
      std::cerr << "AirspySHFource::get_device_names: device " << i
                << " open OK" << std::endl;
#endif

      rc = (airspyhf_error)airspyhf_board_partid_serialno_read(
          airspyhf_ptr, &read_partid_serialno);

      if (rc == AIRSPYHF_SUCCESS) {
        serial_msb = read_partid_serialno.serial_no[0];
        serial_lsb = read_partid_serialno.serial_no[1];
        std::ostringstream devname_ostr;
        devname_ostr << "Serial " << std::hex << std::setw(8)
                     << std::setfill('0') << serial_msb << serial_lsb;
        devices.push_back(devname_ostr.str());
      } else {
        std::cerr << "AirspyHFSource::get_device_names: failed to get device "
                  << i << " serial number: " << rc << std::endl;
      }

      airspyhf_close(airspyhf_ptr);
    } else {
#ifdef DEBUG_AIRSPYHFSOURCE
      std::cerr << "AirspyHFSource::get_device_names: enumerated " << i
                << " Airspy devices: " << std::endl;
#endif
      break; // finished
    }
  }
}

std::uint32_t AirspyHFSource::get_sample_rate() { return m_sampleRate; }

std::uint32_t AirspyHFSource::get_frequency() { return m_frequency; }

void AirspyHFSource::print_specific_parms() {}

bool AirspyHFSource::configure(int sampleRateIndex, uint32_t frequency) {

  m_frequency = frequency;

  airspyhf_error rc;

  if (!m_dev) {
    return false;
  }

  rc = (airspyhf_error)airspyhf_set_freq(m_dev,
                                         static_cast<uint32_t>(m_frequency));

  if (rc != AIRSPYHF_SUCCESS) {
    std::ostringstream err_ostr;
    err_ostr << "Could not set center frequency to " << m_frequency << " Hz";
    m_error = err_ostr.str();
    return false;
  }

  rc = (airspyhf_error)airspyhf_set_samplerate(
      m_dev, static_cast<uint32_t>(sampleRateIndex));

  if (rc != AIRSPYHF_SUCCESS) {
    std::ostringstream err_ostr;
    err_ostr << "Could not set center sample rate to "
             << m_srates[sampleRateIndex] << " Hz";
    m_error = err_ostr.str();
    return false;
  } else {
    m_sampleRate = m_srates[sampleRateIndex];
  }

  return true;
}

bool AirspyHFSource::configure(std::string configurationStr) {
  namespace qi = boost::spirit::qi;
  std::string::iterator begin = configurationStr.begin();
  std::string::iterator end = configurationStr.end();

  int sampleRateIndex = 0;
  uint32_t frequency = 100000000;

  m_sampleRate = 768000;

  parsekv::key_value_sequence<std::string::iterator> p;
  parsekv::pairs_type m;

  if (!qi::parse(begin, end, p, m)) {
    m_error = "Configuration parsing failed\n";
    return false;
  } else {
    if (m.find("srate") != m.end()) {
#ifdef DEBUG_AIRSPYHFSOURCE
      std::cerr << "AirspyHFSource::configure: srate: " << m["srate"]
                << std::endl;
#endif
      if (strcasecmp(m["srate"].c_str(), "list") == 0) {
        m_error = "Available sample rates (Hz): " + m_sratesStr;
        return false;
      }

      m_sampleRate = atoi(m["srate"].c_str());
      uint32_t i;

      for (i = 0; i < m_srates.size(); i++) {
        if (m_srates[i] == static_cast<int>(m_sampleRate)) {
          sampleRateIndex = i;
          break;
        }
      }

      if (i == m_srates.size()) {
        m_error = "Invalid sample rate";
        m_sampleRate = 0;
        return false;
      }
    }

    if (m.find("freq") != m.end()) {
#ifdef DEBUG_AIRSPYHFSOURCE
      std::cerr << "AirspyHFSource::configure: freq: " << m["freq"]
                << std::endl;
#endif
      frequency = atoi(m["freq"].c_str());

      if ((frequency < 60000000) || (frequency > 260000000)) {
        m_error = "Invalid frequency";
        return false;
      }
    }
  }

  m_confFreq = frequency;
  // tuned frequency is up Fs/4 (downconverted in FmDecode)
  double tuner_freq = frequency - 0.25 * m_srates[sampleRateIndex];
  return configure(sampleRateIndex, tuner_freq);
}

bool AirspyHFSource::start(DataBuffer<IQSample> *buf,
                           std::atomic_bool *stop_flag) {
  m_buf = buf;
  m_stop_flag = stop_flag;

  if (m_thread == 0) {
#ifdef DEBUG_AIRSPYHFSOURCE
    std::cerr << "AirspySource::start: starting" << std::endl;
#endif
    m_running = true;
    m_thread = new std::thread(run, m_dev, stop_flag);
    sleep(1);
    return *this;
  } else {
    std::cerr << "AirspyHFSource::start: error" << std::endl;
    m_error = "Source thread already started";
    return false;
  }
}

void AirspyHFSource::run(airspyhf_device *dev, std::atomic_bool *stop_flag) {
#ifdef DEBUG_AIRSPYSOURCE
  std::cerr << "AirspyHFSource::run" << std::endl;
#endif
  airspyhf_error rc = (airspyhf_error)airspyhf_start(dev, rx_callback, NULL);

  if (rc == AIRSPYHF_SUCCESS) {
    while (!stop_flag->load() && (airspyhf_is_streaming(dev) == true)) {
      sleep(1);
    }

    rc = (airspyhf_error)airspyhf_stop(dev);

    if (rc != AIRSPYHF_SUCCESS) {
      std::cerr << "AirspyHFSource::run: Cannot stop Airspy HF Rx: " << rc
                << std::endl;
    }
  } else {
    std::cerr << "AirspyHFSource::run: Cannot start Airspy HF Rx: " << rc
              << std::endl;
  }
}

bool AirspyHFSource::stop() {
#ifdef DEBUG_AIRSPYSOURCE
  std::cerr << "AirspyHFSource::stop" << std::endl;
#endif
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

  m_buf->push(move(iqsamples));
}
