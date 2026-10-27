//
// Copyright RIME Developers
// Distributed under the BSD License
//
#include <rime/build_config.h>

#include <gtest/gtest.h>

// Windows headers (wingdi.h) define ERROR as 0. Define it here as well so
// LOG(ERROR) is exercised with ERROR defined as a macro on every platform.
#ifndef ERROR
#define ERROR 0
#endif

#ifdef RIME_ENABLE_LOGGING
#ifndef GLOG_NO_ABBREVIATED_SEVERITIES
#define GLOG_NO_ABBREVIATED_SEVERITIES
#endif
#endif

#include <rime/common.h>

TEST(LoggingErrorMacroTest, AcceptsWindowsErrorMacro) {
  LOG(ERROR) << "error";
  LOG_IF(ERROR, false) << "conditional error";
  DLOG(ERROR) << "debug error";
  DLOG_IF(ERROR, false) << "conditional debug error";
  SUCCEED();
}
