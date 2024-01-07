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

#ifndef INCLUDE_RTLSDRSOURCE_H
#define INCLUDE_RTLSDRSOURCE_H

#include <string>
#include <thread>

#include "Source.h"

class RtlSdrSource : public Source {
public:
  static constexpr int default_block_length = 16384;

  /** Open RTL-SDR device. */
  RtlSdrSource(int dev_index);

  /** Close RTL-SDR device. */
  virtual ~RtlSdrSource() override;

  virtual bool configure(std::string configuration) override;

  /** Return current sample frequency in Hz. */
  virtual std::uint32_t get_sample_rate() override;

  /** Return device current center frequency in Hz. */
  virtual std::uint32_t get_frequency() override;

  /** Return if device is using Low-IF. */
  virtual bool is_low_if() override;

  /** Print current parameters specific to device type */
  virtual void print_specific_parms() override;

  virtual bool start(DataBuffer<IQSample> *samples,
                     std::atomic_bool *stop_flag) override;
  virtual bool stop() override;

  /** Return true if the device is OK, return false if there is an error. */
  virtual operator bool() const override { return m_dev && m_error.empty(); }

  /** Return a list of supported devices. */
  static void get_device_names(std::vector<std::string> &devices);

private:
  /**
   * Configure RTL-SDR tuner and prepare for streaming.
   *
   * sample_rate  :: desired sample rate in Hz.
   * frequency    :: desired center frequency in Hz.
   * tuner_gain   :: desired tuner gain in 0.1 dB, or INT_MIN for auto-gain.
   * block_length :: preferred number of samples per block.
   * agcmode      :: enable AGC mode.
   * antbias      :: enable antenna bias tee.
   *
   * Return true for success, false if an error occurred.
   */
  bool configure(std::uint32_t sample_rate, std::uint32_t frequency,
                 int tuner_gain, int block_length = default_block_length,
                 bool agcmode = false, bool antbias = false);

  /** Return a list of supported tuner gain settings in units of 0.1 dB. */
  std::vector<int> get_tuner_gains();

  /** Return current tuner gain in units of 0.1 dB. */
  int get_tuner_gain();

  /**
   * Fetch a bunch of samples from the device.
   *
   * This function must be called regularly to maintain streaming.
   * Return true for success, false if an error occurred.
   */
  static bool get_samples(IQSampleVector *samples);

  static void run();

  struct rtlsdr_dev *m_dev;
  int m_block_length;
  std::vector<int> m_gains;
  std::string m_gainsStr;
  bool m_confAgc;
  static RtlSdrSource *m_this;

  std::thread *m_thread;
};

#endif
