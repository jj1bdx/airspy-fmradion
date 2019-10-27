// airspyhf-fmradion
// Software decoder for FM broadcast radio with Airspy HF
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019 Kenji Rikitake, JJ1BDX
// Copyright (C) 2019 Takehiro Sekine
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

#include <iostream>
#include <chrono>
#include <sstream>
#include <thread>
#include <cmath>

#include <sndfile.h>

#include "FileSource.h"
#include "ConfigParser.h"
#include "Utility.h"

FileSource *FileSource::m_this = 0;

// Constructor
FileSource::FileSource(int dev_index)
    : m_sample_rate(default_sample_rate), m_frequency(default_frequency),
      m_zero_offset(false), m_block_length(default_block_length), m_sfp(NULL),
      m_sub_type(0), m_thread(0) {
  m_this = this;
}

// Destructor
FileSource::~FileSource() {
  // Close sndfile.
  if (m_sfp) {
    sf_close(m_sfp);
    m_sfp = NULL;
  }

  m_this = 0;
}

bool FileSource::configure(std::string configurationStr) {
  std::string filename;
  uint32_t sample_rate = default_sample_rate;
  uint32_t frequency = default_frequency;
  bool zero_offset = false;
  int block_length = default_block_length;

  ConfigParser cp;
  ConfigParser::map_type m;

  // srate
  cp.parse_config_string(configurationStr, m);
  if (m.find("srate") != m.end()) {
    std::cerr << "FileSource::configure: srate: " << m["srate"] << std::endl;
    sample_rate = atoi(m["srate"].c_str());
  }

  // freq
  if (m.find("freq") != m.end()) {
    std::cerr << "FileSource::configure: freq: " << m["freq"] << std::endl;
    frequency = atoi(m["freq"].c_str());
  }

  // filename
  if (m.find("filename") != m.end()) {
    std::cerr << "FileSource::configure: filename: " << m["filename"] << std::endl;
    filename = m["filename"];
  }

  // blklen
  if (m.find("blklen") != m.end()) {
    std::cerr << "FileSource::configure: blklen: " << m["blklen"] << std::endl;
    block_length = atoi(m["blklen"].c_str());
  }

  // zero_offset
  if (m.find("zero_offset") != m.end()) {
    std::cerr << "FileSource::configure: zero_offset" << std::endl;
    zero_offset = true;
  }

  // configure
  return configure(filename, sample_rate, frequency, zero_offset, block_length);
}

bool FileSource::configure(std::string fname, std::uint32_t sample_rate,
                           std::uint32_t frequency, bool zero_offset,
                           int block_length) {
  m_devname = fname;
  m_sample_rate = sample_rate;
  m_frequency = frequency;
  m_zero_offset = zero_offset;
  m_block_length = block_length;

  // Open sndfile.
  m_sfp = sf_open(m_devname.c_str(), SFM_READ, &m_sfinfo);
  if (m_sfp == NULL) {
    // Set m_error and return false if sf_open is failed.
    m_error = "Failed to open ";
    m_error += m_devname + " : " + sf_strerror(m_sfp);
    return false;
  }

  // Get format.
  int major_format = m_sfinfo.format & SF_FORMAT_TYPEMASK;
  if ((major_format != SF_FORMAT_WAV) && (major_format != SF_FORMAT_W64) &&
      (major_format != SF_FORMAT_WAVEX)) {
    m_error = "Unsupported major format ";
    m_error += m_devname + " : ";
    std::stringstream ss;
    ss << std::showbase;
    ss << std::hex << major_format;
    m_error += ss.str();
    return false;
  }

  m_sub_type = m_sfinfo.format & SF_FORMAT_SUBMASK;
  if ((m_sub_type != SF_FORMAT_PCM_16) && (m_sub_type != SF_FORMAT_PCM_24) &&
      (m_sub_type != SF_FORMAT_FLOAT)) {
    m_error = "Unsupported subtype of format ";
    m_error += m_devname + " : ";
    std::stringstream ss;
    ss << std::showbase;
    ss << std::hex << m_sub_type;
    m_error += ss.str();
    return false;
  }

  // Overwrite sample rate.
  m_sample_rate = m_sfinfo.samplerate;
  std::cerr << "FileSource::sf_open overwrite srate: " << m_sample_rate << std::endl;

  // Calculate samplerate per microsecond.
  m_sample_rate_per_us = ((double)m_sample_rate) / 1e6;

#if 0
  double d_expected = ((double)m_this->m_block_length) / m_this->m_sample_rate_per_us;
  std::cerr << d_expected << std::endl;
#endif

  m_confFreq = frequency;

  return true;
}

