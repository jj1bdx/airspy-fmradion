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

#include <iostream>
#include <chrono>
#include <thread>
#include <cmath>

#include <sndfile.h>

#include "FileSource.h"
#include "ConfigParser.h"
#include "Utility.h"

FileSource *FileSource::m_this = 0;

FileSource::FileSource(int dev_index)
    : m_block_length(default_block_length), m_thread(0) {
  m_this = this;
}

FileSource::~FileSource() {
  if (m_sfp) {
    sf_close(m_sfp);
    m_sfp = NULL;
  }

  m_this = 0;
}

bool FileSource::configure(std::string configurationStr) {
  uint32_t sample_rate = 384000;
  uint32_t frequency = 82500000;
  std::string filename;

  int block_length = default_block_length;

  ConfigParser cp;
  ConfigParser::map_type m;

  cp.parse_config_string(configurationStr, m);
  if (m.find("srate") != m.end()) {
    std::cerr << "FileSource::configure: srate: " << m["srate"] << std::endl;
    sample_rate = atoi(m["srate"].c_str());
  }

  if (m.find("freq") != m.end()) {
    std::cerr << "FileSource::configure: freq: " << m["freq"] << std::endl;
    frequency = atoi(m["freq"].c_str());
  }

  if (m.find("filename") != m.end()) {
    std::cerr << "FileSource::configure: filename: " << m["filename"] << std::endl;
    filename = m["filename"];
  }

  if (m.find("blklen") != m.end()) {
    std::cerr << "FileSource::configure: blklen: " << m["blklen"] << std::endl;
    block_length = atoi(m["blklen"].c_str());
  }

  m_confFreq = frequency;

  return configure(filename, sample_rate, frequency, block_length);
}

bool FileSource::configure(std::string fname, uint32_t sample_rate,
                           uint32_t frequency, int block_length) {
  m_devname = fname;
  m_samplerate = sample_rate;
  m_frequency = frequency;
  m_confFreq = frequency;
  m_block_length = block_length;

//  m_samplerate_per_us = ((double)m_samplerate) / 1e6;

  return true;
}


std::uint32_t FileSource::get_sample_rate() {
  return m_samplerate;
}

std::uint32_t FileSource::get_frequency() {
  return m_frequency;
}

bool FileSource::is_low_if() {
  return true;
}

void FileSource::print_specific_parms() {
}

void FileSource::get_device_names(std::vector<std::string> &devices) {
  devices.push_back("FileSource");
}


bool FileSource::start(DataBuffer<IQSample> *buf, std::atomic_bool *stop_flag) {
  m_buf = buf;
  m_stop_flag = stop_flag;

  // try to open sndfile
  m_sfp = sf_open(m_devname.c_str(), SFM_READ, &m_sfinfo);
  if (m_sfp == NULL) {
    // retry to open with user defined option, maybe raw
    m_sfinfo.samplerate = m_samplerate;
    m_sfinfo.channels = 2;
    m_sfinfo.format = SF_FORMAT_RAW | SF_FORMAT_PCM_24;
    m_sfp = sf_open(m_devname.c_str(), SFM_READ, &m_sfinfo);
    if (m_sfp == NULL) {
      m_error = "Failed to open ";
      m_error += m_devname + ": " + sf_strerror(m_sfp);
    }
  } else {
    // overwrite sample rate
    m_samplerate = m_sfinfo.samplerate;
  }

  // samplerate per microsecond
  m_samplerate_per_us = m_samplerate / 1e6;

  if (m_thread == 0) {
    m_thread = new std::thread(run);
    return true;
  } else {
    m_error = "Source thread already started";
    return false;
  }
}

bool FileSource::stop() {
  if (m_thread) {
    m_thread->join();
    delete m_thread;
    m_thread = 0;
  }

  return true;
}

void FileSource::run() {
  IQSampleVector iqsamples;

  double d_expected = ((double)m_this->m_block_length) / m_this->m_samplerate_per_us;
  double int_part, frac_part;
  frac_part = std::modf(d_expected, &int_part);

  // integer part of expected microseconds per block reading
  auto expected = std::chrono::microseconds(long(int_part));

  std::cerr << expected.count() << std::endl;
  
  // 1 microsecond
  auto one_us = std::chrono::microseconds(1);

  // delta for fraction part
  double delta = 0.0;

  // get clock and start reading
  auto begin = std::chrono::system_clock::now();
  while (!m_this->m_stop_flag->load()) {
    // read and convert samples
    if (!get_samples(&iqsamples)) {
      break;
    }

    // push samples
    m_this->m_buf->push(std::move(iqsamples));

    // get clock and calculate elapsed
    auto end = std::chrono::system_clock::now();
    auto elapsed = end - begin;

    // throttle
    std::this_thread::sleep_for(expected - elapsed);

    // get next clock
    begin += expected;

    // sigma delta
    delta += frac_part;
    if (delta >= 1.0) {
      begin += one_us;
      delta -= 1.0;
    }
  }

  // close
  sf_close(m_this->m_sfp);
}

// Fetch a bunch of samples from the device.
bool FileSource::get_samples(IQSampleVector *samples) {
  size_t n_read;

  if (!m_this->m_sfp) {
    return false;
  }

  if (!samples) {
    return false;
  }

  // setup iqsample
  samples->resize(m_this->m_block_length);

  // int24 to float32
  {
    // setup vector for reading
    size_t sz = m_this->m_block_length * 2; // * 3 * 2/*ch*/;
    std::vector<int> buf(sz);

    // read samples
    n_read = sf_read_int(m_this->m_sfp, buf.data(), sz);
    if (n_read < sz) {
      m_this->m_error = "short read, samples lost";
      return false;
    }

    // convert float32
    for (int i = 0; i < m_this->m_block_length; i++) {
      int32_t re = buf[2 * i + 1];
      int32_t im = buf[2 * i];
      (*samples)[i] = IQSample(re / IQSample::value_type(8388608),
                               im / IQSample::value_type(8388608));
    }
  }

  return true;
}
