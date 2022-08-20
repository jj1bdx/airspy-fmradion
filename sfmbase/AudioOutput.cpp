// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2015 Edouard Griffiths, F4EXB
// Copyright (C) 2019-2022 Kenji Rikitake, JJ1BDX
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
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

#include "AudioOutput.h"
#include "SoftFM.h"

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

// Output closing method.
void PortAudioOutput::output_close() {
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
  // Set closed flag to prevent multiple closing
  m_closed = true;
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

  unsigned long sample_size = samples.size();
  m_floatbuf.resize(sample_size);

  // Convert double samples to float.
  volk_64f_convert_32f(m_floatbuf.data(), samples.data(), sample_size);

  m_paerror =
      Pa_WriteStream(m_stream, m_floatbuf.data(), sample_size / m_nchannels);
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
