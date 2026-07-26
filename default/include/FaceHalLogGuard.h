#pragma once

// Default to logging enabled unless overridden by Soong config.
#ifndef EXTRA_FACEHAL_ENABLE_LOGGING
#define EXTRA_FACEHAL_ENABLE_LOGGING 1
#endif

#if !EXTRA_FACEHAL_ENABLE_LOGGING

// Include standard log headers first to set their header guards
// and prevent them from overriding our macro definitions later.
#include <log/log.h>
#include <android/log.h>
#include <android-base/logging.h>
#include <utils/Log.h>
#include <cutils/log.h>

namespace org::extra::face::hal::logging {
class NullLogMessage {
 public:
  template <typename T>
  NullLogMessage& operator<<(const T&) {
    return *this;
  }
};
}  // namespace org::extra::face::hal::logging

#ifdef LOG
#undef LOG
#endif
#define LOG(severity) ::org::extra::face::hal::logging::NullLogMessage()

#ifdef ALOGV
#undef ALOGV
#endif
#define ALOGV(...) ((void)0)

#ifdef ALOGD
#undef ALOGD
#endif
#define ALOGD(...) ((void)0)

#ifdef ALOGI
#undef ALOGI
#endif
#define ALOGI(...) ((void)0)

#ifdef ALOGW
#undef ALOGW
#endif
#define ALOGW(...) ((void)0)

#ifdef ALOGE
#undef ALOGE
#endif
#define ALOGE(...) ((void)0)

#ifdef ALOGF
#undef ALOGF
#endif
#define ALOGF(...) ((void)0)

// Direct liblog functions
#ifdef __android_log_print
#undef __android_log_print
#endif
#define __android_log_print(...) ((int)0)

#ifdef __android_log_write
#undef __android_log_write
#endif
#define __android_log_write(...) ((int)0)

#ifdef __android_log_buf_write
#undef __android_log_buf_write
#endif
#define __android_log_buf_write(...) ((int)0)

#ifdef LOG_TAG
#undef LOG_TAG
#endif

#endif
