//
// Copyright RIME Developers
// Distributed under the BSD License
//

#include <rime/build_config.h>

#include "logging.h"

#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

#include <rime/setup.h>

#ifdef RIME_ENABLE_LOGGING

namespace rime {

namespace {

struct LogHandlerRegistration {
  RimeLogHandler handler = nullptr;
  void* context_object = nullptr;
  std::atomic<int> active_callbacks{0};
  std::mutex mutex;
  std::condition_variable cv;
};

struct LogHandlerState {
  std::mutex mutex;
  std::shared_ptr<LogHandlerRegistration> registration;
  std::atomic<bool> installed{false};
};

LogHandlerState& log_handler_state() {
  static LogHandlerState* state = new LogHandlerState;
  return *state;
}

std::shared_ptr<LogHandlerRegistration> acquire_registration() {
  auto& state = log_handler_state();
  std::lock_guard<std::mutex> lock(state.mutex);
  auto registration = state.registration;
  if (registration) {
    registration->active_callbacks.fetch_add(1, std::memory_order_relaxed);
  }
  return registration;
}

void release_registration(
    const std::shared_ptr<LogHandlerRegistration>& registration) {
  if (!registration) {
    return;
  }
  bool last = false;
  {
    std::lock_guard<std::mutex> lock(registration->mutex);
    last = registration->active_callbacks.fetch_sub(
               1, std::memory_order_acq_rel) == 1;
  }
  if (last) {
    registration->cv.notify_all();
  }
}

thread_local const LogHandlerRegistration* t_current_registration = nullptr;

class CallbackGuard {
 public:
  explicit CallbackGuard(std::shared_ptr<LogHandlerRegistration> registration)
      : registration_(std::move(registration)),
        previous_registration_(t_current_registration) {
    t_current_registration = registration_.get();
  }

  ~CallbackGuard() {
    t_current_registration = previous_registration_;
    release_registration(registration_);
  }

 private:
  std::shared_ptr<LogHandlerRegistration> registration_;
  const LogHandlerRegistration* previous_registration_;
};

unsigned long long current_thread_id() {
  thread_local const unsigned long long id = static_cast<unsigned long long>(
      std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return id;
}

}  // namespace

RIME_DLL void SetLogHandler(RimeLogHandler handler, void* context_object) {
  auto& state = log_handler_state();
  std::shared_ptr<LogHandlerRegistration> previous;
  {
    std::lock_guard<std::mutex> lock(state.mutex);
    previous = state.registration;
    if (handler) {
      auto registration = std::make_shared<LogHandlerRegistration>();
      registration->handler = handler;
      registration->context_object = context_object;
      state.registration = std::move(registration);
      state.installed.store(true, std::memory_order_release);
    } else {
      state.registration.reset();
      state.installed.store(false, std::memory_order_release);
    }
  }
  if (!previous) {
    return;
  }
  std::unique_lock<std::mutex> lock(previous->mutex);
  const int self = t_current_registration == previous.get() ? 1 : 0;
  previous->cv.wait(lock, [&previous, self] {
    return previous->active_callbacks.load(std::memory_order_acquire) <= self;
  });
}

namespace logging_internal {

bool has_log_handler() {
  return log_handler_state().installed.load(std::memory_order_acquire);
}

bool log_is_enabled(LogSeverity severity) {
  return severity == LogSeverity::kFatal ||
         static_cast<int>(severity) >= FLAGS_minloglevel;
}

std::ostream& null_stream() {
  class NullBuffer : public std::streambuf {
   protected:
    int overflow(int c) override { return c; }
  };
  static thread_local NullBuffer buffer;
  static thread_local std::ostream stream(&buffer);
  return stream;
}

void write_log(const char* file,
               int line,
               const char* function,
               LogSeverity severity,
               std::string message) {
  auto registration = acquire_registration();
  if (registration) {
    RimeLogRecord record = {};
    RIME_STRUCT_INIT(RimeLogRecord, record);
    record.level = static_cast<int>(severity);
    record.message = message.c_str();
    record.file = file;
    record.line = line;
    record.function = function;
    record.thread_id = current_thread_id();
    bool handler_threw = false;
    try {
      CallbackGuard guard(registration);
      registration->handler(registration->context_object, &record);
    } catch (...) {
      // A throwing handler violates the contract.  Do not let the exception
      // escape through the logging macros; fall back to the default backend.
      handler_threw = true;
    }
    if (handler_threw) {
      google::LogMessage log_message(
          file ? file : "", line, static_cast<google::LogSeverity>(severity));
      log_message.stream() << message;
    }
    if (severity == LogSeverity::kFatal) {
      std::abort();
    }
    return;
  }

  {
    google::LogMessage log_message(file ? file : "", line,
                                   static_cast<google::LogSeverity>(severity));
    log_message.stream() << message;
  }
  if (severity == LogSeverity::kFatal) {
    std::abort();
  }
}

[[noreturn]] void check_failed(const char* file,
                               int line,
                               const char* function,
                               const char* expression) {
  write_log(file, line, function, LogSeverity::kFatal,
            std::string("Check failed: ") + expression);
  std::abort();
}

}  // namespace logging_internal

}  // namespace rime

#else  // RIME_ENABLE_LOGGING

namespace rime {

RIME_DLL void SetLogHandler(RimeLogHandler /*handler*/,
                            void* /*context_object*/) {}

}  // namespace rime

#endif  // RIME_ENABLE_LOGGING
