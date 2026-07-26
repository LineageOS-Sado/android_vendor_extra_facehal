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

#include "Face.h"
#include "FaceTemplateSerializer.h"
#include "VisionServiceImpl.h"
#include "CameraClient.h"
#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <android/binder_process.h>
#include <log/log.h>

using aidl::android::hardware::biometrics::face::IFace;
using org::milahaina::face::hal::CameraClient;
using org::milahaina::face::hal::Face;
using org::milahaina::face::hal::FaceTemplateSerializer;
using org::milahaina::face::hal::VisionServiceImpl;

int main(int argc, char** argv) {
  android::base::InitLogging(argv, android::base::LogdLogger(android::base::MAIN));
  ALOGI("ExtraFaceHal starting...");

  if (FaceTemplateSerializer::CURRENT_VERSION == 0) {
    ALOGW("FaceTemplateSerializer::CURRENT_VERSION is not set by the engine library! Using fallback version 1.");
    FaceTemplateSerializer::CURRENT_VERSION = 1;
  }

  // Camera2-NDK delivers session/device callbacks on binder threads; pool must
  // be non-zero so they are not starved behind synchronous HAL calls.
  ABinderProcess_setThreadPoolMaxThreadCount(4);
  ABinderProcess_startThreadPool();
  ALOGI("Binder thread pool started");

  std::shared_ptr<Face> face = ndk::SharedRefBase::make<Face>();
  if (!face) {
    ALOGE("Failed to create Face HAL instance");
    return EXIT_FAILURE;
  }

  const std::string faceInstance =
      std::string() + IFace::descriptor + "/default";
  binder_status_t status =
      AServiceManager_addService(face->asBinder().get(), faceInstance.c_str());
  if (status != STATUS_OK) {
    ALOGE("Failed to register Face HAL: %s (status: %d)", faceInstance.c_str(),
          status);
    return EXIT_FAILURE;
  }
  ALOGI("Face HAL registered successfully: %s", faceInstance.c_str());

  // Register the vendor IVisionService (for ExtraVision enrollment UI)
  // This is a VINTF-stable service provider.
  std::shared_ptr<VisionServiceImpl> vision =
      ndk::SharedRefBase::make<VisionServiceImpl>();
  if (!vision) {
    ALOGE("Failed to create VisionService instance");
    return EXIT_FAILURE;
  }

  const std::string visionInstance =
      "vendor.extra.biometrics.face.IVisionService/default";
  status = AServiceManager_addService(vision->asBinder().get(),
                                      visionInstance.c_str());
  if (status != STATUS_OK) {
    ALOGE("Failed to register VisionService: %s (status: %d)",
          visionInstance.c_str(), status);
    return EXIT_FAILURE;
  }
  ALOGI("VisionService registered successfully: %s", visionInstance.c_str());

  CameraClient::warmUpAtHalStart();

  ALOGI("ExtraFaceHal is ready and serving");
  ABinderProcess_joinThreadPool();

  // Should never reach here
  ALOGE("ExtraFaceHal binder thread pool exited unexpectedly");
  return EXIT_FAILURE;
}
