//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <rime/build_config.h>

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <rime/common.h>
#include <rime/setup.h>

#ifdef RIME_ENABLE_LOGGING

namespace {

struct CapturedRecord {
  int level = 0;
  std::string message;
  std::string file;
  int line = 0;
  std::string function;
};

struct Capture {
  std::mutex mutex;
  std::vector<CapturedRecord> records;
};

struct BlockingState {
  std::mutex mutex;
  std::condition_variable cv;
  bool entered = false;
  bool release = false;
};

void CaptureHandler(void* context, const RimeLogRecord* record) {
  auto* capture = static_cast<Capture*>(context);
  std::lock_guard<std::mutex> lock(capture->mutex);
  capture->records.push_back({record->level,
                              record->message ? record->message : "",
                              record->file ? record->file : "", record->line,
                              record->function ? record->function : ""});
}

void BlockingHandler(void* context, const RimeLogRecord*) {
  auto* state = static_cast<BlockingState*>(context);
  std::unique_lock<std::mutex> lock(state->mutex);
  state->entered = true;
  state->cv.notify_all();
  state->cv.wait(lock, [state] { return state->release; });
}

class LoggingFacadeTest : public ::testing::Test {
 protected:
  void SetUp() override { rime::SetLogHandler(nullptr, nullptr); }
  void TearDown() override { rime::SetLogHandler(nullptr, nullptr); }
};

TEST_F(LoggingFacadeTest, CapturesRecordMetadata) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  LOG(INFO) << "hello " << 42;

  ASSERT_EQ(capture.records.size(), 1);
  const auto& record = capture.records[0];
  EXPECT_EQ(record.level, RIME_LOG_INFO);
  EXPECT_EQ(record.message, "hello 42");
  EXPECT_NE(record.file.find("logging_facade_test.cc"), std::string::npos);
  EXPECT_GT(record.line, 0);
  EXPECT_FALSE(record.function.empty());
}

TEST_F(LoggingFacadeTest, CapturesAllLevels) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  LOG(INFO) << "info";
  LOG(WARNING) << "warning";
  LOG(ERROR) << "error";

  ASSERT_EQ(capture.records.size(), 3);
  EXPECT_EQ(capture.records[0].level, RIME_LOG_INFO);
  EXPECT_EQ(capture.records[1].level, RIME_LOG_WARNING);
  EXPECT_EQ(capture.records[2].level, RIME_LOG_ERROR);
}

TEST_F(LoggingFacadeTest, HonorsLogCondition) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  LOG_IF(ERROR, false) << "not logged";
  LOG_IF(ERROR, true) << "logged";

  ASSERT_EQ(capture.records.size(), 1);
  EXPECT_EQ(capture.records[0].level, RIME_LOG_ERROR);
  EXPECT_EQ(capture.records[0].message, "logged");
}

TEST_F(LoggingFacadeTest, HonorsEveryN) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  for (int i = 0; i < 10; ++i) {
    LOG_EVERY_N(INFO, 5) << "logged";
  }

  EXPECT_EQ(capture.records.size(), 2);
}

TEST_F(LoggingFacadeTest, SupportsGlogCounterWithHandler) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  for (int i = 0; i < 4; ++i) {
    LOG_EVERY_N(INFO, 2) << "counter " << google::COUNTER;
  }
  rime::SetLogHandler(nullptr, nullptr);

  ASSERT_EQ(capture.records.size(), 2);
  EXPECT_EQ(capture.records[0].message, "counter 1");
  EXPECT_EQ(capture.records[1].message, "counter 3");
}

TEST_F(LoggingFacadeTest, SupportsGlogCounterWithConditionalEveryN) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  for (int i = 0; i < 6; ++i) {
    LOG_IF_EVERY_N(INFO, i % 2 == 0, 2) << "counter " << google::COUNTER;
  }
  rime::SetLogHandler(nullptr, nullptr);

  ASSERT_EQ(capture.records.size(), 2);
  EXPECT_EQ(capture.records[0].message, "counter 1");
  EXPECT_EQ(capture.records[1].message, "counter 5");
}

