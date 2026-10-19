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

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#ifdef __APPLE__
#include <AudioToolbox/AudioWorkInterval.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <os/clock.h>
#include <os/workgroup.h>
#endif // __APPLE__

#include "RealtimeThread.h"

#ifdef __APPLE__

struct RealtimeThread::Impl {
  double period_sec = 0.0;
  bool quiet = false;
  std::uint64_t period_abs = 0; // Mach absolute time units
  os_workgroup_interval_t workgroup = nullptr;
  os_workgroup_join_token_s token;
  bool joined = false;
  bool started = false; // interval started and not yet finished
  unsigned int errors = 0;

  void interval_error(const char *what, int err) {
    if (errors++ == 0) {
      fmt::println(stderr, "\nWARNING: {} failed ({}); further errors counted",
                   what, strerror(err));
    }
  }
};

RealtimeThread::RealtimeThread(double period_sec, bool quiet)
    : m_impl(std::make_unique<Impl>()) {
  m_impl->period_sec = period_sec;
  m_impl->quiet = quiet;
}

RealtimeThread::~RealtimeThread() { stop(); }

bool RealtimeThread::start() {
  Impl &d = *m_impl;
  mach_timebase_info_data_t timebase;
  if (!(d.period_sec > 0.0) || mach_timebase_info(&timebase) != KERN_SUCCESS ||
      timebase.numer == 0) {
    fmt::println(stderr, "WARNING: --realtime: invalid block period or "
                         "timebase; running without it");
    return false;
  }
  // The computation is clamped to the kernel's accepted range (50 us to 50 ms).
  const double period_ns = d.period_sec * 1.0e9;
  const double computation_ns =
      std::clamp(period_ns * computation_fraction, 50.0e3, 50.0e6);
  const auto to_abs = [&timebase](double ns) {
    return static_cast<std::uint32_t>(ns * timebase.denom / timebase.numer);
  };
  thread_time_constraint_policy_data_t policy;
  policy.period = to_abs(period_ns);
  policy.computation = to_abs(computation_ns);
  policy.constraint = policy.period;
  policy.preemptible = TRUE;
  d.period_abs = policy.period;

  const mach_port_t thread = mach_thread_self();
  const kern_return_t kr =
      thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY,
                        reinterpret_cast<thread_policy_t>(&policy),
                        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  mach_port_deallocate(mach_task_self(), thread);
  if (kr != KERN_SUCCESS) {
    fmt::println(stderr,
                 "WARNING: --realtime: thread_policy_set() failed ({}); "
                 "running without it",
                 mach_error_string(kr));
    return false;
  }

  bool wg_joined = false;
  d.workgroup = AudioWorkIntervalCreate("airspy-fmradion decode",
                                        OS_CLOCK_MACH_ABSOLUTE_TIME, nullptr);
  if (d.workgroup == nullptr) {
    fmt::println(stderr,
                 "WARNING: --realtime: AudioWorkIntervalCreate() failed; "
                 "running without a workgroup");
  } else if (const int err = os_workgroup_join(d.workgroup, &d.token)) {
    fmt::println(stderr,
                 "WARNING: --realtime: os_workgroup_join() failed ({}); "
                 "running without a workgroup",
                 strerror(err));
    os_release(d.workgroup);
    d.workgroup = nullptr;
  } else {
    d.joined = true;
    wg_joined = true;
  }

  if (!d.quiet) {
    fmt::println(stderr,
                 "realtime: period {:.3f} ms ({} abs), computation {:.3f} ms "
                 "({} abs), constraint {:.3f} ms, preemptible, workgroup {}",
                 period_ns * 1.0e-6, policy.period, computation_ns * 1.0e-6,
                 policy.computation, period_ns * 1.0e-6,
                 wg_joined ? "joined" : "not joined");
  }
  return true;
}

void RealtimeThread::stop() {
  Impl &d = *m_impl;
  if (d.workgroup == nullptr) {
    return;
  }
  if (d.joined) {
    os_workgroup_leave(d.workgroup, &d.token);
    d.joined = false;
  }
  os_release(d.workgroup);
  d.workgroup = nullptr;
  if (d.errors > 0 && !d.quiet) {
    fmt::println(stderr, "realtime: {} workgroup interval errors", d.errors);
  }
}

// The interval covers the decode work of one block. It starts when the block
// is received, so a backlog drained back to back gets current timestamps.
void RealtimeThread::begin_cycle() {
  Impl &d = *m_impl;
  if (!d.joined) {
    return;
  }
  const std::uint64_t now = mach_absolute_time();
  if (const int err = os_workgroup_interval_start(
          d.workgroup, now, now + d.period_abs, nullptr)) {
    d.interval_error("os_workgroup_interval_start()", err);
    return;
  }
  d.started = true;
}

void RealtimeThread::end_cycle() {
  Impl &d = *m_impl;
  if (!d.started) {
    return;
  }
  d.started = false;
  if (const int err = os_workgroup_interval_finish(d.workgroup, nullptr)) {
    d.interval_error("os_workgroup_interval_finish()", err);
  }
}

#else // __APPLE__

struct RealtimeThread::Impl {};

RealtimeThread::RealtimeThread(double, bool) {}

RealtimeThread::~RealtimeThread() = default;

bool RealtimeThread::start() {
  fmt::println(stderr, "WARNING: --realtime is supported on macOS only; "
                       "ignored");
  return false;
}

void RealtimeThread::stop() {}

void RealtimeThread::begin_cycle() {}

void RealtimeThread::end_cycle() {}

#endif // __APPLE__

// end
