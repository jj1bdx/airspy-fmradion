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
#include <climits>
#include <cstring>
#include <fmt/base.h>
#include <iomanip>
#include <iostream>
#include <rtl-sdr.h>
#include <sstream>
#include <thread>
#include <unistd.h>

#include "ConfigParser.h"
#include "RtlSdrSource.h"
#include "Utility.h"

RtlSdrSource *RtlSdrSource::m_this = 0;

// Open RTL-SDR device.
RtlSdrSource::RtlSdrSource(int dev_index)
    : m_dev(0), m_block_length(default_block_length), m_thread(0) {
  int r;

  const char *devname = rtlsdr_get_device_name(dev_index);
  if (devname != nullptr) {
    m_devname = devname;
  }

  r = rtlsdr_open(&m_dev, dev_index);

  if (r < 0) {
    m_error = "Failed to open RTL-SDR device (";
    m_error += strerror(-r);
    m_error += ")";
  } else {
    m_gains = get_tuner_gains();
    std::ostringstream gains_ostr;

    gains_ostr << std::fixed << std::setprecision(1);

    for (int g : m_gains) {
      gains_ostr << 0.1 * g << " ";
    }

    m_gainsStr = gains_ostr.str();
  }

  m_this = this;
}

// Close RTL-SDR device.
RtlSdrSource::~RtlSdrSource() {
  if (m_dev) {
    rtlsdr_close(m_dev);
  }

  m_this = 0;
}

bool RtlSdrSource::configure(std::string configurationStr) {

  uint32_t sample_rate = 1152000;
  uint32_t frequency = 100000000;
  int tuner_gain = INT_MIN;
  int block_length = default_block_length;
  bool agcmode = false;
  bool antbias = false;

  ConfigParser cp;
  ConfigParser::map_type m;

  cp.parse_config_string(configurationStr, m);
  if (m.find("srate") != m.end()) {
    int samplerate = 0;
    bool samplerate_ok =
        Utility::parse_int(m["srate"].c_str(), samplerate, true);
    sample_rate = static_cast<uint32_t>(samplerate);
    if (!samplerate_ok || (sample_rate < 900001) || (sample_rate > 3200000)) {
      m_error = "Invalid sample rate";
      return false;
    }
    std::cerr << "RtlSdrSource::configure: srate: " << sample_rate << std::endl;
  }

  if (m.find("freq") != m.end()) {
    int freq = 0;
    bool freq_ok = Utility::parse_int(m["freq"].c_str(), freq, true);
    frequency = static_cast<uint32_t>(freq);
    if (!freq_ok || (frequency < 10000000) || (frequency > 2200000000)) {
      m_error = "Invalid frequency";
      return false;
    }
    std::cerr << "RtlSdrSource::configure: freq: " << frequency << std::endl;
  }

  if (m.find("gain") != m.end()) {
    std::string gain_str = m["gain"];
    std::cerr << "RtlSdrSource::configure: gain: " << gain_str << std::endl;

    if (strcasecmp(gain_str.c_str(), "auto") == 0) {
      tuner_gain = INT_MIN;
    } else if (strcasecmp(gain_str.c_str(), "list") == 0) {
      m_error = "Available gains (dB): " + m_gainsStr;
      return false;
    } else {
      double tmpgain;

      if (!Utility::parse_dbl(gain_str.c_str(), tmpgain)) {
        m_error = "Invalid gain";
        return false;
      } else {
        long int tmpgain2 = lrint(tmpgain * 10);

        if (tmpgain2 <= INT_MIN || tmpgain2 >= INT_MAX) {
          m_error = "Invalid gain";
          return false;
        } else {
          tuner_gain = tmpgain2;

          if (find(m_gains.begin(), m_gains.end(), tuner_gain) ==
              m_gains.end()) {
            m_error = "Gain not supported. Available gains (dB): " + m_gainsStr;
            return false;
          }
        }
      }
    }
  } // gain

  if (m.find("blklen") != m.end()) {
    bool blklen_ok = Utility::parse_int(m["blklen"].c_str(), block_length);
    if (!blklen_ok) {
      std::cerr << "RtlSdrSource::configure: invalid blklen" << std::endl;
      return false;
    }
    std::cerr << "RtlSdrSource::configure: blklen: " << block_length
              << std::endl;
  }

  if (m.find("agc") != m.end()) {
    std::cerr << "RtlSdrSource::configure: agc" << std::endl;
    agcmode = true;
  }

  if (m.find("antbias") != m.end()) {
    std::cerr << "RtlSdrSource::configure: agc" << std::endl;
    antbias = true;
  }

  // Intentionally tune at a higher frequency to avoid DC offset.
  m_confFreq = frequency;
  m_confAgc = agcmode;
  double tuner_freq = frequency - sample_rate / 4.0;

  return configure(sample_rate, tuner_freq, tuner_gain, block_length, agcmode,
                   antbias);
}

