// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2026 Kenji Rikitake, JJ1BDX
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
#include <fmt/format.h>
#include <fmt/ranges.h>
#include <memory>
#include <ranges>
#include <rtl-sdr.h>
#include <thread>
#include <unistd.h>

#include "ConfigParser.h"
#include "IfResampler.h"
#include "RtlSdrSource.h"
#include "Utility.h"

std::atomic<RtlSdrSource *> RtlSdrSource::m_this{nullptr};

// Open RTL-SDR device.
RtlSdrSource::RtlSdrSource(int dev_index)
    : m_dev(0), m_block_length(default_block_length), m_thread(nullptr) {
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
    const auto divide_by_ten = [](int v) {
      return static_cast<double>(static_cast<double>(v) * 0.1);
    };
    m_gainsStr = fmt::format(
        "{:.1f}",
        fmt::join(std::views::transform(m_gains, divide_by_ten), ", "));
  }

  m_this = this;
}

// Close RTL-SDR device.
RtlSdrSource::~RtlSdrSource() {
  // Cancel the async loop and join the source thread (if still running)
  // before closing the device, so rtlsdr_close() cannot race a live
  // rtlsdr_read_async() call.
  stop();

  if (m_dev) {
    rtlsdr_close(m_dev);
  }

  m_this = nullptr;
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
    fmt::println(stderr, "RtlSdrSource::configure: srate: {}", sample_rate);
  }

  if (m.find("freq") != m.end()) {
    int freq = 0;
    bool freq_ok = Utility::parse_int(m["freq"].c_str(), freq, true);
    frequency = static_cast<uint32_t>(freq);
    if (!freq_ok || (frequency < 10000000) || (frequency > 2200000000)) {
      m_error = "Invalid frequency";
      return false;
    }
    fmt::println(stderr, "RtlSdrSource::configure: freq: {}", frequency);
  }

  if (m.find("gain") != m.end()) {
    std::string gain_str = m["gain"];
    fmt::println(stderr, "RtlSdrSource::configure: gain: {}", gain_str);

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
      fmt::println(stderr, "RtlSdrSource::configure: invalid blklen");
      return false;
    }
    fmt::println(stderr, "RtlSdrSource::configure: blklen: {}", block_length);
  }

  if (m.find("agc") != m.end()) {
    fmt::println(stderr, "RtlSdrSource::configure: agc");
    agcmode = true;
  }

  if (m.find("antbias") != m.end()) {
    fmt::println(stderr, "RtlSdrSource::configure: antbias");
    antbias = true;
  }

  // Intentionally tune at a higher frequency to avoid DC offset.
  m_confFreq = frequency;
  m_confAgc = agcmode;
  // Compute the Fs/4 down-shift in integer arithmetic to avoid a
  // double->uint32_t narrowing (regression of V20 hardening).
  const uint32_t shift = sample_rate / 4u;
  if (frequency < shift) {
    m_error = "Frequency too low for Fs/4 down-shift";
    return false;
  }
  const uint32_t tuner_freq = frequency - shift;

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
  // Cap the block length at IfResampler::max_input_length (65536) so that
  // a source block can never exceed what the resampler chain can process
  // in one call. With the valid RTL-SDR sample rate range
  // [900001, 3200000] Hz, the IF resampler always downsamples
  // (ratio >= 900001/384000), so a 65536-sample block shrinks to at most
  // ~27963 samples, within AudioResampler::max_input_length (32768).
  m_block_length = (block_length < 4096) ? 4096
                   : (block_length > IfResampler::max_input_length)
                       ? IfResampler::max_input_length
                       : block_length;
  m_block_length -= m_block_length % 4096;
  // The rtlsdr_read_async() buffer size (2 * m_block_length bytes) is
  // therefore a multiple of 8192, satisfying the library requirement of
  // a multiple of 512 bytes.
  if (m_block_length != block_length) {
    fmt::println(stderr,
                 "RtlSdrSource::configure: blklen adjusted from {} to {}",
                 block_length, m_block_length);
  }

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
    m_thread = std::make_unique<std::thread>(run, m_dev, stop_flag);
    return true;
  } else {
    m_error = "Source thread already started";
    return false;
  }
}

