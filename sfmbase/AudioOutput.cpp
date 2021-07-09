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

#include "sndfile.h"
#define _FILE_OFFSET_BITS 64

#include <algorithm>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "AudioOutput.h"
#include "SoftFM.h"

// class AudioOutput

// Encode a list of samples as signed 16-bit little-endian integers.
void AudioOutput::samplesToInt16(const SampleVector &samples,
                                 std::vector<uint8_t> &bytes) {
  bytes.resize(2 * samples.size());

  SampleVector::const_iterator i = samples.begin();
  SampleVector::const_iterator n = samples.end();
  std::vector<uint8_t>::iterator k = bytes.begin();

  while (i != n) {
    Sample s = *(i++);
    // Limit output within [-1.0, 1.0].
    s = std::max(Sample(-1.0), std::min(Sample(1.0), s));
    // Convert output to [-32767, 32767].
    long v = lrint(s * 32767);
    unsigned long u = v;
    *(k++) = u & 0xff;
    *(k++) = (u >> 8) & 0xff;
  }
}

// Encode a list of samples as signed 32-bit little-endian floats.
// Note: no output range limitation.
void AudioOutput::samplesToFloat32(const SampleVector &samples,
                                   std::vector<uint8_t> &bytes) {
  bytes.resize(4 * samples.size());

  SampleVector::const_iterator i = samples.begin();
  SampleVector::const_iterator n = samples.end();
  std::vector<uint8_t>::iterator k = bytes.begin();

  while (i != n) {
    // Union for converting float and uint32_t.
    union {
      float f;
      uint32_t u32;
    } v;
    Sample s = *(i++);
    // Note: no output range limitation.
    v.f = (float)s;
    uint32_t u = v.u32;
    *(k++) = u & 0xff;
    *(k++) = (u >> 8) & 0xff;
    *(k++) = (u >> 16) & 0xff;
    *(k++) = (u >> 24) & 0xff;
  }
}

// class SndfileOutput

// Constructor
SndfileOutput::SndfileOutput(const std::string &filename,
                             unsigned int samplerate, bool stereo, int format)
    : numberOfChannels(stereo ? 2 : 1), sampleRate(samplerate) {

  if (filename == "-") {
    m_fd = STDOUT_FILENO;
  } else {
    m_fd = open(filename.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (m_fd < 0) {
      m_error = "can not open '" + filename + "' (" + strerror(errno) + ")";
      m_zombie = true;
      return;
    }
  }

  m_sndfile_sfinfo.format = format;
  m_sndfile_sfinfo.samplerate = sampleRate;
  m_sndfile_sfinfo.channels = numberOfChannels;

  if (!sf_format_check(&m_sndfile_sfinfo)) {
    m_error = "SF_INFO for file '" + filename + "' is invalid";
    m_zombie = true;
    return;
  }

  m_sndfile = sf_open_fd(m_fd, SFM_WRITE, &m_sndfile_sfinfo, SF_TRUE);
  if (m_sndfile == nullptr) {
    m_error =
        "can not open '" + filename + "' (" + sf_strerror(m_sndfile) + ")";
    m_zombie = true;
    return;
  }

  int filetype = m_sndfile_sfinfo.format & SF_FORMAT_TYPEMASK;

  // For RF64 file, downgrade to WAV if filesize is smaller than 4GB
  if (filetype == SF_FORMAT_RF64) {
    if (SF_TRUE !=
        sf_command(m_sndfile, SFC_RF64_AUTO_DOWNGRADE, NULL, SF_TRUE)) {
      m_error = "unable to set SFC_RF64_AUTO_DOWNGRADE to SF_TRUE on '" +
                filename + "' (" + sf_strerror(m_sndfile) + ")";
      m_zombie = true;
      return;
    }
  }

  // For filetypes with header, header is updated for every sf_write() calls
  if ((filetype == SF_FORMAT_RF64) || (filetype == SF_FORMAT_WAV)) {
    if (SF_TRUE !=
        sf_command(m_sndfile, SFC_SET_UPDATE_HEADER_AUTO, NULL, SF_TRUE)) {
      m_error = "unable to set SFC_SET_UPDATE_HEADER_AUTO to SF_TRUE on '" +
                filename + "' (" + sf_strerror(m_sndfile) + ")";
      m_zombie = true;
      return;
    }
  }

  m_device_name = "SndfileOutput";
}

// Destructor.
SndfileOutput::~SndfileOutput() {
  // Nothing special to handle m_zombie..
  // Done writing the file
  if (m_sndfile) {
    sf_close(m_sndfile);
  }
}

// Write audio data.
bool SndfileOutput::write(const SampleVector &samples) {
  // Fail if zombied.
  if (m_zombie) {
    return false;
  }

  sf_count_t size = samples.size();
  // Write samples to file with items.
  sf_count_t k = sf_write_double(m_sndfile, samples.data(), size);
  if (k != size) {
    m_error = "write failed (";
    m_error += sf_strerror(m_sndfile);
    m_error += ")";
    return false;
  }
  return true;
}

// Class PortAudioOutput

// Construct PortAudio output stream.
PortAudioOutput::PortAudioOutput(const PaDeviceIndex device_index,
                                 unsigned int samplerate, bool stereo) {
  m_nchannels = stereo ? 2 : 1;

  m_paerror = Pa_Initialize();
  if (m_paerror != paNoError) {
    add_paerror("Pa_Initialize()");
    return;
  }

  if (device_index == -1) {
    m_outputparams.device = Pa_GetDefaultOutputDevice();
  } else {
    PaDeviceIndex index = static_cast<PaDeviceIndex>(device_index);
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
  m_outputparams.suggestedLatency =
      Pa_GetDeviceInfo(m_outputparams.device)->defaultHighOutputLatency;
  m_outputparams.hostApiSpecificStreamInfo = NULL;

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
}

// Destructor.
PortAudioOutput::~PortAudioOutput() {
  m_paerror = Pa_StopStream(m_stream);
  if (m_paerror != paNoError) {
    add_paerror("Pa_StopStream()");
    return;
  }
  Pa_Terminate();
}

// Write audio data.
bool PortAudioOutput::write(const SampleVector &samples) {
  if (m_zombie) {
    return false;
  }

  unsigned long sample_size = samples.size();
  // Convert samples to bytes.
  samplesToFloat32(samples, m_bytebuf);

  m_paerror =
      Pa_WriteStream(m_stream, m_bytebuf.data(), sample_size / m_nchannels);
  if (m_paerror == paNoError) {
    return true;
  } else if (m_paerror == paOutputUnderflowed) {
    // This error is benign
    // fprintf(stderr, "paOutputUnderflowed\n");
    return true;
  } else
    add_paerror("Pa_WriteStream()");
  return false;
}

// Terminate PortAudio
// then add PortAudio error string to m_error and set m_zombie flag.
void PortAudioOutput::add_paerror(const std::string &premsg) {
  Pa_Terminate();
  m_error += premsg;
  m_error += ": PortAudio error: (number: ";
  m_error += std::to_string(m_paerror);
  m_error += " message: ";
  m_error += Pa_GetErrorText(m_paerror);
  m_error += ")";
  m_zombie = true;
}

/* end */
