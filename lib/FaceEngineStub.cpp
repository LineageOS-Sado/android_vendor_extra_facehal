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

#define LOG_TAG "ExtraFaceHalStub"

#include "FaceEngine.h"
#include "FaceTemplateSerializer.h"

#include <android-base/logging.h>
#include <log/log.h>
#include <vector>
#include <mutex>
#include <map>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

// Stub implementation of template version
uint32_t FaceTemplateSerializer::CURRENT_VERSION = 1;

struct FaceEngine::Impl {
    FaceEngineCallbacks mCallbacks;
    std::map<int32_t, std::vector<float>> enrolledEmbeddings;
    std::mutex enrollmentMutex;
};

FaceEngine &FaceEngine::getInstance() {
    static FaceEngine instance;
    return instance;
}

FaceEngine::FaceEngine() : mImpl(std::make_unique<Impl>()) {}

FaceEngine::~FaceEngine() = default;

void FaceEngine::getSensorProps(aidl::android::hardware::biometrics::face::SensorProps &props) {
    using aidl::android::hardware::biometrics::common::SensorStrength;
    using aidl::android::hardware::biometrics::face::FaceSensorType;
    using aidl::android::hardware::biometrics::common::ComponentInfo;

    props.commonProps.sensorId = 1008;
    props.commonProps.sensorStrength = SensorStrength::STRONG;
    props.commonProps.maxEnrollmentsPerUser = 3;

    ComponentInfo info;
    info.componentId = "extra_face_engine_stub";
    info.hardwareVersion = "Stub-Hardware";
    info.firmwareVersion = "Stub-Firmware-v1.0.0";
    info.serialNumber = "Extra-Stub-Serial";
    info.softwareVersion = "Extra-Stub-Engine-v1.0.0";

    props.commonProps.componentInfo = {info};
    props.sensorType = FaceSensorType::RGB;
    props.supportsDetectInteraction = false;
    props.halControlsPreview = false;
    props.enrollPreviewScale = 1.0f;
}

bool FaceEngine::init(const FaceEngineCallbacks &callbacks) {
    mImpl->mCallbacks = callbacks;
    LOG(INFO) << "Stub FaceEngine initialized";
    return true;
}

void FaceEngine::release() {
    LOG(INFO) << "Stub FaceEngine released";
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.clear();
}

int FaceEngine::authenticate(const std::vector<uint8_t> &/*nv21Frame*/, int /*width*/, int /*height*/,
                             int userId, float &outScore, int32_t &outFaceId) {
    if (isCancelled()) {
        LOG(INFO) << "Stub authenticate: operation cancelled, skipping frame processing";
        return -1;
    }
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    if (mImpl->enrolledEmbeddings.empty()) {
        LOG(INFO) << "Stub auth failed: No enrolled faces";
        outScore = 0.0f;
        outFaceId = -1;
        return 1; // Return no match / fail
    }

    // Stub mock match: matches the first enrolled face with high similarity score
    auto it = mImpl->enrolledEmbeddings.begin();
    outFaceId = it->first;
    outScore = 0.95f; // high similarity above threshold
    LOG(INFO) << "Stub auth success: Matched face " << outFaceId << " with score " << outScore << " for user " << userId;
    return 0; // Return success
}

int FaceEngine::enroll(int userId, const std::vector<uint8_t> &/*nv21Frame*/, int /*width*/,
                       int /*height*/, int32_t &outFaceId) {
    if (isCancelled()) {
        LOG(INFO) << "Stub authenticate: operation cancelled, skipping frame processing";
        return -1;
    }
    mEnrollFrameCount++;
    int progress = (mEnrollFrameCount * 100) / ENROLL_REQUIRED_GOOD_FRAMES;
    LOG(INFO) << "Stub enroll progress: " << progress << "%";

    if (mEnrollFrameCount < ENROLL_REQUIRED_GOOD_FRAMES) {
        return VendorCode::KEEP;
    }

    // Enrollment completed
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);

    // Generate a face ID
    int32_t maxId = 0;
    for (const auto &[faceId, _] : mImpl->enrolledEmbeddings) {
        if (faceId > maxId) {
            maxId = faceId;
        }
    }
    outFaceId = maxId + 1;

    // Create a mock template embedding (size 512, all zeros)
    std::vector<float> mockEmbedding(512, 0.0f);

    // Call the callback to save it to storage
    if (mImpl->mCallbacks.saveTemplate) {
        if (!mImpl->mCallbacks.saveTemplate(userId, outFaceId, mockEmbedding)) {
            LOG(ERROR) << "Stub failed to save enrollment template via callback";
            mEnrollFrameCount = 0;
            return VendorCode::FAILED;
        }
    }

    mImpl->enrolledEmbeddings[outFaceId] = std::move(mockEmbedding);
    mEnrollFrameCount = 0;
    LOG(INFO) << "Stub enrolled face ID: " << outFaceId;
    return VendorCode::FACE_OK;
}

int FaceEngine::analyzeFaceQuality(const std::vector<uint8_t> &/*nv21*/, int /*width*/,
                                   int /*height*/) {
    if (isCancelled()) {
        LOG(INFO) << "Stub authenticate: operation cancelled, skipping frame processing";
        return -1;
    }
    // Stub always reports good face quality
    return VendorCode::FACE_OK;
}

int FaceEngine::deleteEnrollment(int userId, int faceId) {
    if (mImpl->mCallbacks.deleteTemplate) {
        mImpl->mCallbacks.deleteTemplate(userId, faceId);
    }
    {
        std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
        mImpl->enrolledEmbeddings.erase(faceId);
    }
    LOG(INFO) << "Stub deleted enrollment face " << faceId << " for user " << userId;
    return 0;
}

int FaceEngine::getEnrollmentCount(int userId) {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    return static_cast<int>(mImpl->enrolledEmbeddings.size());
}

std::vector<int32_t> FaceEngine::getEnrolledFaceIds(int userId) {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    std::vector<int32_t> ids;
    for (const auto &[faceId, _] : mImpl->enrolledEmbeddings) {
        ids.push_back(faceId);
    }
    return ids;
}

bool FaceEngine::restoreEnrollments(int userId) {
    LOG(INFO) << "Stub restoring enrollments for user " << userId;
    if (!mImpl->mCallbacks.loadTemplates) {
        LOG(WARNING) << "Stub restoreEnrollments: loadTemplates callback is null";
        return false;
    }
    auto loaded = mImpl->mCallbacks.loadTemplates(userId);

    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.clear();
    for (auto &pair : loaded) {
        mImpl->enrolledEmbeddings[pair.first] = std::move(pair.second);
        LOG(INFO) << "Stub restored face: " << pair.first;
    }
    return true;
}

std::vector<float> FaceEngine::getLastLandmarks() {
    // Return empty landmarks
    return {};
}

void FaceEngine::resetEnrollment() {
    mEnrollFrameCount = 0;
}

void FaceEngine::cancelAll() {
    LOG(INFO) << "Stub FaceEngine::cancelAll: cancelling and resetting all operations";
    mCancelled = true;
    resetEnrollment();
    mCaptureRequested = false;
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
