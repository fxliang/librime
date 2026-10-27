//
// Copyright RIME Developers
// Distributed under the BSD License
//
#ifndef RIME_LOGGING_H_
#define RIME_LOGGING_H_

#include <atomic>
#include <cstdint>
#include <cstring>
#include <optional>
#include <ostream>
#include <sstream>
#include <streambuf>
#include <string>
#include <utility>

#ifndef _WIN32
#include <strings.h>
#endif

#include "rime_api.h"

#ifdef RIME_ENABLE_LOGGING
#include <glog/logging.h>

// glog provides the same macro names.  The macros below are a facade that can
// either forward records to an external handler or fall back to glog, so the
// glog implementations must not be visible to the rest of librime.
#undef LOG
#undef VLOG
#undef LOG_IF
#undef LOG_EVERY_N
#undef LOG_IF_EVERY_N
#undef LOG_ASSERT
#undef CHECK
#undef CHECK_EQ
#undef CHECK_NE
#undef CHECK_LE
#undef CHECK_LT
#undef CHECK_GE
#undef CHECK_GT
#undef CHECK_NOTNULL
#undef CHECK_STREQ
#undef CHECK_STRCASEEQ
#undef CHECK_STRNE
#undef CHECK_STRCASENE
#undef DLOG
#undef DVLOG
#undef DLOG_IF
#undef DLOG_EVERY_N
#undef DLOG_IF_EVERY_N
#undef DLOG_ASSERT
#undef DCHECK
#undef DCHECK_EQ
#undef DCHECK_NE
#undef DCHECK_LE
#undef DCHECK_LT
#undef DCHECK_GE
#undef DCHECK_GT
#undef DCHECK_NOTNULL
#undef DCHECK_STREQ
#undef DCHECK_STRCASEEQ
#undef DCHECK_STRNE
#undef DCHECK_STRCASENE

namespace rime {

enum class LogSeverity : int {
  kInfo = RIME_LOG_INFO,
  kWarning = RIME_LOG_WARNING,
  kError = RIME_LOG_ERROR,
  kFatal = RIME_LOG_FATAL,
};

namespace logging_internal {

//! Returns true when a log handler is installed.
RIME_DLL bool has_log_handler();

//! Returns true when a record with the given severity passes the minloglevel
//! gate.
RIME_DLL bool log_is_enabled(LogSeverity severity);

//! Emits a record to the installed handler, or to the default backend.
RIME_DLL void write_log(const char* file,
                        int line,
                        const char* function,
                        LogSeverity severity,
                        std::string message);

//! Stream used to discard arguments of disabled debug logs.
RIME_DLL std::ostream& null_stream();

[[noreturn]] RIME_DLL void check_failed(const char* file,
                                        int line,
                                        const char* function,
                                        const char* expression);

template <class T>
std::string stringify(const T& value) {
  std::ostringstream stream;
  stream << value;
  return stream.str();
}

inline std::string stringify(const char* value) {
  return value ? std::string(value) : std::string("(null)");
}

inline std::string stringify(char* value) {
  return value ? std::string(value) : std::string("(null)");
}

inline std::string stringify(std::nullptr_t) {
  return "(null)";
}

inline bool string_equal(const char* lhs, const char* rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs) {
    return false;
  }
  return std::strcmp(lhs, rhs) == 0;
}

inline bool string_not_equal(const char* lhs, const char* rhs) {
  return !string_equal(lhs, rhs);
}

inline bool string_case_equal(const char* lhs, const char* rhs) {
  if (lhs == rhs) {
    return true;
  }
  if (!lhs || !rhs) {
    return false;
  }
#ifdef _WIN32
  return _stricmp(lhs, rhs) == 0;
#else
  return strcasecmp(lhs, rhs) == 0;
#endif
}

inline bool string_case_not_equal(const char* lhs, const char* rhs) {
  return !string_case_equal(lhs, rhs);
}

inline std::int64_t& every_n_counter() {
  static thread_local std::int64_t counter = 0;
  return counter;
}

template <class T>
T check_notnull(const char* file,
                int line,
                const char* function,
                const char* expression,
                T&& value) {
  if (value == nullptr) {
    check_failed(file, line, function, expression);
  }
  return std::forward<T>(value);
}

class CheckMessage {
 public:
  CheckMessage(const char* file,
               int line,
               const char* function,
               const char* expression)
      : file_(file),
        line_(line),
        function_(function),
        expression_(expression) {}

  ~CheckMessage() {
    std::string message = "Check failed: ";
    message += expression_;
    std::string streamed = stream_.str();
    if (!streamed.empty()) {
      message += " ";
      message += streamed;
    }
    write_log(file_, line_, function_, LogSeverity::kFatal, message);
  }

  std::ostream& stream() { return stream_; }

