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

#ifndef INCLUDE_AIRSPYHFSOURCE_H_
#define INCLUDE_AIRSPYHFSOURCE_H_

#include "libairspyhf/airspyhf.h"
#include <cstdint>
#include <string>
#include <vector>

#include "Source.h"

#define AIRSPYHF_MAX_DEVICE (32)

class AirspyHFSource : public Source {
public:
  /** Open Airspy device. */
  AirspyHFSource(int dev_index);

  /** Close Airspy device. */
  virtual ~AirspyHFSource();

  virtual bool configure(std::string configuration);

  /** Return current sample frequency in Hz. */
  virtual std::uint32_t get_sample_rate();

  /** Return device current center frequency in Hz. */
  virtual std::uint32_t get_frequency();

  /** Print current parameters specific to device type */
  virtual void print_specific_parms();

  virtual bool start(DataBuffer<IQSample> *buf, std::atomic_bool *stop_flag);
  virtual bool stop();

  /** Return true if the device is OK, return false if there is an error. */
  virtual operator bool() const { return m_dev && m_error.empty(); }

  /** Return a list of supported devices. */
  static void get_device_names(std::vector<std::string> &devices);

private:
  /**
   * Configure Airspy HF tuner and prepare for streaming.
   *
   * sampleRateIndex :: desired sample rate index in the sample rates
   * enumeration list. frequency       :: desired center frequency in Hz.
   *
   * Return true for success, false if an error occurred.
   */
  bool configure(int sampleRateIndex, uint32_t frequency);

  void callback(const float *buf, int len);
  static int rx_callback(airspyhf_transfer_t *transfer);
  static void run(airspyhf_device *dev, std::atomic_bool *stop_flag);

  struct airspyhf_device *m_dev;
  uint32_t m_sampleRate;
  uint32_t m_frequency;
  bool m_running;
  std::thread *m_thread;
  static AirspyHFSource *m_this;
  std::vector<int> m_srates;
  std::string m_sratesStr;
};

#endif /* INCLUDE_AIRSPYSOURCE_H_ */
