// airspy-fmradion
// Software decoder for FM broadcast radio with Airspy
//
// Copyright (C) 2026 Kenji Rikitake, JJ1BDX
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

#include <cstdio>
#include <fmt/format.h>
#ifdef __APPLE__
#include <pthread.h>
#include <pthread/qos.h>
#endif // __APPLE__

#include "AudioOutput.h"
#include "DataBuffer.h"
#include "Source.h"
#include "StatusPrinter.h"

StatusPrinter::StatusPrinter(ModType modtype, bool verbose,
                             const AudioOutput *output,
                             const DataBuffer<IQSample> *buffer,
                             const Source *source)
    : m_modtype(modtype), m_verbose(verbose), m_output(output),
      m_buffer(buffer), m_source(source), m_thread(&StatusPrinter::run, this) {}

StatusPrinter::~StatusPrinter() { stop(); }

bool StatusPrinter::try_publish(const StatusSnapshot &snap) {
  std::unique_lock<std::mutex> lock(m_mutex, std::try_to_lock);
  if (!lock.owns_lock()) {
    return false;
  }
  m_snap = snap;
  m_fresh = true;
  return true;
}

void StatusPrinter::publish(const StatusSnapshot &snap) {
  std::scoped_lock<std::mutex> lock(m_mutex);
  m_snap = snap;
  m_fresh = true;
}

void StatusPrinter::stop() {
  {
    std::scoped_lock<std::mutex> lock(m_mutex);
    m_stop = true;
  }
  m_cond.notify_all();
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

void StatusPrinter::run() {
#ifdef __APPLE__
  pthread_set_qos_class_self_np(QOS_CLASS_UTILITY, 0);
#endif // __APPLE__
  for (;;) {
    bool stopping;
    {
      std::unique_lock<std::mutex> lock(m_mutex);
      m_cond.wait_for(lock, interval, [this] { return m_stop; });
      stopping = m_stop;
    }
    print();
    if (stopping) {
      break;
    }
  }
}

void StatusPrinter::print() {
  StatusSnapshot s;
  {
    std::scoped_lock<std::mutex> lock(m_mutex);
    if (!m_fresh) {
      return;
    }
    s = m_snap;
    m_fresh = false;
  }
  // Replay stereo transitions that happened since the last print.
  for (uint32_t n = s.stereo_events - m_printed_events; n > 0; n--) {
    m_stereo = !m_stereo;
    fmt::println(stderr, "\n{} stereo signal", m_stereo ? "got" : "lost");
  }
  m_printed_events = s.stereo_events;

  switch (m_modtype) {
  case ModType::FM:
    fmt::print(stderr,
               "\rblk={:11}:ppm={:+7.3f}:IF={:+6.1f}dB:AF={:+6.1f}dB:"
               "Pilot= {:8.6f}",
               s.block, s.ppm, s.if_db, s.af_db, s.pilot);
    break;
  case ModType::NBFM:
    fmt::print(stderr, "\rblk={:11}:ppm={:+7.3f}:IF={:+6.1f}dB:AF={:+6.1f}dB",
               s.block, s.ppm, s.if_db, s.af_db);
    break;
  case ModType::AM:
  case ModType::DSB:
  case ModType::USB:
  case ModType::LSB:
  case ModType::CW:
  case ModType::WSPR:
    fmt::print(stderr, "\rblk={:11}:IF={:+6.1f}dB:AGC={:+6.1f}dB:AF={:+6.1f}dB",
               s.block, s.if_db, s.agc_db, s.af_db);
    break;
  }
  if (m_verbose) {
    fmt::print(stderr, ":Q={}/{}:DBlk={}", m_buffer->queue_size(),
               m_buffer->max_queue_size(), m_buffer->dropped_blocks());
    const AudioOutput::Telemetry t = m_output->telemetry();
    if (t.capacity_frames >= 0 && t.min_free_frames >= 0) {
      fmt::print(
          stderr, ":UF={}:Ring={:.1f}/{:.1f}/{:.1f}ms", t.underflows,
          frames_to_ms(t.capacity_frames - t.last_free_frames, t.sample_rate),
          frames_to_ms(t.capacity_frames - t.min_free_frames, t.sample_rate),
          frames_to_ms(t.capacity_frames, t.sample_rate));
    }
    fmt::print(stderr, ":SDRdrop={}", m_source->dropped_samples());
  }
  fflush(stderr);
}
