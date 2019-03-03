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

#ifndef INCLUDE_AIRSPYSOURCE_H_
#define INCLUDE_AIRSPYSOURCE_H_

#include "libairspy/airspy.h"
#include <cstdint>
#include <string>
#include <vector>

#include "Source.h"

#define AIRSPY_MAX_DEVICE (32)

class AirspySource : public Source {
public:
  /** Open Airspy device. */
  AirspySource(int dev_index);

  /** Close Airspy device. */
  virtual ~AirspySource();

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
   * Configure Airspy tuner and prepare for streaming.
   *
   * sampleRateIndex :: desired sample rate index in the sample rates
   * enumeration list. frequency       :: desired center frequency in Hz.
   * bias_ant        :: antenna bias
   * lna_gain        :: desired LNA gain: 0 to 14 dB.
   * mix_gain        :: desired mixer gain: 0 to 15 dB.
   * vga_gain        :: desired VGA gain: 0 to 15 dB
   * lna_agc         :: LNA AGC
   * mix_agc         :: Mixer AGC
   *
   * Return true for success, false if an error occurred.
   */
  bool configure(int sampleRateIndex, uint32_t frequency, bool bias_ant,
                 int lna_gain, int mix_gain, int vga_gain, bool lna_agc,
                 bool mix_agc);

  void callback(const float *buf, int len);
  static int rx_callback(airspy_transfer_t *transfer);
  static void run(airspy_device *dev, std::atomic_bool *stop_flag);

  struct airspy_device *m_dev;
  uint32_t m_sampleRate;
  uint32_t m_frequency;
  int m_lnaGain;
  int m_mixGain;
  int m_vgaGain;
  bool m_biasAnt;
  bool m_lnaAGC;
  bool m_mixAGC;
  bool m_running;
  std::thread *m_thread;
  static AirspySource *m_this;
  static const std::vector<int> m_lgains;
  static const std::vector<int> m_mgains;
  static const std::vector<int> m_vgains;
  std::vector<int> m_srates;
  std::string m_lgainsStr;
  std::string m_mgainsStr;
  std::string m_vgainsStr;
  std::string m_sratesStr;
};

#endif /* INCLUDE_AIRSPYSOURCE_H_ */
