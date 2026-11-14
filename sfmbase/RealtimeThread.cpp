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
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fmt/format.h>
#include <mutex>
#include <string>
#include <vector>
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
// Interval between policy checks in policy_held() and ProducerPolicy::tick().
constexpr double policy_check_interval_ns = 2.0e9;

std::uint32_t ns_to_abs(const mach_timebase_info_data_t &tb, double ns) {
  return static_cast<std::uint32_t>(ns * tb.denom / tb.numer);
}

double abs_to_ns(const mach_timebase_info_data_t &tb, std::uint64_t abs) {
  return static_cast<double>(abs) * tb.numer / tb.denom;
}

// Build the policy for a block period: constraint = period, computation =
// half the constraint (the kernel's minimum) clamped to the kernel's limits.
// Return false if the period or the timebase is unusable.
bool make_time_constraint(double period_sec, mach_timebase_info_data_t &tb,
                          thread_time_constraint_policy_data_t &policy) {
  const double period_ns = period_sec * 1.0e9;
  if (!(period_sec > 0.0) || mach_timebase_info(&tb) != KERN_SUCCESS ||
      tb.numer == 0 || tb.denom == 0 ||
      !(period_ns * tb.denom / tb.numer <= max_period_abs)) {
    return false;
  }
  // The kernel raises a computation below constraint / 2 to that value, so
  // request it explicitly. Above 100 ms the computation is clamped to the
  // kernel's 50 ms limit and the constraint stays at the period.
  const double computation_ns =
      std::clamp(period_ns / 2.0, min_computation_ns, max_computation_ns);
  policy.period = ns_to_abs(tb, period_ns);
  policy.computation = ns_to_abs(tb, computation_ns);
  policy.constraint = policy.period;
  policy.preemptible = TRUE; // ignored by the kernel
  return true;
}

// Read the stored time-constraint policy. Return false if the thread has none
// or the query failed (kr is then not KERN_SUCCESS).
bool get_time_constraint(mach_port_t thread,
                         thread_time_constraint_policy_data_t &out,
                         kern_return_t &kr) {
  mach_msg_type_number_t count = THREAD_TIME_CONSTRAINT_POLICY_COUNT;
  boolean_t get_default = FALSE;
  kr = thread_policy_get(thread, THREAD_TIME_CONSTRAINT_POLICY,
                         reinterpret_cast<thread_policy_t>(&out), &count,
                         &get_default);
  return kr == KERN_SUCCESS && !get_default;
}

// Set the policy on a thread and read back what the kernel stored. readback is
// false if the read failed; applied then holds the requested values.
kern_return_t set_time_constraint(mach_port_t thread,
                                  thread_time_constraint_policy_data_t policy,
                                  thread_time_constraint_policy_data_t &applied,
                                  bool &readback) {
  const kern_return_t kr =
      thread_policy_set(thread, THREAD_TIME_CONSTRAINT_POLICY,
                        reinterpret_cast<thread_policy_t>(&policy),
                        THREAD_TIME_CONSTRAINT_POLICY_COUNT);
  applied = policy;
  readback = false;
  if (kr == KERN_SUCCESS) {
    thread_time_constraint_policy_data_t got;
    kern_return_t gkr;
    if (get_time_constraint(thread, got, gkr)) {
      applied = got;
      readback = true;
    }
  }
  return kr;
}
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
    return ::abs_to_ns(timebase, abs);
  }

  std::uint32_t ns_to_abs(double ns) const { return ::ns_to_abs(timebase, ns); }
};

RealtimeThread::RealtimeThread(double period_sec, bool quiet)
    : m_impl(std::make_unique<Impl>()) {
  m_impl->period_sec = period_sec;
  m_impl->quiet = quiet;
}

RealtimeThread::~RealtimeThread() { stop(); }

