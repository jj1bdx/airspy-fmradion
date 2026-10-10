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
    long capacity_frames = -1;
    long min_free_frames = -1; // lowest free space seen (-1: none yet)
    long last_free_frames = -1;
    unsigned int sample_rate = 0;
  };

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
  /// samplerate in Hz, stereo (true for stereo data), sf_info (SF_INFO.format
  /// selects the output format).
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
  // defaultHighOutputLatency floored at minimum_latency_default).
  PortAudioOutput(const PaDeviceIndex device_index, unsigned int samplerate,
                  bool stereo, PaTime suggested_latency_sec = -1.0);

  virtual ~PortAudioOutput() override;
  virtual bool write(const SampleVector &samples) override;
  virtual void output_close() override;
  virtual Telemetry telemetry() const override;

private:
  // Terminate PortAudio
  // then add PortAudio error string to m_error and set m_zombie flag.
  void add_paerror(const std::string &msg);

  unsigned int m_nchannels;
  PaStreamParameters m_outputparams{};
  PaStream *m_stream = nullptr;
  PaError m_paerror = paNoError;
  volk::vector<float> m_floatbuf;
  // Telemetry: written by the writer thread, read by any thread.
  unsigned int m_samplerate = 0;
  long m_capacity_frames = -1;
  std::atomic<std::uint64_t> m_underflows{0};
  std::atomic<long> m_min_free_frames{-1};
  std::atomic<long> m_last_free_frames{-1};
};

#endif