 private:
  const char* file_;
  int line_;
  const char* function_;
  const char* expression_;
  std::ostringstream stream_;
};

class CheckOpResult {
 public:
  CheckOpResult() = default;
  CheckOpResult(std::string value1, std::string value2)
      : value1_(std::move(value1)), value2_(std::move(value2)), failed_(true) {}

  explicit operator bool() const { return failed_; }

  const std::string& value1() const { return value1_; }
  const std::string& value2() const { return value2_; }

 private:
  std::string value1_;
  std::string value2_;
  bool failed_ = false;
};

template <class T1, class T2, class Compare>
CheckOpResult check_op(const T1& value1, const T2& value2, Compare compare) {
  if (compare(value1, value2)) {
    return CheckOpResult();
  }
  return CheckOpResult(stringify(value1), stringify(value2));
}

template <class Compare>
CheckOpResult check_str(const char* value1,
                        const char* value2,
                        Compare compare) {
  if (compare(value1, value2)) {
    return CheckOpResult();
  }
  return CheckOpResult(stringify(value1), stringify(value2));
}

class CheckOpMessage {
 public:
  CheckOpMessage(const char* file,
                 int line,
                 const char* function,
                 const char* expression,
                 const CheckOpResult& result)
      : file_(file),
        line_(line),
        function_(function),
        expression_(expression),
        result_(result) {}

  ~CheckOpMessage() {
    std::string message = "Check failed: ";
    message += expression_;
    message += " (";
    message += result_.value1();
    message += " vs ";
    message += result_.value2();
    message += ")";
    std::string streamed = stream_.str();
    if (!streamed.empty()) {
      message += " ";
      message += streamed;
    }
    write_log(file_, line_, function_, LogSeverity::kFatal, message);
  }

  std::ostream& stream() { return stream_; }

 private:
  const char* file_;
  int line_;
  const char* function_;
  const char* expression_;
  const CheckOpResult& result_;
  std::ostringstream stream_;
};

class HandlerStream {
 public:
  explicit HandlerStream(std::int64_t counter) : stream_(dummy_, 2, counter) {
    stream_.rdbuf(&buffer_);
  }

  HandlerStream(const HandlerStream&) = delete;
  HandlerStream& operator=(const HandlerStream&) = delete;
  HandlerStream(HandlerStream&&) = delete;
  HandlerStream& operator=(HandlerStream&&) = delete;

  std::ostream& stream() { return stream_; }
  std::string str() const { return buffer_.str(); }

 private:
  std::stringbuf buffer_;
  char dummy_[2] = {};
  google::LogMessage::LogStream stream_;
};

class LogMessage {
 public:
  LogMessage(const char* file,
             int line,
             const char* function,
             LogSeverity severity,
             std::int64_t counter)
      : file_(file), line_(line), function_(function), severity_(severity) {
    if (!has_log_handler()) {
      if (counter > 0) {
        glog_message_.emplace(file ? file : "", line,
                              static_cast<google::LogSeverity>(severity),
                              counter, &google::LogMessage::SendToLog);
      } else {
        glog_message_.emplace(file ? file : "", line,
                              static_cast<google::LogSeverity>(severity));
      }
    } else {
      stream_.emplace(counter);
    }
  }

  ~LogMessage() {
    if (!glog_message_) {
      write_log(file_, line_, function_, severity_,
                stream_ ? stream_->str() : std::string());
    }
  }

  std::ostream& stream() {
    if (glog_message_) {
      return glog_message_->stream();
    }
    return stream_->stream();
  }

 private:
  const char* file_;
  int line_;
  const char* function_;
  LogSeverity severity_;
  std::optional<google::LogMessage> glog_message_;
  std::optional<HandlerStream> stream_;
};

class Voidify {
 public:
  void operator&(std::ostream&) {}
};

}  // namespace logging_internal

}  // namespace rime

#define RIME_LOG_NOOP                          \
  true ? (void)0                               \
       : ::rime::logging_internal::Voidify() & \
             ::rime::logging_internal::null_stream()

// Severity tokens are pasted into these level macros so that Windows macros
// such as ERROR from wingdi.h are not expanded in LOG(ERROR).
#define RIME_LOG_LEVEL_INFO RIME_LOG_INFO
#define RIME_LOG_LEVEL_WARNING RIME_LOG_WARNING
#define RIME_LOG_LEVEL_ERROR RIME_LOG_ERROR
#define RIME_LOG_LEVEL_FATAL RIME_LOG_FATAL

// Evaluate the stream arguments only when the condition and level check pass.
#define RIME_LOG_STREAM_IMPL(level, condition, counter)             \
  (!(condition) || !::rime::logging_internal::log_is_enabled(       \
                       static_cast<::rime::LogSeverity>(level)))    \
      ? (void)0                                                     \
      : ::rime::logging_internal::Voidify() &                       \
            ::rime::logging_internal::LogMessage(                   \
                __FILE__, __LINE__, __func__,                       \
                static_cast<::rime::LogSeverity>(level), (counter)) \
                .stream()