bool RtlSdrSource::stop() {
  // Idempotent: only act when the source thread has not been joined yet,
  // so the destructor can call stop() unconditionally.
  if (m_thread) {
    // Force the blocked rtlsdr_read_async() call in run() to return
    // even if the caller did not already set *m_stop_flag,
    // so join() cannot deadlock. A negative return here only means
    // the async loop already ended, so it is not reported.
    if (m_dev) {
      rtlsdr_cancel_async(m_dev);
    }
    m_thread->join();
    m_thread.reset();
  }

  return true;
}

void RtlSdrSource::run(struct rtlsdr_dev *dev, std::atomic_bool *stop_flag) {
  RtlSdrSource *self = m_this.load();
  if (!self || !dev) {
    return;
  }

  // rtlsdr_read_async() blocks this thread inside the libusb event loop,
  // invoking rx_callback for each filled buffer, until rtlsdr_cancel_async()
  // is called from stop().
  int r = rtlsdr_read_async(dev, rx_callback, nullptr, 0,
                            static_cast<uint32_t>(2 * self->m_block_length));
  if (r < 0 && !stop_flag->load()) {
    fmt::println(stderr, "RtlSdrSource::run: rtlsdr_read_async failed: {}", r);
    self->m_error = "rtlsdr_read_async failed";
  }

  // Streaming has ended (cancellation, device failure, or an immediate
  // rtlsdr_read_async() error). Mark the end of the stream so a consumer
  // blocked in DataBuffer::pull() always wakes up and the main loop can
  // terminate and clean up.
  if (self->m_buf) {
    self->m_buf->push_end();
  }
}

void RtlSdrSource::rx_callback(unsigned char *buf, uint32_t len, void *ctx) {
  (void)ctx;
  RtlSdrSource *self = m_this.load();
  if (self) {
    self->callback(buf, len);
  }
}

// Convert a block of raw offset-binary I/Q bytes and push it
// to the output buffer.
void RtlSdrSource::callback(const unsigned char *buf, std::size_t len) {
  // Keep pushing until the async loop is cancelled: the consumer may be
  // blocked in DataBuffer::pull() and relies on these pushes (and the
  // final push_end() in run()) to wake up and observe the stop flag.
  //
  // A cancelled in-flight transfer may deliver a short or empty buffer;
  // this is benign, not a device error.
  if (len < 2) {
    return;
  }

  const std::size_t nsamples = len / 2;
  IQSampleVector iqsamples(nsamples);

  for (std::size_t i = 0; i < nsamples; i++) {
    // RTL-SDR outputs offset-binary 8-bit: 0..255 with 128 = DC zero.
    int32_t re = static_cast<int32_t>(buf[2 * i]) - 128;
    int32_t im = static_cast<int32_t>(buf[2 * i + 1]) - 128;
    iqsamples[i] = IQSample(re / IQSample::value_type(128),
                            im / IQSample::value_type(128));
  }

  if (m_buf) {
    m_buf->push(std::move(iqsamples));
  }
}

// Return a list of supported devices.
void RtlSdrSource::get_device_names(std::vector<std::string> &devices) {
  // Buffer size matches librtlsdr's internal libusb_get_string_descriptor_ascii
  // limit; this coupling is load-bearing and undocumented in librtlsdr.
  static constexpr std::size_t USB_STRING_MAX = 256;
  char manufacturer[USB_STRING_MAX], product[USB_STRING_MAX],
      serial[USB_STRING_MAX];
  int device_count = rtlsdr_get_device_count();

  if (device_count > 0) {
    devices.reserve(static_cast<std::size_t>(device_count));
  }

  for (int i = 0; i < device_count; i++) {
    if (!rtlsdr_get_device_usb_strings(i, manufacturer, product, serial)) {
      std::string name = fmt::format("{} {} {}", manufacturer, product, serial);
      devices.push_back(name);
    }
  }
}

// end
