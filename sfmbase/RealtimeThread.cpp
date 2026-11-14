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
#include <pthread.h>
#endif // __APPLE__

#include "RealtimeThread.h"

#ifdef __APPLE__

namespace {
// Largest block period in Mach ticks; the policy fields are 32 bits wide.
constexpr double max_period_abs = 4294967295.0;
// Computation limits accepted by the kernel.
constexpr double min_computation_ns = 50.0e3;
constexpr double max_computation_ns = 50.0e6;
// Interval between policy checks in policy_held().
constexpr double policy_check_interval_ns = 2.0e9;
} // namespace

struct RealtimeThread::Impl {
  double period_sec = 0.0;
  bool quiet = false;
  mach_timebase_info_data_t timebase{};
  std::uint64_t period_abs = 0;     // Mach absolute time units
  std::uint64_t computation_ns = 0; // as reported by the kernel
  std::uint64_t next_check_abs = 0;
  std::uint64_t check_interval_abs = 0;
  pthread_t owner{};       // the thread that called start()
  bool policy_set = false; // time-constraint policy applied
  bool stopped = false;
  os_workgroup_interval_t workgroup = nullptr;
  os_workgroup_join_token_s token;
  bool joined = false;
  bool started = false; // interval started and not yet finished
  unsigned int errors = 0;
  const char *first_error_what = nullptr;
  int first_error = 0;

  // Called on the decode thread: count only; stop() prints.
  void interval_error(const char *what, int err) {
    if (errors++ == 0) {
      first_error_what = what;
      first_error = err;
    }
  }

  double abs_to_ns(std::uint64_t abs) const {
    return static_cast<double>(abs) * timebase.numer / timebase.denom;
  }

  std::uint32_t ns_to_abs(double ns) const {
    return static_cast<std::uint32_t>(ns * timebase.denom / timebase.numer);
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
  const double period_ns = d.period_sec * 1.0e9;
  if (!(d.period_sec > 0.0) ||
      mach_timebase_info(&d.timebase) != KERN_SUCCESS ||
      d.timebase.numer == 0 || d.timebase.denom == 0 ||
      !(period_ns * d.timebase.denom / d.timebase.numer <= max_period_abs)) {
    fmt::println(stderr, "WARNING: --realtime: invalid block period or "
                         "timebase; running without it");
    return false;
  }
  // The kernel raises a computation below constraint / 2 to that value, so
  // request it explicitly. Above 100 ms the computation is clamped to the
  // kernel's 50 ms limit and the constraint stays at the period.
  const double computation_ns =
      std::clamp(period_ns / 2.0, min_computation_ns, max_computation_ns);
  thread_time_constraint_policy_data_t policy;
  policy.period = d.ns_to_abs(period_ns);
  policy.computation = d.ns_to_abs(computation_ns);
  policy.constraint = policy.period;
  policy.preemptible = TRUE; // ignored by the kernel
  d.period_abs = policy.period;
  d.check_interval_abs = d.ns_to_abs(policy_check_interval_ns);
  d.owner = pthread_self();

  const mach_port_t thread = pthread_mach_thread_np(d.owner);
  const kern_return_t kr =
      thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY,
                        reinterpret_cast<thread_policy_t>(&policy),
                        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  if (kr != KERN_SUCCESS) {
    fmt::println(stderr,
                 "WARNING: --realtime: thread_policy_set() failed ({}); "
                 "running without it",
                 mach_error_string(kr));
    return false;
  }
  d.policy_set = true;

  // Read back what the kernel stored.
  thread_time_constraint_policy_data_t applied = policy;
  mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
  boolean_t get_default = FALSE;
  bool readback = true;
  if (thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY,
                        reinterpret_cast<thread_policy_t>(&applied), &count,
                        &get_default) != KERN_SUCCESS ||
      get_default) {
    fmt::println(stderr,
                 "WARNING: --realtime: thread_policy_get() did not return the "
                 "applied policy; showing requested values");
    applied = policy;
    readback = false;
  }
  d.computation_ns =
      static_cast<std::uint64_t>(d.abs_to_ns(applied.computation));
  d.next_check_abs = mach_absolute_time() + d.check_interval_abs;

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
    fmt::println(
        stderr,
        "realtime: {} period {:.3f} ms ({} abs), computation {:.3f} ms "
        "({} abs), constraint {:.3f} ms ({} abs), workgroup {}",
        readback ? "kernel" : "requested", d.abs_to_ns(applied.period) * 1.0e-6,
        applied.period, d.abs_to_ns(applied.computation) * 1.0e-6,
        applied.computation, d.abs_to_ns(applied.constraint) * 1.0e-6,
        applied.constraint, wg_joined ? "joined" : "not joined");
  }
  return true;
}

void RealtimeThread::stop() {
  Impl &d = *m_impl;
  if (d.stopped) {
    return;
  }
  d.stopped = true;
  // Leaving the workgroup and the policy change belong to the thread that
  // called start().
  const bool on_owner = d.policy_set && pthread_equal(pthread_self(), d.owner);
  if (d.joined && on_owner) {
    os_workgroup_leave(d.workgroup, &d.token);
  }
  d.joined = false;
  if (d.workgroup != nullptr) {
    os_release(d.workgroup);
    d.workgroup = nullptr;
  }
  if (on_owner) {
    thread_extended_policy_data_t normal;
    normal.timeshare = TRUE;
    thread_policy_set(pthread_mach_thread_np(d.owner), THREAD_EXTENDED_POLICY,
                      reinterpret_cast<thread_policy_t>(&normal),
                      THREAD_EXTENDED_POLICY_COUNT);
  } else if (d.policy_set) {
    fmt::println(stderr, "WARNING: --realtime: stop() called from another "
                         "thread; scheduling policy not restored");
  }
  d.policy_set = false;
  if (d.errors > 0 && !d.quiet) {
    fmt::println(stderr,
                 "realtime: {} workgroup interval errors (first: {} "
                 "failed, {})",
                 d.errors, d.first_error_what, strerror(d.first_error));
  }
}

std::uint64_t RealtimeThread::computation_ns() const {
  return m_impl->computation_ns;
}

bool RealtimeThread::policy_held() {
  Impl &d = *m_impl;
  if (!d.policy_set) {
    return true;
  }
  const std::uint64_t now = mach_absolute_time();
  if (now < d.next_check_abs) {
    return true;
  }
  d.next_check_abs = now + d.check_interval_abs;
  thread_time_constraint_policy_data_t current;
  mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
  boolean_t get_default = FALSE;
  if (thread_policy_get(pthread_mach_thread_np(pthread_self()),
                        THREAD_TIME_CONSTRAINT_POLICY,
                        reinterpret_cast<thread_policy_t>(&current), &count,
                        &get_default) != KERN_SUCCESS) {
    return true; // unknown; do not report a loss
  }
  return !get_default;
}

// The interval covers the decode work of one block. It starts when the block
// is received, so a backlog drained back to back gets current timestamps.
// Must run on the thread that called start(): os_workgroup_interval_start()
// from a thread outside the workgroup aborts the process.
void RealtimeThread::begin_cycle() {
  Impl &d = *m_impl;
  if (!d.joined) {
    return;
  }
  if (!pthread_equal(pthread_self(), d.owner)) {
    d.interval_error("os_workgroup_interval_start() (wrong thread)", EPERM);
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

std::uint64_t RealtimeThread::computation_ns() const { return 0; }

bool RealtimeThread::policy_held() { return true; }

void RealtimeThread::begin_cycle() {}

void RealtimeThread::end_cycle() {}

#endif // __APPLE__

// end
