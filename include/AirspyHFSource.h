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

#ifndef INCLUDE_AIRSPYHFSOURCE_H
#define INCLUDE_AIRSPYHFSOURCE_H

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
  virtual ~AirspyHFSource() override;

  virtual bool configure(std::string configuration) override;

  /** Return current sample frequency in Hz. */
  virtual std::uint32_t get_sample_rate() override;

  /** Return device current center frequency in Hz. */
  virtual std::uint32_t get_frequency() override;

  /** Return if device is using Low-IF. */
  virtual bool is_low_if() override;

  /** Print current parameters specific to device type */
  virtual void print_specific_parms() override;

  virtual bool start(DataBuffer<IQSample> *buf,
                     std::atomic_bool *stop_flag) override;
  virtual bool stop() override;

  /** Return true if the device is OK, return false if there is an error. */
  virtual operator bool() const override { return m_dev && m_error.empty(); }

  /** Return a list of supported devices. */
  static void get_device_names(std::vector<std::string> &devices);

private:
  int32_t check_sampleRateIndex(uint32_t sampleRate);
  /**
   * Configure Airspy HF tuner and prepare for streaming.
   *
   * sampleRateIndex :: desired sample rate index in the sample rates
   * hfAttLevel      :: HF attenuation level and AGC control
   *                 :: 0 (default): AGC turned on and no attenuation
   *                 :: 1 ~ 8: AGC turned off, 6 ~ 48dB attenuation (6dB step)
   * enumeration list. frequency       :: desired center frequency in Hz.
   *
   * Return true for success, false if an error occurred.
   */
  bool configure(int sampleRateIndex, uint8_t hfAttLevel, uint32_t frequency);

  void callback(const float *buf, int len);
  static int rx_callback(airspyhf_transfer_t *transfer);
  static void run(airspyhf_device *dev, std::atomic_bool *stop_flag);

  struct airspyhf_device *m_dev;
  uint32_t m_sampleRate;
  uint32_t m_frequency;
  bool m_low_if;
  bool m_running;
  static AirspyHFSource *m_this;
  std::vector<int> m_srates;
  std::string m_sratesStr;

  airspyhf_lib_version_t m_libv;
  int m_ndev;
  std::vector<uint64_t> m_serials;

  std::thread *m_thread;
};

#endif /* INCLUDE_AIRSPYSOURCE_H_ */