TEST_F(LoggingFacadeTest, ClearsHandler) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  LOG(INFO) << "logged";
  rime::SetLogHandler(nullptr, nullptr);
  LOG(INFO) << "not captured";

  ASSERT_EQ(capture.records.size(), 1);
  EXPECT_EQ(capture.records[0].message, "logged");
}

TEST_F(LoggingFacadeTest, InstallsHandlerThroughCApi) {
  RimeApi* api = rime_get_api();
  ASSERT_NE(api, nullptr);
  ASSERT_TRUE(RIME_API_AVAILABLE(api, set_log_handler));

  Capture capture;
  api->set_log_handler(&CaptureHandler, &capture);
  LOG(INFO) << "via c api";
  api->set_log_handler(nullptr, nullptr);

  ASSERT_EQ(capture.records.size(), 1);
  EXPECT_EQ(capture.records[0].message, "via c api");
}

TEST_F(LoggingFacadeTest, SupportsConcurrentLogging) {
  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);

  constexpr int kThreads = 4;
  constexpr int kRecordsPerThread = 100;
  std::vector<std::thread> threads;
  for (int i = 0; i < kThreads; ++i) {
    threads.emplace_back([kRecordsPerThread] {
      for (int j = 0; j < kRecordsPerThread; ++j) {
        LOG(INFO) << "threaded";
      }
    });
  }
  for (auto& thread : threads) {
    thread.join();
  }

  EXPECT_EQ(capture.records.size(), kThreads * kRecordsPerThread);
}

TEST_F(LoggingFacadeTest, WaitsForInFlightCallbacksWhenClearing) {
  BlockingState state;
  rime::SetLogHandler(&BlockingHandler, &state);

  std::thread logging_thread([] { LOG(INFO) << "blocking"; });
  bool entered = false;
  {
    std::unique_lock<std::mutex> lock(state.mutex);
    entered = state.cv.wait_for(lock, std::chrono::seconds(5),
                                [&state] { return state.entered; });
  }
  EXPECT_TRUE(entered);

  std::atomic<bool> cleared{false};
  std::thread clearing_thread([&state, &cleared] {
    rime::SetLogHandler(nullptr, nullptr);
    cleared.store(true, std::memory_order_release);
  });

  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  EXPECT_FALSE(cleared.load(std::memory_order_acquire));

  {
    std::lock_guard<std::mutex> lock(state.mutex);
    state.release = true;
  }
  state.cv.notify_all();

  logging_thread.join();
  clearing_thread.join();
  EXPECT_TRUE(cleared.load(std::memory_order_acquire));
}

TEST_F(LoggingFacadeTest, AllowsHandlerToClearItself) {
  struct State {
    std::atomic<bool> cleared{false};
  };
  auto handler = [](void* context, const RimeLogRecord*) {
    rime::SetLogHandler(nullptr, nullptr);
    static_cast<State*>(context)->cleared.store(true,
                                                std::memory_order_release);
  };

  State state;
  rime::SetLogHandler(handler, &state);
  LOG(INFO) << "clear from handler";

  EXPECT_TRUE(state.cleared.load(std::memory_order_acquire));
  LOG(INFO) << "not captured";
}

TEST_F(LoggingFacadeTest, DoesNotPropagateHandlerExceptions) {
  auto handler = [](void*, const RimeLogRecord*) {
    throw std::runtime_error("handler failure");
  };
  rime::SetLogHandler(handler, nullptr);

  EXPECT_NO_THROW({ LOG(INFO) << "throwing handler"; });

  rime::SetLogHandler(nullptr, nullptr);
}

