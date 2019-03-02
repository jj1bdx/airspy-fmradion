// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
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

#ifndef INCLUDE_SOURCE_H_
#define INCLUDE_SOURCE_H_

#include <atomic>
#include <memory>
#include <string>

#include "DataBuffer.h"
#include "SoftFM.h"

class Source {
public:
  Source() : m_confFreq(0), m_buf(0) {}
  virtual ~Source() {}

  /**
   * Configure device and prepare for streaming.
   */
  virtual bool configure(std::string configuration) = 0;

  /** Return current sample frequency in Hz. */
  virtual std::uint32_t get_sample_rate() = 0;

  /** Return device current center frequency in Hz. */
  virtual std::uint32_t get_frequency() = 0;

  /** Return current configured center frequency in Hz. */
  std::uint32_t get_configured_frequency() const { return m_confFreq; }

  /** Print current parameters specific to device type */
  virtual void print_specific_parms() = 0;

  /** start device before sampling loop.
   * Give it a reference to the buffer of samples */
  virtual bool start(DataBuffer<IQSample> *buf,
                     std::atomic_bool *stop_flag) = 0;

  /** stop device after sampling loop */
  virtual bool stop() = 0;

  /** Return true if the device is OK, return false if there is an error. */
  virtual operator bool() const = 0;

  /** Return name of opened RTL-SDR device. */
  std::string get_device_name() const { return m_devname; }

  /** Return the last error, or return an empty string if there is no error. */
  std::string error() {
    std::string ret(m_error);
    m_error.clear();
    return ret;
  }

protected:
  std::string m_devname;
  std::string m_error;
  uint32_t m_confFreq;
  DataBuffer<IQSample> *m_buf;
  std::atomic_bool *m_stop_flag;
};

#endif /* INCLUDE_SOURCE_H_ */
