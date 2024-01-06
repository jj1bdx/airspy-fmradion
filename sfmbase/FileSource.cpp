// airspyhf-fmradion
// Software decoder for FM broadcast radio with Airspy HF
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019 Takehiro Sekine
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

#include <chrono>
#include <iostream>
#include <sstream>
#include <thread>

#include "ConfigParser.h"
#include "FileSource.h"
#include "Utility.h"

FileSource *FileSource::m_this = 0;

// Constructor
FileSource::FileSource(int dev_index)
    : m_sample_rate(default_sample_rate), m_frequency(default_frequency),
      m_zero_offset(false), m_block_length(default_block_length),
      m_sfp(nullptr), m_fmt_fn(nullptr), m_thread(0) {
  m_sfinfo = {0};
  m_this = this;
}

// Destructor
FileSource::~FileSource() {
  // Close sndfile.
  if (m_sfp) {
    sf_close(m_sfp);
    m_sfp = nullptr;
  }

  m_this = 0;
}

bool FileSource::configure(std::string configurationStr) {
  std::string filename;
  bool raw = false;
  FormatType format_type = FormatType::S16_LE;
  uint32_t sample_rate = default_sample_rate;
  uint32_t frequency = default_frequency;
  bool zero_offset = false;
  int block_length = default_block_length;

  bool srate_specified = false;

  ConfigParser cp;
  ConfigParser::map_type m;

  // filename
  cp.parse_config_string(configurationStr, m);
  if (m.find("filename") != m.end()) {
    std::cerr << "FileSource::configure: filename: " << m["filename"]
              << std::endl;
    filename = m["filename"];
  }

  // srate
  if (m.find("srate") != m.end()) {
    std::cerr << "FileSource::configure: srate: " << m["srate"] << std::endl;
    int samplerate = 0;
    bool samplerate_ok =
        Utility::parse_int(m["srate"].c_str(), samplerate, true);
    sample_rate = static_cast<uint32_t>(samplerate);
    srate_specified = true;
    if (!samplerate_ok) {
      std::cerr << "FileSource::configure: invalid samplerate" << std::endl;
      return false;
    }
  }

  // freq
  if (m.find("freq") != m.end()) {
    std::cerr << "FileSource::configure: freq: " << m["freq"] << std::endl;
    int freq = 0;
    bool freq_ok = Utility::parse_int(m["freq"].c_str(), freq, true);
    frequency = static_cast<uint32_t>(freq);
    if (!freq_ok) {
      std::cerr << "FileSource::configure: invalid frequency" << std::endl;
      return false;
    }
  }

  // blklen
  if (m.find("blklen") != m.end()) {
    std::cerr << "FileSource::configure: blklen: " << m["blklen"] << std::endl;
    bool blklen_ok = Utility::parse_int(m["blklen"].c_str(), block_length);
    if (!blklen_ok) {
      std::cerr << "FileSource::configure: invalid blklen" << std::endl;
      return false;
    }
  }

  // zero_offset
  if (m.find("zero_offset") != m.end()) {
    std::cerr << "FileSource::configure: zero_offset" << std::endl;
    zero_offset = true;
  }

  // format_type
  if (m.find("format") != m.end()) {
    if (m["format"] == "S8_LE") {
      format_type = FormatType::S8_LE;
    } else if (m["format"] == "S16_LE") {
      format_type = FormatType::S16_LE;
    } else if (m["format"] == "S24_LE") {
      format_type = FormatType::S24_LE;
    } else if (m["format"] == "U8_LE") {
      format_type = FormatType::U8_LE;
    } else if (m["format"] == "FLOAT") {
      format_type = FormatType::Float;
    } else {
      std::cerr << "FileSource::configure: format: " << m["format"]
                << " is not supported." << std::endl;
      std::cerr << "FileSource::configure: supported format is S8_LE, S16_LE, "
                   "S24_LE, U8_LE, FLOAT."
                << std::endl;
      return false;
    }
    std::cerr << "FileSource::configure: format: " << m["format"] << std::endl;
  }

  // raw
  if (m.find("raw") != m.end()) {
    std::cerr << "FileSource::configure: raw" << std::endl;
    // Check format_type. Try to apply S16_LE when no format specified.
    if (format_type == FormatType::Unknown) {
      std::cerr << "FileSource::configure: raw warn: no format specified. "
                   "apply S16_LE."
                << std::endl;
      format_type = FormatType::S16_LE;
    }
    // Check samplerate. Try to apply 384000Hz when no samplerate specified.
    if (!srate_specified) {
      std::cerr << "FileSource::configure: raw warn: no samplerate specified. "
                   "apply 384000."
                << std::endl;
    }
    raw = true;
  }

  // configure
  return configure(filename, raw, format_type, sample_rate, frequency,
                   zero_offset, block_length);
}