bool RealtimeThread::start() {
  Impl &d = *m_impl;
  thread_time_constraint_policy_data_t policy;
  if (!make_time_constraint(d.period_sec, d.timebase, policy)) {
    fmt::println(stderr, "WARNING: --realtime: invalid block period or "
                         "timebase; running without it");
    return false;
  }
  d.period_abs = policy.period;
  d.check_interval_abs = d.ns_to_abs(policy_check_interval_ns);
  d.owner = pthread_self();

  const mach_port_t thread = pthread_mach_thread_np(d.owner);
  thread_time_constraint_policy_data_t applied;
  bool readback = false;
  const kern_return_t kr =
      set_time_constraint(thread, policy, applied, readback);
  if (kr != KERN_SUCCESS) {
    fmt::println(stderr,
                 "WARNING: --realtime: thread_policy_set() failed ({}); "
                 "running without it",
                 mach_error_string(kr));
    return false;
  }
  d.policy_set = true;

  if (!readback) {
    fmt::println(stderr,
                 "WARNING: --realtime: thread_policy_get() did not return the "
                 "applied policy; showing requested values");
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
  kern_return_t kr;
  if (get_time_constraint(pthread_mach_thread_np(pthread_self()), current,
                          kr)) {
    return true;
  }
  return kr != KERN_SUCCESS; // a failed query is unknown, not a loss
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

// ProducerPolicy: time-constraint policy for SDR producer threads.

namespace {
// Unique id of a thread given its port; 0 if the thread is gone.
std::uint64_t thread_unique_id(mach_port_t thread) {
  thread_identifier_info_data_t info;
  mach_msg_type_number_t count = THREAD_IDENTIFIER_INFO_COUNT;
  if (thread_info(thread, THREAD_IDENTIFIER_INFO,
                  reinterpret_cast<thread_info_t>(&info),
                  &count) != KERN_SUCCESS) {
    return 0;
  }
  return info.thread_id;
}

std::uint64_t current_unique_id() {
  std::uint64_t id = 0;
  pthread_threadid_np(nullptr, &id);
  return id;
}
} // namespace

struct ProducerPolicy::Impl {
  struct Entry {
    mach_port_t port; // owned send right
    std::uint64_t id;
    int state; // 0 not checked, 1 held, 2 lost
  };

  std::string name;
  bool quiet = false;
  bool valid = false;
  mach_timebase_info_data_t timebase{};
  thread_time_constraint_policy_data_t policy{};
  std::uint64_t check_interval_abs = 0;

  std::mutex ready_mutex; // guards ready
  std::condition_variable ready_cv;
  bool ready = false;

  std::mutex mutex; // guards everything below
  std::vector<Entry> entries;
  thread_time_constraint_policy_data_t applied{};
  bool readback = false;
  const char *how = "";
  std::string warning;     // set by apply_self(); printed by tick()
  bool report_due = false; // a result is ready to be printed by tick()
  std::uint64_t callback_id = 0;
  std::uint64_t next_check_abs = 0;
};

ProducerPolicy::ProducerPolicy(const char *name, double period_sec, bool quiet)
    : m_impl(std::make_unique<Impl>()) {
  Impl &d = *m_impl;
  d.name = name;
  d.quiet = quiet;
  d.valid = make_time_constraint(period_sec, d.timebase, d.policy);
  if (!d.valid) {
    fmt::println(stderr,
                 "WARNING: --realtime: {}: invalid block period or timebase; "
                 "producer threads run without it",
                 d.name);
    d.ready = true; // nothing to wait for
    return;
  }
  d.check_interval_abs = ns_to_abs(d.timebase, policy_check_interval_ns);
  d.next_check_abs = mach_absolute_time() + d.check_interval_abs;
}

ProducerPolicy::~ProducerPolicy() {
  for (const Impl::Entry &e : m_impl->entries) {
    mach_port_deallocate(mach_task_self(), e.port);
  }
}

void ProducerPolicy::ThreadSet::release() {
  for (const std::uint32_t port : m_ports) {
    if (port != MACH_PORT_NULL) {
      mach_port_deallocate(mach_task_self(), port);
    }
  }
  m_ports.clear();
  m_ids.clear();
}

bool ProducerPolicy::apply_self(const char *how) {
  Impl &d = *m_impl;
  if (!d.valid) {
    return false;
  }
  // The send right from mach_thread_self() is kept for later checks.
  const mach_port_t self = mach_thread_self();
  thread_time_constraint_policy_data_t applied;
  bool readback = false;
  const kern_return_t kr =
      set_time_constraint(self, d.policy, applied, readback);
  const std::lock_guard<std::mutex> lock(d.mutex);
  if (kr != KERN_SUCCESS) {
    mach_port_deallocate(mach_task_self(), self);
    d.warning = fmt::format(
        "WARNING: --realtime: {}: thread_policy_set() failed ({}); the "
        "thread runs without it",
        d.name, mach_error_string(kr));
    d.report_due = true;
    return false;
  }
  d.entries.push_back({self, current_unique_id(), 0});
  d.applied = applied;
  d.readback = readback;
  d.how = how;
  d.report_due = true;
  return true;
}

ProducerPolicy::ThreadSet ProducerPolicy::snapshot() {
  ThreadSet set;
  thread_act_array_t list = nullptr;
  mach_msg_type_number_t count = 0;
  if (task_threads(mach_task_self(), &list, &count) != KERN_SUCCESS) {
    return set;
  }
  for (mach_msg_type_number_t i = 0; i < count; i++) {
    const std::uint64_t id = thread_unique_id(list[i]);
    if (id == 0) {
      mach_port_deallocate(mach_task_self(), list[i]); // already exited
    } else {
      set.m_ports.push_back(list[i]);
      set.m_ids.push_back(id);
    }
  }
  vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(list),
                count * sizeof(thread_act_t));
  return set;
}

bool ProducerPolicy::apply_new(const ThreadSet &before, ThreadSet &after,
                               std::size_t expected) {
  Impl &d = *m_impl;
  bool ok = false;
  if (d.valid) {
    std::vector<std::size_t> fresh;
    for (std::size_t i = 0; i < after.m_ids.size(); i++) {
      if (std::find(before.m_ids.begin(), before.m_ids.end(), after.m_ids[i]) ==
          before.m_ids.end()) {
        fresh.push_back(i);
      }
    }
    if (fresh.size() != expected) {
      fmt::println(stderr,
                   "WARNING: --realtime: {}: {} new threads after the start "
                   "call (expected {}); using the callback thread only",
                   d.name, fresh.size(), expected);
    } else {
      std::size_t done = 0;
      for (const std::size_t i : fresh) {
        thread_time_constraint_policy_data_t applied;
        bool readback = false;
        const kern_return_t kr =
            set_time_constraint(after.m_ports[i], d.policy, applied, readback);
        if (kr != KERN_SUCCESS) {
          fmt::println(stderr,
                       "WARNING: --realtime: {}: thread_policy_set() failed "
                       "({}) on a library thread",
                       d.name, mach_error_string(kr));
          continue;
        }
        const std::lock_guard<std::mutex> lock(d.mutex);
        d.entries.push_back({after.m_ports[i], after.m_ids[i], 0});
        after.m_ports[i] = MACH_PORT_NULL; // ownership moved to the entry
        d.applied = applied;
        d.readback = readback;
        done++;
      }
      ok = (done == expected);
      if (done > 0) {
        const std::lock_guard<std::mutex> lock(d.mutex);
        d.how = "threads created by the start call";
        d.report_due = true;
      }
    }
  }
  // Without a full result, the first callback thread gets the policy.
  m_fallback.store(!ok, std::memory_order_release);
  m_decided.store(true, std::memory_order_release);
  return ok;
}

void ProducerPolicy::mark_ready() {
  Impl &d = *m_impl;
  {
    const std::lock_guard<std::mutex> lock(d.ready_mutex);
    d.ready = true;
  }
  d.ready_cv.notify_all();
}

bool ProducerPolicy::wait_ready(std::chrono::milliseconds timeout) {
  Impl &d = *m_impl;
  std::unique_lock<std::mutex> lock(d.ready_mutex);
  if (d.ready_cv.wait_for(lock, timeout, [&d] { return d.ready; })) {
    return true;
  }
  lock.unlock();
  fmt::println(stderr,
               "WARNING: --realtime: {}: the source did not finish starting "
               "within {} ms; continuing",
               d.name, timeout.count());
  return false;
}

void ProducerPolicy::first_callback() {
  Impl &d = *m_impl;
  if (!m_decided.load(std::memory_order_acquire)) {
    return; // apply_new() has not finished; try again on the next block
  }
  const std::uint64_t id = current_unique_id();
  if (m_fallback.load(std::memory_order_acquire) && d.valid) {
    apply_self("callback thread, fallback");
  }
  {
    const std::lock_guard<std::mutex> lock(d.mutex);
    d.callback_id = id;
  }
  m_callback_seen.store(true, std::memory_order_release);
}

void ProducerPolicy::check() {
  Impl &d = *m_impl;
  const std::lock_guard<std::mutex> lock(d.mutex);
  for (Impl::Entry &e : d.entries) {
    thread_time_constraint_policy_data_t current;
    kern_return_t kr;
    if (get_time_constraint(e.port, current, kr)) {
      e.state = 1;
    } else if (kr == KERN_SUCCESS) {
      e.state = 2; // the thread has the default policy: lost
    } // a failed query (exited thread) keeps the previous state
  }
}

void ProducerPolicy::tick() {
  Impl &d = *m_impl;
  if (!d.valid) {
    return;
  }
  bool due = false;
  std::string message;
  {
    const std::lock_guard<std::mutex> lock(d.mutex);
    const std::uint64_t now = mach_absolute_time();
    due = now >= d.next_check_abs;
    if (due) {
      d.next_check_abs = now + d.check_interval_abs;
    }
    if (d.report_due) {
      d.report_due = false;
      if (!d.warning.empty()) {
        message = std::move(d.warning);
        d.warning.clear();
      } else if (!d.quiet) {
        message = fmt::format(
            "realtime: {}: policy on {} thread(s) ({}): constraint {:.3f} ms "
            "({} abs), computation {:.3f} ms ({} abs), {} values",
            d.name, d.entries.size(), d.how,
            abs_to_ns(d.timebase, d.applied.constraint) * 1.0e-6,
            d.applied.constraint,
            abs_to_ns(d.timebase, d.applied.computation) * 1.0e-6,
            d.applied.computation, d.readback ? "kernel" : "requested");
      }
    }
  }
  // Print outside the lock: a blocked stderr must not hold up the callback.
  if (!message.empty()) {
    fmt::println(stderr, "{}", message);
  }
  if (due) {
    check();
  }
}

std::string ProducerPolicy::summary() {
  Impl &d = *m_impl;
  const std::lock_guard<std::mutex> lock(d.mutex);
  if (d.entries.empty()) {
    return std::string();
  }
  int held = 0;
  int lost = 0;
  bool callback_listed = (d.callback_id == 0);
  for (const Impl::Entry &e : d.entries) {
    held += (e.state == 1);
    lost += (e.state == 2);
    callback_listed = callback_listed || (e.id == d.callback_id);
  }
  const int unchecked = static_cast<int>(d.entries.size()) - held - lost;
  std::string line = fmt::format(
      "Summary: {} producer threads: time-constraint policy on {}, held at "
      "last check={}, lost={}, not checked={}",
      d.name, d.entries.size(), held, lost, unchecked);
  if (!callback_listed) {
    line += "; WARNING: the callback thread is not among them";
  }
  return line;
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

struct ProducerPolicy::Impl {};

ProducerPolicy::ProducerPolicy(const char *, double, bool) {}

ProducerPolicy::~ProducerPolicy() = default;

void ProducerPolicy::ThreadSet::release() {}

bool ProducerPolicy::apply_self(const char *) { return false; }

ProducerPolicy::ThreadSet ProducerPolicy::snapshot() { return ThreadSet(); }

bool ProducerPolicy::apply_new(const ThreadSet &, ThreadSet &, std::size_t) {
  return false;
}

void ProducerPolicy::mark_ready() {}

bool ProducerPolicy::wait_ready(std::chrono::milliseconds) { return true; }

void ProducerPolicy::first_callback() {}

void ProducerPolicy::check() {}

void ProducerPolicy::tick() {}

std::string ProducerPolicy::summary() { return std::string(); }

#endif // __APPLE__

// end
