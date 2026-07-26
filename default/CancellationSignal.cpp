/*
 * Copyright (C) 2026 The Project MiLahaina
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "ExtraFaceHal"
#include <log/log.h>
#include "CancellationSignal.h"
#include "Session.h"
#include <android-base/logging.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

CancellationSignal::CancellationSignal(const std::shared_ptr<Session> &session) : mSession(session) {}

ScopedAStatus CancellationSignal::cancel() {
  LOG(INFO) << "cancel operation";
  if (auto session = mSession.lock()) {
    session->cancel();
  } else {
    LOG(WARNING) << "Session already destroyed, ignoring cancel";
  }
  return ScopedAStatus::ok();
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
