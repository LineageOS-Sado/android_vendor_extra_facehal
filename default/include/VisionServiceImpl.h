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

#pragma once

#include <aidl/vendor/extra/biometrics/face/BnVisionService.h>
#include <atomic>
#include <mutex>
#include <sys/mman.h>

#include "FaceEngine.h"

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::vendor::extra::biometrics::face::BnVisionService;
using aidl::vendor::extra::biometrics::face::IVisionService;
using ndk::ScopedAStatus;

class VisionServiceImpl : public BnVisionService {
public:
  VisionServiceImpl();
  ~VisionServiceImpl();

  ScopedAStatus onFrame(const ::ndk::ScopedFileDescriptor &fd, int32_t width,
                        int32_t height, int32_t angle) override;
  ScopedAStatus
  setCallback(const std::shared_ptr<IVisionService> &callback) override;
  ScopedAStatus getVendorCode(int32_t *_aidl_return) override;
  ScopedAStatus getLastLandmarks(std::vector<float> *_aidl_return) override;

private:
  int onCameraFrame(const std::vector<uint8_t> &frame, int width, int height,
                    int angle);

  FaceEngine &mEngine;
  std::shared_ptr<IVisionService> mCallback;
  std::mutex mCallbackMutex;
};

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