bool FileSource::configure(std::string fname, bool raw, FormatType format_type,
                           std::uint32_t sample_rate, std::uint32_t frequency,
                           bool zero_offset, int block_length) {
  m_devname = fname;
  m_sample_rate = sample_rate;
  m_frequency = frequency;
  m_zero_offset = zero_offset;
  m_block_length = block_length;

  // Fill sfinfo when raw is true;
  if (raw) {
    m_sfinfo.samplerate = (int)m_sample_rate;
    m_sfinfo.channels = 2;
    m_sfinfo.format = SF_FORMAT_RAW | to_sf_format(format_type);
  }

  // Open sndfile.
  m_sfp = sf_open(m_devname.c_str(), SFM_READ, &m_sfinfo);
  if (m_sfp == nullptr) {
    // Set m_error and return false if sf_open is failed.
    m_error = "Failed to open ";
    m_error += m_devname + " : " + sf_strerror(m_sfp);
    return false;
  }

  // Overwrite sample rate.
  if (((int)m_sample_rate) != m_sfinfo.samplerate) {
    m_sample_rate = m_sfinfo.samplerate;
    std::cerr << "FileSource::sf_open: overwrite sample rate: " << m_sample_rate
              << std::endl;
  }

  // Check format.
  int major_format = m_sfinfo.format & SF_FORMAT_TYPEMASK;
  std::string major_str;
  if (!get_major_format(major_format, major_str)) {
    m_error = "Unsupported major format ";
    m_error += m_devname + " : ";
    std::stringstream ss;
    ss << std::showbase;
    ss << std::hex << major_format;
    m_error += ss.str();
    return false;
  }

  // Check sub type.
  int sub_type = m_sfinfo.format & SF_FORMAT_SUBMASK;
  std::string sub_type_str;
  if (!get_sub_type(sub_type, sub_type_str)) {
    m_error = "Unsupported major format ";
    m_error += m_devname + " : ";
    std::stringstream ss;
    ss << std::showbase;
    ss << std::hex << sub_type;
    m_error += ss.str();
    return false;
  }

  // Print format.
  std::cerr << "FileSource::format: " << major_str << ", " << sub_type_str
            << std::endl;

  // Set fmt_fn.
  switch (sub_type) {
  case SF_FORMAT_PCM_S8:
  case SF_FORMAT_PCM_16:
  case SF_FORMAT_PCM_24:
  case SF_FORMAT_PCM_U8:
  case SF_FORMAT_FLOAT:
    m_fmt_fn = &FileSource::get_sf_read_float;
    break;
  default:
    return false;
    break;
  }

  // Calculate samplerate per microsecond.
  m_sample_rate_per_us = ((double)m_sample_rate) / 1e6;

  // Limit too large block length.
  double d_expected_us = ((double)m_block_length) / m_sample_rate_per_us;
  if (d_expected_us > max_expected_us) {
    std::uint32_t tmp_blklen =
        round_power((int)(max_expected_us * m_sample_rate_per_us));
    std::cerr << "FileSource::configure: large blklen, round blklen "
              << m_block_length << " to " << tmp_blklen << std::endl;
    m_block_length = tmp_blklen;
  }

  m_confFreq = frequency;

  return true;
}

// round to power of 2
std::uint32_t FileSource::round_power(int n) {
  if (n <= 0) {
    return 0;
  }

  if ((n & (n - 1)) == 0) {
    return (std::uint32_t)n;
  }

  std::uint32_t ret = 1;
  while (n > 1) {
    ret <<= 1;
    n >>= 1;
  }

  return ret;
}

std::uint32_t FileSource::get_sample_rate() { return m_sample_rate; }

std::uint32_t FileSource::get_frequency() { return m_frequency; }

bool FileSource::is_low_if() { return !m_zero_offset; }

void FileSource::print_specific_parms() {
  // nop
}

// Return a list of supported device.
void FileSource::get_device_names(std::vector<std::string> &devices) {
  // Currently, set "FileSource".
  devices.push_back("FileSource");
}

int FileSource::to_sf_format(FormatType format_type) {
  int ret = 0;

  switch (format_type) {
  case FormatType::S8_LE:
    ret = SF_FORMAT_PCM_S8;
    break;
  case FormatType::S16_LE:
    ret = SF_FORMAT_PCM_16;
    break;
  case FormatType::S24_LE:
    ret = SF_FORMAT_PCM_24;
    break;
  case FormatType::U8_LE:
    ret = SF_FORMAT_PCM_U8;
    break;
  case FormatType::Float:
    ret = SF_FORMAT_FLOAT;
    break;
  default:
    assert(0);
    break;
  }

  return ret;
}

