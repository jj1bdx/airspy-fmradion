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

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <fmt/format.h>
#include <unistd.h>
#include <vector>

#include "AudioOutput.h"
#include "SoftFM.h"
#include "sndfile.h"

// class SndfileOutput

// Constructor
SndfileOutput::SndfileOutput(const std::string &filename,
                             unsigned int samplerate, bool stereo, int format)
    : numberOfChannels(stereo ? 2 : 1), sampleRate(samplerate) {

  if (filename == "-") {
    m_fd = STDOUT_FILENO;
  } else {
    m_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (m_fd < 0) {
      m_error =
          fmt::format("can not open '{}' ({})", filename, strerror(errno));
      m_zombie = true;
      return;
    }
  }

  m_sndfile_sfinfo.format = format;
  m_sndfile_sfinfo.samplerate = sampleRate;
  m_sndfile_sfinfo.channels = numberOfChannels;

  if (!sf_format_check(&m_sndfile_sfinfo)) {
    m_error = fmt::format("SF_INFO for file '{}' is invalid", filename);
    m_zombie = true;
    if (m_fd != STDOUT_FILENO && m_fd >= 0) {
      ::close(m_fd);
    }
    m_fd = -1;
    return;
  }

  m_sndfile = sf_open_fd(m_fd, SFM_WRITE, &m_sndfile_sfinfo, SF_TRUE);
  if (m_sndfile == nullptr) {
    m_error =
        fmt::format("can not open '{}' ({})", filename, sf_strerror(nullptr));
    m_zombie = true;
    if (m_fd != STDOUT_FILENO && m_fd >= 0) {
      ::close(m_fd);
    }
    m_fd = -1;
    return;
  }

  const int filetype = m_sndfile_sfinfo.format & SF_FORMAT_TYPEMASK;

  // For RF64 file, downgrade to WAV if filesize is smaller than 4GB
  if (filetype == SF_FORMAT_RF64) {
    if (SF_TRUE !=
        sf_command(m_sndfile, SFC_RF64_AUTO_DOWNGRADE, NULL, SF_TRUE)) {
      m_error = fmt::format(
          "unable to set SFC_RF64_AUTO_DOWNGRADE to SF_TRUE on '{}' ({})",
          filename, sf_strerror(m_sndfile));
      add_error_log_info(m_sndfile);
      return;
    }
  }

  // For filetypes with header, header is updated for every sf_write() calls
  if ((filetype == SF_FORMAT_RF64) || (filetype == SF_FORMAT_WAV)) {
    if (SF_TRUE !=
        sf_command(m_sndfile, SFC_SET_UPDATE_HEADER_AUTO, NULL, SF_TRUE)) {
      m_error = fmt::format(
          "unable to set SFC_SET_UPDATE_HEADER_AUTO to SF_TRUE on '{}' ({})",
          filename, sf_strerror(m_sndfile));
      add_error_log_info(m_sndfile);
      return;
    }
  }

#if defined(LIBSNDFILE_MP3_ENABLED)
  // Set MP3 parameters here
  if (filetype == SF_FORMAT_MPEG) {
    fmt::println(stderr, "Set MP3 parameters");
    // Set MP3 default format parameters to:
    // Variable bitrate mode, compression level = 0.1
    int constant_mode = SF_BITRATE_MODE_VARIABLE;
    double compression_level = 0.1;
    // NOTE: you need to set SFC_SET_COMPRESSION_LEVEL *BEFORE*
    // executing SFC_SET_BITRATE_MODE.
    if (SF_TRUE != sf_command(m_sndfile, SFC_SET_COMPRESSION_LEVEL,
                              &compression_level, sizeof(double))) {
      m_error =
          fmt::format("unable to set SFC_SET_COMPRESSION_LEVEL on '{}' ({})",
                      filename, sf_strerror(m_sndfile));
      add_error_log_info(m_sndfile);
      return;
    }
    if (SF_TRUE != sf_command(m_sndfile, SFC_SET_BITRATE_MODE, &constant_mode,
                              sizeof(int))) {
      m_error = fmt::format("unable to set SFC_SET_BITRATE_MODE on '{}' ({})",
                            filename, sf_strerror(m_sndfile));
      add_error_log_info(m_sndfile);
      return;
    }
  }
#endif // LIBSNDFILE_MP3_ENABLED

  m_device_name = "SndfileOutput";
}