#define RIME_LOG_STREAM(level, condition) \
  RIME_LOG_STREAM_IMPL(level, condition, 0)

#define RIME_LOG_EVERY_N_STREAM(level, condition) \
  RIME_LOG_STREAM_IMPL(level, condition,          \
                       ::rime::logging_internal::every_n_counter())

#define RIME_LOG_EVERY_N_CONDITION(n)                                   \
  ([](int rime_every_n) {                                               \
    static ::std::atomic<unsigned long long> rime_log_counter{0};       \
    const auto rime_log_occurrence =                                    \
        rime_log_counter.fetch_add(1, ::std::memory_order_relaxed) + 1; \
    ::rime::logging_internal::every_n_counter() =                       \
        static_cast<::std::int64_t>(rime_log_occurrence);               \
    return rime_every_n > 0 &&                                          \
           (rime_log_occurrence - 1) %                                  \
                   static_cast<unsigned long long>(rime_every_n) ==     \
               0;                                                       \
  }(n))

#define RIME_LOG_IF_EVERY_N_CONDITION(condition, n)                         \
  ([&](int rime_every_n) {                                                  \
    static ::std::atomic<unsigned long long> rime_log_counter{0};           \
    static ::std::atomic<unsigned long long> rime_log_mod_counter{0};       \
    const auto rime_log_occurrence =                                        \
        rime_log_counter.fetch_add(1, ::std::memory_order_relaxed) + 1;     \
    ::rime::logging_internal::every_n_counter() =                           \
        static_cast<::std::int64_t>(rime_log_occurrence);                   \
    if (!(condition)) {                                                     \
      return false;                                                         \
    }                                                                       \
    ::rime::logging_internal::every_n_counter() =                           \
        static_cast<::std::int64_t>(rime_log_occurrence);                   \
    if (rime_every_n <= 0) {                                                \
      return false;                                                         \
    }                                                                       \
    const auto rime_log_mod_occurrence =                                    \
        rime_log_mod_counter.fetch_add(1, ::std::memory_order_relaxed) + 1; \
    return (rime_log_mod_occurrence - 1) %                                  \
               static_cast<unsigned long long>(rime_every_n) ==             \
           0;                                                               \
  }(n))
#define RIME_LOG_VLOG_ENABLED(verboselevel) VLOG_IS_ON(verboselevel)

