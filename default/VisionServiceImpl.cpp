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
#include "VisionServiceImpl.h"
#include "FaceEngine.h"
#include "FaceStorageCallbacks.h"
#include "VendorStateManager.h"
#include <android-base/logging.h>
#include <errno.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cutils/ashmem.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

VisionServiceImpl::VisionServiceImpl() : mEngine(FaceEngine::getInstance()) {
  mEngine.init(getFaceEngineCallbacks());

  mEngine.setVisionFrameCallback([this](const std::vector<uint8_t> &frame,
                                        int width, int height, int angle) {
    return this->onCameraFrame(frame, width, height, angle);
  });
}

VisionServiceImpl::~VisionServiceImpl() {
  mEngine.setVisionFrameCallback(nullptr);
}

ScopedAStatus VisionServiceImpl::onFrame(const ::ndk::ScopedFileDescriptor &fd,
                                         int32_t width, int32_t height,
                                         int32_t angle) {
  // App-to-HAL direction: not used in this architecture, but must be implemented
  (void)fd; (void)width; (void)height; (void)angle;
  return ScopedAStatus::ok();
}

ScopedAStatus VisionServiceImpl::setCallback(const std::shared_ptr<::aidl::vendor::extra::biometrics::face::IVisionService> &callback) {
  std::lock_guard<std::mutex> lock(mCallbackMutex);
  mCallback = callback;
  LOG(INFO) << "VisionService callback registered (using IVisionService interface)";
  return ScopedAStatus::ok();
}

int VisionServiceImpl::onCameraFrame(const std::vector<uint8_t> &frame, int width, int height, int angle) {
  std::shared_ptr<IVisionService> cb;
  {
      std::lock_guard<std::mutex> lock(mCallbackMutex);
      cb = mCallback;
  }

  if (cb == nullptr) {
      return -1;
  }

  // Full-rate 640x480 NV21 is too large to send as Binder byte[] continuously.
  // Send only an fd through Binder; the app reads the frame bytes from ashmem.
  const size_t size = frame.size();
  int fd = ashmem_create_region("ExtraFaceFrame", size);
  if (fd < 0) {
      LOG(ERROR) << "Failed to create ashmem: " << strerror(errno);
      return -1;
  }

  void* addr = mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (addr == MAP_FAILED) {
      LOG(ERROR) << "Failed to mmap ashmem: " << strerror(errno);
      close(fd);
      return -1;
  }

  memcpy(addr, frame.data(), size);
  munmap(addr, size);

  ndk::ScopedFileDescriptor sfd(fd);
  auto status = cb->onFrame(sfd, width, height, angle);
  
  if (!status.isOk()) {
      LOG(WARNING) << "Failed to send frame to App: " << status.getMessage();
      // Reset callback if it becomes stale/disconnected
      std::lock_guard<std::mutex> lock(mCallbackMutex);
      mCallback = nullptr;
      return -1;
  }

  return 0;
}

ScopedAStatus VisionServiceImpl::getVendorCode(int32_t *_aidl_return) {
  *_aidl_return = VendorStateManager::getInstance().consumeVendorCode();
  return ScopedAStatus::ok();
}

ScopedAStatus
VisionServiceImpl::getLastLandmarks(std::vector<float> *_aidl_return) {
  *_aidl_return = mEngine.getLastLandmarks();
  return ScopedAStatus::ok();
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