// Output closing method.
void SndfileOutput::output_close() {
  // Nothing special to handle m_zombie..
  // Done writing the file
  if (m_sndfile) {
    sf_close(m_sndfile);
  }
  // Set closed flag to prevent multiple closing
  m_closed = true;
}

// Destructor.
SndfileOutput::~SndfileOutput() {
  // close output if not yet closed
  if (!m_closed) {
    SndfileOutput::output_close();
  }
}

// Write audio data.
bool SndfileOutput::write(const SampleVector &samples) {
  // Fail if zombied.
  if (m_zombie) {
    return false;
  }

  const sf_count_t size = samples.size();
  // Write samples to file with items.
  sf_count_t k = sf_write_float(m_sndfile, samples.data(), size);
  if (k != size) {
    m_error = fmt::format("write failed ({})", sf_strerror(m_sndfile));
    return false;
  }
  return true;
}

// Add sndfile log info to m_error and set m_zombie flag
void SndfileOutput::add_error_log_info(SNDFILE *sf) {
  const int max_length = 8192;
  char buffer[max_length];

  const int length = sf_command(sf, SFC_GET_LOG_INFO, buffer, max_length);
  if (length <= 0) {
    m_error.append("\n=== SFC_GET_LOG_INFO returned no data\n");
    m_zombie = true;
    return;
  }
  const std::string logmsg(buffer, static_cast<std::size_t>(length));

  m_error.append("\n=== SFC_GET_LOG_INFO output:\n");
  m_error.append(logmsg);
  m_error.append("\n=== End of SFC_GET_LOG_INFO output\n");
  m_zombie = true;
}
// Class PortAudioOutput

// Construct PortAudio output stream.
PortAudioOutput::PortAudioOutput(const PaDeviceIndex device_index,
                                 unsigned int samplerate, bool stereo,
                                 PaTime suggested_latency_sec,
                                 double prefill_ms, double adapt_max_ms)
    : m_prefill_ms(prefill_ms), m_adapt_max_ms(adapt_max_ms) {
  m_nchannels = stereo ? 2 : 1;

  m_paerror = Pa_Initialize();
  if (m_paerror != paNoError) {
    add_paerror("Pa_Initialize()");
    return;
  }

  if (device_index == -1) {
    m_outputparams.device = Pa_GetDefaultOutputDevice();
  } else {
    const PaDeviceIndex index = static_cast<PaDeviceIndex>(device_index);
    if (index >= Pa_GetDeviceCount()) {
      add_paerror("Device number out of range");
      return;
    }
    m_outputparams.device = index;
  }
  if (m_outputparams.device == paNoDevice) {
    add_paerror("No default output device");
    return;
  }
  m_device_name = Pa_GetDeviceInfo(m_outputparams.device)->name;

  m_outputparams.channelCount = m_nchannels;
  m_outputparams.sampleFormat = paFloat32;
  if (suggested_latency_sec >= 0.0) {
    // User-specified latency (-L): used verbatim, bypassing the floor
    // minimum_latency_default below; main.cpp range-checks it to [1, 100] ms.
    m_outputparams.suggestedLatency = suggested_latency_sec;
  } else {
    m_outputparams.suggestedLatency =
        Pa_GetDeviceInfo(m_outputparams.device)->defaultHighOutputLatency;
    m_outputparams.hostApiSpecificStreamInfo = NULL;
    // Guarantee minimum latency.
    if (m_outputparams.suggestedLatency < minimum_latency_default) {
      m_outputparams.suggestedLatency = minimum_latency_default;
    }
  }

  fmt::println(stderr, "suggestedLatency = {} [ms]",
               m_outputparams.suggestedLatency * 1000);

  m_paerror =
      Pa_OpenStream(&m_stream,
                    NULL, // no input
                    &m_outputparams, samplerate, paFramesPerBufferUnspecified,
                    paClipOff, // no clipping
                    NULL,      // no callback, blocking API
                    NULL       // no callback userData
      );
  if (m_paerror != paNoError) {
    add_paerror("Pa_OpenStream()");
    return;
  }

  m_paerror = Pa_StartStream(m_stream);
  if (m_paerror != paNoError) {
    add_paerror("Pa_StartStream()");
    return;
  }

  // Ring capacity estimate: reported output latency times the sample rate.
  m_samplerate = samplerate;
  const PaStreamInfo *info = Pa_GetStreamInfo(m_stream);
  if (info != nullptr) {
    m_capacity_frames = static_cast<long>(info->outputLatency * samplerate);
  }
}

