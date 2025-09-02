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

#ifndef INCLUDE_AUDIOOUTPUT_H
#define INCLUDE_AUDIOOUTPUT_H

#include <string>

#include "SoftFM.h"

#include "portaudio.h"
#include <sndfile.h>

extern "C" {
#include "pa_ringbuffer.h"
}

/** Base class for writing audio data to file or playback. */
class AudioOutput {
public:
  /** Destructor. */
  virtual ~AudioOutput() {}

  /**
   * Write audio data.
   *
   * Return true on success.
   * Return false if an error occurs.
   */
  virtual bool write(const SampleVector &samples) = 0;

  // Close audio output.
  virtual void output_close() = 0;

  /** Return the last error, or return an empty string if there is no error. */
  std::string error() {
    std::string ret(m_error);
    m_error.clear();
    return ret;
  }

  /** Return true if the stream is OK, return false if there is an error. */
  operator bool() const { return (!m_zombie) && m_error.empty(); }

  const std::string get_device_name() { return m_device_name; }

protected:
  /** Constructor. */
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
  /**
   * Construct libsndfile audio writer.
   *
   * filename     :: file name (including path) or "-" to write to stdout
   * samplerate   :: audio sample rate in Hz
   * stereo       :: true if the output stream contains stereo data
   * sf_info:     :: specify output format by SF_INFO.format
   */
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
  int m_fd;
  SNDFILE *m_sndfile;
  SF_INFO m_sndfile_sfinfo;
};

class PortAudioOutput : public AudioOutput {
public:
  // Static variables.

  // Ring buffer size
  // *must be* a power of 2
  // 65536 frames for 48000 frames/sec ~= 2.73 seconds
  // (Stereo playback needs *two* samples for a frame)
  static constexpr ring_buffer_size_t ringbuffer_frame_length = 65536;
  // This value should be large to prevent unwanted noise
  static constexpr unsigned long frames_per_buffer = 2048;

  // Construct PortAudio output stream.
  //
  // device_index :: device index number
  // samplerate   :: audio sample rate in Hz
  // stereo       :: true if the output stream contains stereo data
  PortAudioOutput(const PaDeviceIndex device_index, unsigned int samplerate,
                  bool stereo);

  virtual ~PortAudioOutput() override;
  virtual bool write(const SampleVector &samples) override;
  virtual void output_close() override;

  // C++ callback code called from pa_callback().
  int stream_callback(float *output, unsigned long frame_count);

private:
  // Terminate PortAudio
  // then add PortAudio error string to m_error and set m_zombie flag.
  void add_paerror(const std::string &msg);

  // Static C-style callback function for PortAudio stream.
  // user_data has the pointer to the PortAudio object itself ('this').
  static int pa_callback(const void *input, void *output,
                         unsigned long frame_count,
                         const PaStreamCallbackTimeInfo *time_info,
                         PaStreamCallbackFlags status_flags, void *user_data) {
    PortAudioOutput *portaudio_object =
        static_cast<PortAudioOutput *>(user_data);
    return portaudio_object->stream_callback(static_cast<float *>(output),
                                             frame_count);
  }

  static ring_buffer_size_t rbs_min(ring_buffer_size_t a,
                                    ring_buffer_size_t b) {
    return (a < b) ? a : b;
  }

  unsigned int m_nchannels;
  PaStreamParameters m_outputparams;
  PaStream *m_stream;
  PaError m_paerror;
  volk::vector<float> m_floatbuf;
  PaUtilRingBuffer m_ringbuffer;
  volk::vector<float> m_ringbuffer_data;
};

#endif
