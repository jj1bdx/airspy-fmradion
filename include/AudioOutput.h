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

#ifndef INCLUDE_AUDIOOUTPUT_H
#define INCLUDE_AUDIOOUTPUT_H

#include <atomic>
#include <cstdint>
#include <string>

#include "SoftFM.h"

#include "portaudio.h"
#include <sndfile.h>

/// Base class for writing audio data to file or playback.
class AudioOutput {
public:
  /// Output-side counters; capacity_frames < 0 means the output has no ring.
  struct Telemetry {
    std::uint64_t underflows = 0;
    // Underflows reported while starting up (prime and first block).
    std::uint64_t startup_underflows = 0;
    long capacity_frames = -1;
    long min_free_frames = -1; // lowest free space seen (-1: none yet)
    // Highest free space seen just before a write, i.e. the ring trough
    // (-1: none yet; the first block is excluded).
    long max_pre_free_frames = -1;
    long last_free_frames = -1;
    // Device IO buffer size in frames now and at stream start (-1: unknown).
    long device_buffer_frames = -1;
    long device_buffer_start_frames = -1;
    long prefill_frames = 0; // total silence prefill written
    unsigned int sample_rate = 0;
  };

  /// Convert a frame count at the given sample rate to milliseconds.
  inline static double frames_to_ms(long frames, unsigned int rate) {
    return (rate > 0) ? (1000.0 * frames / rate) : 0.0;
  }

  /// Destructor.
  virtual ~AudioOutput() {}

  /// Write audio data; return true on success, false if an error occurs.
  virtual bool write(const SampleVector &samples) = 0;

  // Close audio output.
  virtual void output_close() = 0;

  /// Return the last error, or return an empty string if there is no error.
  std::string error() {
    std::string ret(m_error);
    m_error.clear();
    return ret;
  }

  /// Return true if the stream is OK, return false if there is an error.
  operator bool() const { return (!m_zombie) && m_error.empty(); }

  std::string get_device_name() const { return m_device_name; }

  /// Return output counters (callable from any thread); default: none.
  virtual Telemetry telemetry() const { return Telemetry{}; }

protected:
  /// Constructor.
  AudioOutput() : m_zombie(false), m_closed(false) {}

  std::string m_error;
  bool m_zombie;
  std::string m_device_name;
  bool m_closed;

private:
  AudioOutput(const AudioOutput &);            // no copy constructor
  AudioOutput &operator=(const AudioOutput &); // no assignment operator
};

// Output via libsndfile
class SndfileOutput : public AudioOutput {
public:
  /// Construct libsndfile audio writer: filename (path, or "-" for stdout),
  /// samplerate in Hz, stereo (true for stereo data), format (SF_INFO.format
  /// value selecting the output format).
  SndfileOutput(const std::string &filename, unsigned int samplerate,
                bool stereo, int format);

  virtual ~SndfileOutput() override;
  virtual bool write(const SampleVector &samples) override;
  virtual void output_close() override;

private:
  // Add sndfile log info to m_error and set m_zombie flag
  void add_error_log_info(SNDFILE *sf);

  const unsigned numberOfChannels;
  const unsigned sampleRate;
  int m_fd = -1;
  SNDFILE *m_sndfile = nullptr;
  SF_INFO m_sndfile_sfinfo{};
};

class PortAudioOutput : public AudioOutput {
public:
  // Static variables.

  // Minimum latency for audio output in seconds

  // PortAudio suggestedLatency: 0.014717 (Mac mini 2023, macOS 14.3.1),
  // 0.034830 (Ubuntu 22.04.4 x86_64); a 40 ms floor suffices on macOS, Ubuntu,
  // Raspberry Pi OS (-L for less). See doc/LATENCY_OPTION_20260715.md.
  static constexpr PaTime minimum_latency_default = 0.04;

  // Construct PortAudio output stream: device_index, samplerate (Hz), stereo,
  // suggested_latency_sec (PaTime; negative = no override, use
  // defaultHighOutputLatency floored at minimum_latency_default),
  // prefill_ms (silence written once before the first audio block; 0 = none),
  // adapt_max_ms (if > 0, add one silence quantum per underflow until the
  // total prefill reaches this cap; 0 = off). Both are clamped to the
  // ring capacity minus one audio block at the first write().
  PortAudioOutput(const PaDeviceIndex device_index, unsigned int samplerate,
                  bool stereo, PaTime suggested_latency_sec = -1.0,
                  double prefill_ms = 0.0, double adapt_max_ms = 0.0);

  /// Silence quantum added per underflow by the adaptive prefill.
  static constexpr double adapt_step_ms = 10.0;

  virtual ~PortAudioOutput() override;
  virtual bool write(const SampleVector &samples) override;
  virtual void output_close() override;
  virtual Telemetry telemetry() const override;

private:
  // Terminate PortAudio
  // then add PortAudio error string to m_error and set m_zombie flag.
  void add_paerror(const std::string &msg);

  // Write silence_frames of silence; count an underflow report (as a
  // start-up underflow if startup is true). Return success.
  bool write_silence(long silence_frames, bool startup);

  // Raise the capacity estimate to avail if avail is larger.
  void raise_capacity(long avail);

  // Apply the prefill once, before the first audio block of block_frames.
  bool prime(long block_frames);

  // Adaptive prefill: after an underflow, add one silence quantum if the
  // ring has room and the cap is not reached. Return success.
  bool adapt();

  unsigned int m_nchannels;
  PaStreamParameters m_outputparams{};
  PaStream *m_stream = nullptr;
  PaError m_paerror = paNoError;
  volk::vector<float> m_floatbuf;
  // Telemetry: written by the writer thread, read by any thread.
  unsigned int m_samplerate = 0;
  // Ring size estimate (macOS: PortAudio's formula; else from outputLatency),
  // raised by the writer thread to the largest free space observed.
  std::atomic<long> m_capacity_frames{-1};
  std::atomic<std::uint64_t> m_underflows{0};
  std::atomic<std::uint64_t> m_startup_underflows{0};
  std::atomic<long> m_min_free_frames{-1};
  std::atomic<long> m_max_pre_free_frames{-1};
  std::atomic<long> m_last_free_frames{-1};
  std::atomic<long> m_prefill_total_frames{0};
  // Prefill state: used by the writer thread only.
  double m_prefill_ms = 0.0;
  double m_adapt_max_ms = 0.0;
  bool m_primed = false;
  long m_max_block_frames = 0;
  long m_adapt_cap_frames = 0;
  // macOS: output AudioDeviceID (0 = none) and its IO buffer size at start.
  std::uint32_t m_mac_device = 0;
  long m_device_buffer_start_frames = -1;
};

#endif