// Output closing method.
void PortAudioOutput::output_close() {
  // Make closing idempotent.
  if (m_closed) {
    return;
  }
  // Set closed flag first so the destructor cannot re-enter this method
  // even when an error path returns early below.
  m_closed = true;
  // If the object is a zombie, add_paerror() has already called
  // Pa_Terminate(); calling any further PortAudio API is undefined.
  // Also skip closing if the stream was never opened.
  if (m_zombie || (m_stream == nullptr)) {
    return;
  }
  m_paerror = Pa_StopStream(m_stream);
  if (m_paerror != paNoError) {
    add_paerror("Pa_StopStream()");
    return;
  }
  m_paerror = Pa_CloseStream(m_stream);
  if (m_paerror != paNoError) {
    add_paerror("Pa_CloseStream()");
    return;
  }
  Pa_Terminate();
}

// Destructor.
PortAudioOutput::~PortAudioOutput() {
  // close output if not yet closed
  if (!m_closed) {
    PortAudioOutput::output_close();
  }
}

// Write audio data.
bool PortAudioOutput::write(const SampleVector &samples) {
  if (m_zombie) {
    return false;
  }

  const unsigned long sample_size = samples.size();

  // Prefill goes right before the first block, not at stream start: the
  // source may take long to deliver it, and the ring would drain meanwhile.
  const long block_frames = static_cast<long>(sample_size / m_nchannels);
  m_max_block_frames = std::max(m_max_block_frames, block_frames);
  const bool first_block = !m_primed;
  if (first_block) {
    if (!prime(block_frames)) {
      return false;
    }
  }

  m_floatbuf.resize(sample_size);

  // Copy float samples to the PortAudio buffer.
  std::copy_n(samples.data(), sample_size, m_floatbuf.data());

  m_paerror =
      Pa_WriteStream(m_stream, m_floatbuf.data(), sample_size / m_nchannels);
  if (m_paerror == paNoError || m_paerror == paOutputUnderflowed) {
    // Underflow is benign for the stream but is counted.
    const bool underflowed = (m_paerror == paOutputUnderflowed);
    if (underflowed) {
      m_underflows.fetch_add(1, std::memory_order_relaxed);
    }
    // Free space right after a write is the ring's lowest, i.e. its fullest.
    const long avail = Pa_GetStreamWriteAvailable(m_stream);
    if (avail >= 0) {
      m_last_free_frames.store(avail, std::memory_order_relaxed);
      const long prev = m_min_free_frames.load(std::memory_order_relaxed);
      if (prev < 0 || avail < prev) {
        m_min_free_frames.store(avail, std::memory_order_relaxed);
      }
    }
    // The start-up underflow of the first block is not a stall.
    if (underflowed && !first_block && m_adapt_cap_frames > 0) {
      return adapt();
    }
    return true;
  } else {
    add_paerror("Pa_WriteStream()");
  }
  return false;
}