// Configure RTL-SDR tuner and prepare for streaming.
bool RtlSdrSource::configure(uint32_t sample_rate, uint32_t frequency,
                             int tuner_gain, int block_length, bool agcmode,
                             bool antbias) {
  int r;

  if (!m_dev) {
    return false;
  }

  r = rtlsdr_set_sample_rate(m_dev, sample_rate);
  if (r < 0) {
    m_error = "rtlsdr_set_sample_rate failed";
    return false;
  }

  r = rtlsdr_set_center_freq(m_dev, frequency);
  if (r < 0) {
    m_error = "rtlsdr_set_center_freq failed";
    return false;
  }

  if (tuner_gain == INT_MIN) {
    r = rtlsdr_set_tuner_gain_mode(m_dev, 0);
    if (r < 0) {
      m_error = "rtlsdr_set_tuner_gain_mode could not set automatic gain";
      return false;
    }
  } else {
    r = rtlsdr_set_tuner_gain_mode(m_dev, 1);
    if (r < 0) {
      m_error = "rtlsdr_set_tuner_gain_mode could not set manual gain";
      return false;
    }

    r = rtlsdr_set_tuner_gain(m_dev, tuner_gain);
    if (r < 0) {
      m_error = "rtlsdr_set_tuner_gain failed";
      return false;
    }
  }

  // set RTL AGC mode
  r = rtlsdr_set_agc_mode(m_dev, int(agcmode));
  if (r < 0) {
    m_error = "rtlsdr_set_agc_mode failed";
    return false;
  }

  // set RTL antenna Bias Tee
  // Note: not all device supports this
  r = rtlsdr_set_bias_tee(m_dev, int(antbias));
  if (r < 0) {
    m_error = "rtlsdr_set_bias_tee failed";
    return false;
  }

  // set block length
  m_block_length = (block_length < 4096)          ? 4096
                   : (block_length > 1024 * 1024) ? 1024 * 1024
                                                  : block_length;
  m_block_length -= m_block_length % 4096;

  // reset buffer to start streaming
  if (rtlsdr_reset_buffer(m_dev) < 0) {
    m_error = "rtlsdr_reset_buffer failed";
    return false;
  }

  return true;
}

// Return current sample frequency in Hz.
uint32_t RtlSdrSource::get_sample_rate() {
  return rtlsdr_get_sample_rate(m_dev);
}

// Return device current center frequency in Hz.
uint32_t RtlSdrSource::get_frequency() { return rtlsdr_get_center_freq(m_dev); }

// Do not assume low-IF for RTL-SDR (Zero-IF by definition)
bool RtlSdrSource::is_low_if() { return false; }

void RtlSdrSource::print_specific_parms() {
  int lnagain = get_tuner_gain();

  if (lnagain == INT_MIN) {
    fmt::println(stderr, "LNA gain:          auto");
  } else {
    fmt::println(stderr, "LNA gain:          {:.1f} dB", 0.1 * lnagain);
  }

  fmt::println(stderr, "RTL AGC mode:      {}",
               m_confAgc ? "enabled" : "disabled");
}

// Return current tuner gain in units of 0.1 dB.
int RtlSdrSource::get_tuner_gain() { return rtlsdr_get_tuner_gain(m_dev); }

// Return a list of supported tuner gain settings in units of 0.1 dB.
std::vector<int> RtlSdrSource::get_tuner_gains() {
  int num_gains = rtlsdr_get_tuner_gains(m_dev, nullptr);
  if (num_gains <= 0) {
    return std::vector<int>();
  }

  std::vector<int> gains(num_gains);
  if (rtlsdr_get_tuner_gains(m_dev, gains.data()) != num_gains) {
    return std::vector<int>();
  }

  return gains;
}

bool RtlSdrSource::start(DataBuffer<IQSample> *buf,
                         std::atomic_bool *stop_flag) {
  m_buf = buf;
  m_stop_flag = stop_flag;

  if (m_thread == 0) {
    m_thread = new std::thread(run);
    return true;
  } else {
    m_error = "Source thread already started";
    return false;
  }
}

bool RtlSdrSource::stop() {
  if (m_thread) {
    m_thread->join();
    delete m_thread;
    m_thread = 0;
  }

  return true;
}

void RtlSdrSource::run() {
  IQSampleVector iqsamples;

  while (!m_this->m_stop_flag->load() && get_samples(&iqsamples)) {
    m_this->m_buf->push(std::move(iqsamples));
  }
}

// Fetch a bunch of samples from the device.
bool RtlSdrSource::get_samples(IQSampleVector *samples) {
  int r, n_read;

  if (!m_this->m_dev) {
    return false;
  }

  if (!samples) {
    return false;
  }

  std::vector<uint8_t> buf(2 * m_this->m_block_length);

  r = rtlsdr_read_sync(m_this->m_dev, buf.data(), 2 * m_this->m_block_length,
                       &n_read);

  if (r < 0) {
    m_this->m_error = "rtlsdr_read_sync failed";
    return false;
  }

  if (n_read != 2 * m_this->m_block_length) {
    m_this->m_error = "short read, samples lost";
    return false;
  }

  samples->resize(m_this->m_block_length);

  for (int i = 0; i < m_this->m_block_length; i++) {
    int32_t re = buf[2 * i];
    int32_t im = buf[2 * i + 1];
    (*samples)[i] = IQSample((re - 128) / IQSample::value_type(128),
                             (im - 128) / IQSample::value_type(128));
  }

  return true;
}

// Return a list of supported devices.
void RtlSdrSource::get_device_names(std::vector<std::string> &devices) {
  char manufacturer[256], product[256], serial[256];
  int device_count = rtlsdr_get_device_count();

  if (device_count > 0) {
    devices.resize(device_count);
  }

  devices.clear();

  for (int i = 0; i < device_count; i++) {
    if (!rtlsdr_get_device_usb_strings(i, manufacturer, product, serial)) {
      std::ostringstream name_ostr;
      name_ostr << manufacturer << " " << product << " " << serial;
      devices.push_back(name_ostr.str());
    }
  }
}

/* end */
