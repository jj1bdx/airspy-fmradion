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

#ifndef INCLUDE_REALTIMETHREAD_H
#define INCLUDE_REALTIMETHREAD_H

#include <memory>

/// Real-time scheduling of the decode thread (macOS only): a time-constraint
/// policy, and an interval workgroup that is started and finished once per
/// block. On other platforms start() only prints a warning.
class RealtimeThread {
public:
  /// Declared computation time as a fraction of the block period.
  static constexpr double computation_fraction = 0.25;

  /// Prepare for a block period of period_sec; quiet suppresses the info line.
  RealtimeThread(double period_sec, bool quiet);

  /// Leave the workgroup if joined.
  ~RealtimeThread();

  RealtimeThread(const RealtimeThread &) = delete;
  RealtimeThread &operator=(const RealtimeThread &) = delete;

  /// Apply the policy to the calling thread, then join the workgroup.
  /// Return false if the policy failed; a workgroup failure only warns.
  bool start();

  /// Leave and release the workgroup; safe to call twice.
  void stop();

  /// Mark one block cycle: begin on construction, finish on destruction or
  /// finish(). A null RealtimeThread makes this a no-op.
  class Cycle {
  public:
    explicit Cycle(RealtimeThread *rt) : m_rt(rt) {
      if (m_rt != nullptr) {
        m_rt->begin_cycle();
      }
    }
    ~Cycle() { finish(); }
    void finish() {
      if (m_rt != nullptr) {
        m_rt->end_cycle();
        m_rt = nullptr;
      }
    }
    Cycle(const Cycle &) = delete;
    Cycle &operator=(const Cycle &) = delete;

  private:
    RealtimeThread *m_rt;
  };

private:
  void begin_cycle();
  void end_cycle();

  struct Impl;
  std::unique_ptr<Impl> m_impl;
};

#endif // INCLUDE_REALTIMETHREAD_H