std::uint32_t FileSource::get_sample_rate() {
  return m_sample_rate;
}

std::uint32_t FileSource::get_frequency() {
  return m_frequency;
}

bool FileSource::is_low_if() {
  return !m_zero_offset;
}

void FileSource::print_specific_parms() {
  // nop
}

// Return a list of supported device.
void FileSource::get_device_names(std::vector<std::string> &devices) {
  // Currently, set "FileSource".
  devices.push_back("FileSource");
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
  double d_expected = ((double)m_this->m_block_length) / m_this->m_sample_rate_per_us;

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
}

// Fetch a bunch of samples from the file.
bool FileSource::get_samples(IQSampleVector *samples) {

  if (!m_this->m_sfp) {
    return false;
  }
  if (!samples) {
    return false;
  }

  bool ret;
  switch (m_this->m_sub_type) {
  case SF_FORMAT_PCM_16:
    ret = m_this->get_s16(samples);
    break;
  case SF_FORMAT_PCM_24:
    ret = m_this->get_s24(samples);
    break;
  case SF_FORMAT_FLOAT:
    ret = m_this->get_float(samples);
    break;
  default:
    // Unsupported format sub_type.
    ret = false;
    break;
  }

  return ret;
}

bool FileSource::get_s16(IQSampleVector *samples) {
  // read sint16 and convert to float32

  // setup vector for reading
  sf_count_t n_read;
  sf_count_t sz = m_this->m_block_length * 2;
  std::vector<int> buf(sz);

  // read int samples
  n_read = sf_read_int(m_this->m_sfp, buf.data(), sz);
  if (n_read <= 0) {
    // finish reading.
    return false;
  }

  // setup iqsample
  samples->resize(n_read / 2);

  // convert sint16 to float32
  for (int i = 0; i < n_read / 2; i++) {
    int32_t re = buf[2 * i];
    int32_t im = buf[2 * i + 1];
    (*samples)[i] = IQSample(re / IQSample::value_type(32768),
                             im / IQSample::value_type(32768));
  }

  return true;
}

bool FileSource::get_s24(IQSampleVector *samples) {
  // read sint24 and convert to float32

  // setup vector for reading
  sf_count_t n_read;
  sf_count_t sz = m_this->m_block_length * 2;
  std::vector<int> buf(sz);

  // read int samples
  n_read = sf_read_int(m_this->m_sfp, buf.data(), sz);
  if (n_read <= 0) {
    // finish reading.
    return false;
  }

  // setup iqsample
  samples->resize(n_read / 2);

  // convert int24 to float32
  for (int i = 0; i < n_read / 2; i++) {
    int32_t re = buf[2 * i];
    int32_t im = buf[2 * i + 1];
    (*samples)[i] = IQSample(re / IQSample::value_type(8388608),
                             im / IQSample::value_type(8388608));
  }

  return true;
}

bool FileSource::get_float(IQSampleVector *samples) {
  // read sample and copy to float32

  // setup vector for reading
  sf_count_t n_read;
  sf_count_t sz = m_this->m_block_length * 2;
  std::vector<float> buf(sz);

  // read int samples
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