// Write silence_frames of silence.
bool PortAudioOutput::write_silence(long silence_frames) {
  const std::vector<float> silence(
      static_cast<std::size_t>(silence_frames) * m_nchannels, 0.0f);
  m_paerror = Pa_WriteStream(m_stream, silence.data(),
                             static_cast<unsigned long>(silence_frames));
  if (m_paerror == paOutputUnderflowed) {
    m_underflows.fetch_add(1, std::memory_order_relaxed);
  } else if (m_paerror != paNoError) {
    add_paerror("Pa_WriteStream()");
    return false;
  }
  return true;
}

// Apply the prefill once, before the first audio block.
bool PortAudioOutput::prime(long block_frames) {
  m_primed = true;
  if (m_prefill_ms <= 0.0 && m_adapt_max_ms <= 0.0) {
    return true;
  }
  if (m_capacity_frames < 0 || m_samplerate == 0) {
    fmt::println(stderr, "WARNING: PortAudio ring capacity unknown, "
                         "prefill disabled");
    m_adapt_cap_frames = 0;
    return true;
  }
  // The prefill plus one block must fit in the ring; otherwise the blocking
  // write would only wait for the device to drain it, i.e. stall.
  const long limit = m_capacity_frames - block_frames;
  const auto to_frames = [this](double ms) {
    return static_cast<long>(ms * m_samplerate / 1000.0);
  };
  long frames = to_frames(m_prefill_ms);
  long cap = to_frames(m_adapt_max_ms);
  if (frames > limit || cap > limit) {
    fmt::println(stderr,
                 "WARNING: PortAudio prefill clamped to ring capacity "
                 "{:.1f} ms minus one block {:.1f} ms = {:.1f} ms",
                 frames_to_ms(m_capacity_frames, m_samplerate),
                 frames_to_ms(block_frames, m_samplerate),
                 frames_to_ms(std::max(limit, 0L), m_samplerate));
  }
  frames = std::clamp(frames, 0L, std::max(limit, 0L));
  m_adapt_cap_frames = std::clamp(cap, 0L, std::max(limit, 0L));
  if (frames > 0) {
    if (!write_silence(frames)) {
      return false;
    }
    m_prefill_total_frames.store(frames, std::memory_order_relaxed);
  }
  return true;
}

// Adaptive prefill step, called after an underflow was reported.
bool PortAudioOutput::adapt() {
  const long total = m_prefill_total_frames.load(std::memory_order_relaxed);
  // Keep one (largest seen) block of room, like prime().
  const long cap =
      std::min(m_adapt_cap_frames, m_capacity_frames - m_max_block_frames);
  const long step = static_cast<long>(adapt_step_ms * m_samplerate / 1000.0);
  const long add = std::min(step, cap - total);
  if (add <= 0) {
    return true;
  }
  // Never block: the ring must have room for the quantum.
  if (Pa_GetStreamWriteAvailable(m_stream) < add) {
    return true;
  }
  if (!write_silence(add)) {
    return false;
  }
  m_prefill_total_frames.store(total + add, std::memory_order_relaxed);
  return true;
}

// Return the output counters.
AudioOutput::Telemetry PortAudioOutput::telemetry() const {
  Telemetry t;
  t.underflows = m_underflows.load(std::memory_order_relaxed);
  t.capacity_frames = m_capacity_frames;
  t.min_free_frames = m_min_free_frames.load(std::memory_order_relaxed);
  t.last_free_frames = m_last_free_frames.load(std::memory_order_relaxed);
  t.prefill_frames = m_prefill_total_frames.load(std::memory_order_relaxed);
  t.sample_rate = m_samplerate;
  return t;
}

// Terminate PortAudio
// then add PortAudio error string to m_error and set m_zombie flag.
void PortAudioOutput::add_paerror(const std::string &premsg) {
  Pa_Terminate();
  const std::string addmsg =
      fmt::format("{}: PortAudio error: (number: {} message: {})", premsg,
                  m_paerror, Pa_GetErrorText(m_paerror));
  m_error.append(addmsg);
  m_zombie = true;
}

// end