TEST_F(LoggingFacadeTest, HonorsMinLogLevelWithHandler) {
  rime::SetupLogging("logging_facade_test", RIME_LOG_ERROR, nullptr);

  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);
  LOG(INFO) << "info";
  LOG(WARNING) << "warning";
  LOG(ERROR) << "error";
  rime::SetLogHandler(nullptr, nullptr);

  rime::SetupLogging("logging_facade_test", RIME_LOG_INFO, nullptr);

  ASSERT_EQ(capture.records.size(), 1);
  EXPECT_EQ(capture.records[0].level, RIME_LOG_ERROR);
}

TEST_F(LoggingFacadeTest, HonorsVerboseLevelWithHandler) {
  const int saved = FLAGS_v;
  FLAGS_v = 1;

  Capture capture;
  rime::SetLogHandler(&CaptureHandler, &capture);
  VLOG(1) << "verbose";
  VLOG(2) << "more verbose";
  rime::SetLogHandler(nullptr, nullptr);

  FLAGS_v = saved;

  ASSERT_EQ(capture.records.size(), 1);
  EXPECT_EQ(capture.records[0].message, "verbose");
}

TEST_F(LoggingFacadeTest, CheckMacrosCompileAndPass) {
  int value = 7;
  CHECK(true);
  CHECK_EQ(value, 7);
  CHECK_NE(value, 8);
  CHECK_LE(value, 7);
  CHECK_LT(value, 8);
  CHECK_GE(value, 7);
  CHECK_GT(value, 6);
  CHECK_STREQ("rime", "rime");
  CHECK_STRNE("rime", "librime");
  CHECK_STRCASEEQ("Rime", "rime");
  CHECK_STRCASENE("Rime", "librime");
  CHECK_STREQ(nullptr, nullptr);
  CHECK_STRNE(nullptr, "rime");
  EXPECT_EQ(CHECK_NOTNULL(&value), &value);
}

TEST_F(LoggingFacadeTest, CheckNotNullSupportsMoveOnlyValues) {
  auto value = std::make_unique<int>(7);
  EXPECT_EQ(*CHECK_NOTNULL(value), 7);

  auto moved = CHECK_NOTNULL(std::move(value));
  EXPECT_NE(moved, nullptr);
  EXPECT_EQ(*moved, 7);
}

TEST_F(LoggingFacadeTest, CheckMacrosSupportStreaming) {
  int value = 7;
  CHECK(true) << "not logged";
  CHECK_EQ(value, 7) << "not logged";
  CHECK_STREQ("rime", "rime") << "not logged";
  SUCCEED();
}

#if GTEST_HAS_DEATH_TEST
TEST_F(LoggingFacadeTest, CheckFailureAborts) {
  EXPECT_DEATH({ CHECK(false); }, "Check failed");
}

TEST_F(LoggingFacadeTest, CheckOpFailureIncludesStreamedMessage) {
  EXPECT_DEATH({ CHECK_EQ(1, 2) << ": extra"; }, "1 vs 2.*extra");
}

TEST_F(LoggingFacadeTest, CheckStrFailureIncludesStreamedMessage) {
  EXPECT_DEATH(
      { CHECK_STREQ("left", "right") << ": extra"; }, "left.*right.*extra");
}
#endif

}  // namespace

#else  // RIME_ENABLE_LOGGING

TEST(LoggingFacadeTest, SetLogHandlerIsNoOpWithoutLogging) {
  RimeApi* api = rime_get_api();
  ASSERT_NE(api, nullptr);
  ASSERT_TRUE(RIME_API_AVAILABLE(api, set_log_handler));

  int calls = 0;
  auto handler = [](void* context, const RimeLogRecord*) {
    ++*static_cast<int*>(context);
  };
  api->set_log_handler(handler, &calls);
  LOG(INFO) << "discarded";
  api->set_log_handler(nullptr, nullptr);

  EXPECT_EQ(calls, 0);
}

#endif  // RIME_ENABLE_LOGGING
