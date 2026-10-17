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

#ifndef INCLUDE_STATUSPRINTER_H
#define INCLUDE_STATUSPRINTER_H

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>

#include "SoftFM.h"

class AudioOutput;
class Source;
template <class Element> class DataBuffer;

/// Status values published by the decode loop (one consistent snapshot).
struct StatusSnapshot {
  uint64_t block = 0;
  float ppm = 0.0f;
  float if_db = 0.0f;
  float af_db = 0.0f;
  float pilot = 0.0f;
  float agc_db = 0.0f;
  /// Number of stereo detected/lost transitions so far.
  uint32_t stereo_events = 0;
};

/// Prints the status line from its own thread so that a blocked stderr never
/// stalls the decode loop. The decode loop never blocks on this object.
class StatusPrinter {
public:
  /// Interval between checks for a new snapshot; a print happens only when
  /// one was published since the last print.
  /// Set to 50msec for smooth updates.
  static constexpr std::chrono::milliseconds interval{50};

  /// Start the printer thread.
  StatusPrinter(ModType modtype, bool verbose, const AudioOutput *output,
                const DataBuffer<IQSample> *buffer, const Source *source);

  /// Destructor; stops the thread.
  ~StatusPrinter();

  StatusPrinter(const StatusPrinter &) = delete;
  StatusPrinter &operator=(const StatusPrinter &) = delete;

  /// Publish a snapshot unless the printer holds the lock; return success.
  bool try_publish(const StatusSnapshot &snap);

  /// Publish a snapshot, waiting for the lock (used once at exit).
  void publish(const StatusSnapshot &snap);

  /// Print the last snapshot and join the thread; safe to call twice.
  void stop();

private:
  void run();
  void print();

  const ModType m_modtype;
  const bool m_verbose;
  const AudioOutput *m_output;
  const DataBuffer<IQSample> *m_buffer;
  const Source *m_source;
  std::mutex m_mutex;
  std::condition_variable m_cond;
  StatusSnapshot m_snap;
  bool m_fresh = false;
  bool m_stop = false;
  // Printer-thread state only.
  uint32_t m_printed_events = 0;
  bool m_stereo = false;
  // Declared last: started after the other members are initialized.
  std::thread m_thread;
};

#endif // INCLUDE_STATUSPRINTER_H