bool FileSource::get_major_format(int major_format, std::string &str) {
  bool ret = false;
  if (!m_sfp) {
    return ret;
  }
  if ((major_format != SF_FORMAT_WAV) && (major_format != SF_FORMAT_W64) &&
      (major_format != SF_FORMAT_WAVEX) && (major_format != SF_FORMAT_RAW)) {
    return ret;
  }
  int count;
  SF_FORMAT_INFO sfi;
  sf_command(m_sfp, SFC_GET_FORMAT_MAJOR_COUNT, &count, sizeof(int));
  for (int i = 0; i < count; i++) {
    sfi.format = i;
    sf_command(m_sfp, SFC_GET_FORMAT_MAJOR, &sfi, sizeof(sfi));
    if (sfi.format == major_format) {
      str = sfi.name;
      ret = true;
      break;
    }
  }
  return ret;
}

bool FileSource::get_sub_type(int sub_type, std::string &str) {
  bool ret = false;
  if (!m_sfp) {
    return ret;
  }

  if ((sub_type != SF_FORMAT_PCM_S8) && (sub_type != SF_FORMAT_PCM_16) &&
      (sub_type != SF_FORMAT_PCM_24) && (sub_type != SF_FORMAT_PCM_U8) &&
      (sub_type != SF_FORMAT_FLOAT)) {
    return ret;
  }

  int count;
  SF_FORMAT_INFO sfi;
  sf_command(m_sfp, SFC_GET_FORMAT_SUBTYPE_COUNT, &count, sizeof(int));
  for (int i = 0; i < count; i++) {
    sfi.format = i;
    sf_command(m_sfp, SFC_GET_FORMAT_SUBTYPE, &sfi, sizeof(sfi));
    if (sfi.format == sub_type) {
      str = sfi.name;
      ret = true;
      break;
    }
  }
  return ret;
}

bool FileSource::start(DataBuffer<IQSample> *buf, std::atomic_bool *stop_flag) {
  m_buf = buf;
  m_stop_flag = stop_flag;

  // Start thread.
  if (m_thread == 0) {
    m_thread = new std::thread(run);
    return true;
  } else {
    m_error = "Source thread already started";
    return false;
  }
}

bool FileSource::stop() {
  // Terminate thread.
  if (m_thread) {
    m_thread->join();
    delete m_thread;
    m_thread = 0;
  }

  return true;
}

// Thread to read IQSample from file.
void FileSource::run() {
  IQSampleVector iqsamples;

  // expected microseconds per block reading
  double d_expected =
      ((double)m_this->m_block_length) / m_this->m_sample_rate_per_us;

  // Divide into its fractional and integer part.
  double int_part, frac_part;
  frac_part = std::modf(d_expected, &int_part);

  // integer part of expected microseconds per block reading
  auto expected = std::chrono::microseconds(long(int_part));
  //  std::cerr << expected.count() << std::endl;

  // 1 microsecond
  auto one_us = std::chrono::microseconds(1);

  // delta for fraction part
  double delta = 0.0;

  // Get clock and start reading.
  auto begin = std::chrono::system_clock::now();
  while (!m_this->m_stop_flag->load()) {
    // Read and convert samples.
    if (!get_samples(&iqsamples)) {
      break;
    }

    // Push samples.
    m_this->m_buf->push(std::move(iqsamples));

    // Get clock and calculate elapsed.
    auto end = std::chrono::system_clock::now();
    auto elapsed = end - begin;

    // Throttle.
    std::this_thread::sleep_for(expected - elapsed);

    // Get next clock.
    begin += expected;

    // Sigma delta.
    delta += frac_part;
    if (delta >= 1.0) {
      begin += one_us;
      delta -= 1.0;
    }
  }

  // Push end.
  m_this->m_buf->push_end();

  // Close sndfile.
  sf_close(m_this->m_sfp);

  // Clear handle.
  m_this->m_sfp = nullptr;
}

// Fetch a bunch of samples from the file.
bool FileSource::get_samples(IQSampleVector *samples) {
  bool ret;

  if (!m_this->m_sfp) {
    return false;
  }
  if (!samples) {
    return false;
  }
  if (!m_this->m_fmt_fn) {
    return false;
  }

  ret = (*(m_this->m_fmt_fn))(samples);

  return ret;
}

// TODO: merge get_s8(), get_u8(), get_s16(), get_s24(),
//       and get_float()
//       since the type conversion is doen in sf_read_float() anyway

bool FileSource::get_sf_read_float(IQSampleVector *samples) {
  // read a file using sf_read_float() and convert to float32

  // setup vector for reading
  sf_count_t n_read;
  sf_count_t sz = m_this->m_block_length * 2;
  std::vector<float> buf(sz);

  // read float samples
  // Note: implicit conversion done in sf_read_float()
  n_read = sf_read_float(m_this->m_sfp, buf.data(), sz);
  if (n_read <= 0) {
    // finish reading.
    return false;
  }

  // setup iqsample
  samples->resize(n_read / 2);

  // copy to sample
  for (int i = 0; i < n_read / 2; i++) {
    float re = buf[2 * i];
    float im = buf[2 * i + 1];
    (*samples)[i] = IQSample(re, im);
  }

  return true;
}