#define LOG(severity) RIME_LOG_STREAM(RIME_LOG_LEVEL_##severity, true)
#define LOG_IF(severity, condition) \
  RIME_LOG_STREAM(RIME_LOG_LEVEL_##severity, (condition))
#define LOG_EVERY_N(severity, n)                     \
  RIME_LOG_EVERY_N_STREAM(RIME_LOG_LEVEL_##severity, \
                          RIME_LOG_EVERY_N_CONDITION(n))
#define LOG_IF_EVERY_N(severity, condition, n)       \
  RIME_LOG_EVERY_N_STREAM(RIME_LOG_LEVEL_##severity, \
                          RIME_LOG_IF_EVERY_N_CONDITION(condition, n))
#define LOG_ASSERT(condition)                         \
  RIME_LOG_STREAM(RIME_LOG_LEVEL_FATAL, !(condition)) \
      << "Assert failed: " #condition
#define VLOG(verboselevel) \
  RIME_LOG_STREAM(RIME_LOG_LEVEL_INFO, RIME_LOG_VLOG_ENABLED(verboselevel))

#define CHECK(condition)                                          \
  (condition) ? (void)0                                           \
              : ::rime::logging_internal::Voidify() &             \
                    ::rime::logging_internal::CheckMessage(       \
                        __FILE__, __LINE__, __func__, #condition) \
                        .stream()

#define RIME_CHECK_OP(op, val1, val2)                                         \
  while (::rime::logging_internal::CheckOpResult rime_check_result =          \
             ::rime::logging_internal::check_op(                              \
                 (val1), (val2),                                              \
                 [](const auto& rime_check_lhs, const auto& rime_check_rhs) { \
                   return rime_check_lhs op rime_check_rhs;                   \
                 }))                                                          \
  ::rime::logging_internal::CheckOpMessage(__FILE__, __LINE__, __func__,      \
                                           #val1 " " #op " " #val2,           \
                                           rime_check_result)                 \
      .stream()

#define CHECK_EQ(val1, val2) RIME_CHECK_OP(==, val1, val2)
#define CHECK_NE(val1, val2) RIME_CHECK_OP(!=, val1, val2)
#define CHECK_LE(val1, val2) RIME_CHECK_OP(<=, val1, val2)
#define CHECK_LT(val1, val2) RIME_CHECK_OP(<, val1, val2)
#define CHECK_GE(val1, val2) RIME_CHECK_OP(>=, val1, val2)
#define CHECK_GT(val1, val2) RIME_CHECK_OP(>, val1, val2)

#define CHECK_NOTNULL(val)                                                    \
  ::rime::logging_internal::check_notnull(__FILE__, __LINE__, __func__, #val, \
                                          (val))

#define RIME_CHECK_STR(predicate, val1, val2)                                 \
  while (::rime::logging_internal::CheckOpResult rime_check_result =          \
             ::rime::logging_internal::check_str(                             \
                 (val1), (val2),                                              \
                 [](const char* rime_check_lhs, const char* rime_check_rhs) { \
                   return predicate(rime_check_lhs, rime_check_rhs);          \
                 }))                                                          \
  ::rime::logging_internal::CheckOpMessage(                                   \
      __FILE__, __LINE__, __func__, #val1 " / " #val2, rime_check_result)     \
      .stream()

#define CHECK_STREQ(val1, val2) \
  RIME_CHECK_STR(::rime::logging_internal::string_equal, val1, val2)
#define CHECK_STRNE(val1, val2) \
  RIME_CHECK_STR(::rime::logging_internal::string_not_equal, val1, val2)
#define CHECK_STRCASEEQ(val1, val2) \
  RIME_CHECK_STR(::rime::logging_internal::string_case_equal, val1, val2)
#define CHECK_STRCASENE(val1, val2) \
  RIME_CHECK_STR(::rime::logging_internal::string_case_not_equal, val1, val2)

#ifndef NDEBUG
#define DLOG(severity) RIME_LOG_STREAM(RIME_LOG_LEVEL_##severity, true)
#define DVLOG(verboselevel) VLOG(verboselevel)
#define DLOG_IF(severity, condition) \
  RIME_LOG_STREAM(RIME_LOG_LEVEL_##severity, (condition))
#define DLOG_EVERY_N(severity, n) LOG_EVERY_N(severity, n)
#define DLOG_IF_EVERY_N(severity, condition, n) \
  LOG_IF_EVERY_N(severity, condition, n)
#define DLOG_ASSERT(condition) LOG_ASSERT(condition)
#define DCHECK(condition) CHECK(condition)
#define DCHECK_EQ(val1, val2) CHECK_EQ(val1, val2)
#define DCHECK_NE(val1, val2) CHECK_NE(val1, val2)
#define DCHECK_LE(val1, val2) CHECK_LE(val1, val2)
#define DCHECK_LT(val1, val2) CHECK_LT(val1, val2)
#define DCHECK_GE(val1, val2) CHECK_GE(val1, val2)
#define DCHECK_GT(val1, val2) CHECK_GT(val1, val2)
#define DCHECK_NOTNULL(val) CHECK_NOTNULL(val)
#define DCHECK_STREQ(val1, val2) CHECK_STREQ(val1, val2)
#define DCHECK_STRNE(val1, val2) CHECK_STRNE(val1, val2)
#define DCHECK_STRCASEEQ(val1, val2) CHECK_STRCASEEQ(val1, val2)
#define DCHECK_STRCASENE(val1, val2) CHECK_STRCASENE(val1, val2)
#else
#define DLOG(severity) RIME_LOG_NOOP
#define DVLOG(verboselevel) RIME_LOG_NOOP
#define DLOG_IF(severity, condition) RIME_LOG_NOOP
#define DLOG_EVERY_N(severity, n) RIME_LOG_NOOP
#define DLOG_IF_EVERY_N(severity, condition, n) RIME_LOG_NOOP
#define DLOG_ASSERT(condition) RIME_LOG_NOOP
#define DCHECK(condition) RIME_LOG_NOOP
#define DCHECK_EQ(val1, val2) RIME_LOG_NOOP
#define DCHECK_NE(val1, val2) RIME_LOG_NOOP
#define DCHECK_LE(val1, val2) RIME_LOG_NOOP
#define DCHECK_LT(val1, val2) RIME_LOG_NOOP
#define DCHECK_GE(val1, val2) RIME_LOG_NOOP
#define DCHECK_GT(val1, val2) RIME_LOG_NOOP
#define DCHECK_NOTNULL(val) (val)
#define DCHECK_STREQ(val1, val2) RIME_LOG_NOOP
#define DCHECK_STRNE(val1, val2) RIME_LOG_NOOP
#define DCHECK_STRCASEEQ(val1, val2) RIME_LOG_NOOP
#define DCHECK_STRCASENE(val1, val2) RIME_LOG_NOOP
#endif  // NDEBUG

#else  // RIME_ENABLE_LOGGING

#include "no_logging.h"

#endif  // RIME_ENABLE_LOGGING

#endif  // RIME_LOGGING_H_
