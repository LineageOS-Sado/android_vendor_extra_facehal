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
#include "Session.h"
#include "CancellationSignal.h"
#include "FaceEngine.h"
#include "FaceStorageCallbacks.h"
#include "AcquiredInfoMapper.h"
#include <aidl/android/hardware/biometrics/face/AcquiredInfo.h>
#include <aidl/android/hardware/biometrics/face/AuthenticationFrame.h>
#include <aidl/android/hardware/biometrics/face/EnrollmentFrame.h>
#include <android-base/logging.h>
#include <aidl/android/hardware/biometrics/common/DisplayState.h>
#include <aidl/android/hardware/biometrics/common/OperationReason.h>
#include <unistd.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::biometrics::face::AcquiredInfo;
using aidl::android::hardware::biometrics::face::AuthenticationFrame;
using aidl::android::hardware::biometrics::face::EnrollmentFrame;
using aidl::android::hardware::biometrics::face::Error;
using aidl::android::hardware::biometrics::common::DisplayState;
using aidl::android::hardware::biometrics::common::OperationReason;

struct Session::SessionCallbackQueue {
    std::mutex lock;
    std::condition_variable cv;
    std::queue<std::function<void()>> queue;
    bool running = true;
};

Session::Session(int32_t userId, const std::shared_ptr<ISessionCallback> &cb)
    : mUserId(userId), mCb(cb), mEngine(FaceEngine::getInstance()),
      mCryptoClient(CryptoClient::getInstance()), mCameraClient(std::make_shared<CameraClient>()),
      mIsAuthenticating(false), mIsEnrolling(false), mIsDetectingInteraction(false), mEnrollRemaining(0), mCurrentChallenge(0),
      mCurrentOperationReason(OperationReason::UNKNOWN), mCurrentDisplayState(DisplayState::UNKNOWN) {
  if (!mEngine.init(getFaceEngineCallbacks())) {
    LOG(ERROR) << "Failed to initialize FaceEngine in Session";
  }
  mEngine.restoreEnrollments(mUserId);

  mEngine.setSessionFrameCallback([this](const std::vector<uint8_t> &frame,
                                         int width, int height,
                                         int angle) -> int {
    return this->onCameraFrame(frame, width, height, angle);
  });

  mCallbackQueueState = std::make_shared<SessionCallbackQueue>();
  mCallbackWorker = std::thread([state = mCallbackQueueState]() {
      while (true) {
          std::function<void()> task;
          {
              std::unique_lock<std::mutex> lock(state->lock);
              state->cv.wait(lock, [state]() { return !state->running || !state->queue.empty(); });
              if (!state->running && state->queue.empty()) {
                  break;
              }
              task = std::move(state->queue.front());
              state->queue.pop();
          }
          if (task) {
              task();
          }
      }
  });
}

Session::~Session() {
  mEngine.setSessionFrameCallback(nullptr);
  cancel();

  if (mCallbackQueueState) {
      std::lock_guard<std::mutex> lock(mCallbackQueueState->lock);
      mCallbackQueueState->running = false;
      mCallbackQueueState->cv.notify_all();
  }
  if (mCallbackWorker.joinable()) {
      mCallbackWorker.detach();
  }
}

void Session::cancel() {
  LOG(INFO) << "Session::cancel: cancelling all active operations";
  if (mIsAuthenticating || mIsEnrolling || mIsDetectingInteraction) {
    postCallback([cb = mCb]() {
        cb->onError(Error::CANCELED, 0);
    });
  }
  mIsAuthenticating = false;
  mIsEnrolling = false;
  mIsDetectingInteraction = false;
  mCurrentOperationReason = OperationReason::UNKNOWN;
  mEngine.cancelAll();
  mCameraClient->stop();
}

namespace {

void downscaleNv21(const std::vector<uint8_t> &src, int srcW, int srcH,
                   std::vector<uint8_t> &dst, int dstW, int dstH) {
  dst.resize(dstW * dstH * 3 / 2);
  const uint8_t *srcY = src.data();
  const uint8_t *srcUV = src.data() + srcW * srcH;
  uint8_t *dstY = dst.data();
  uint8_t *dstUV = dst.data() + dstW * dstH;

  float scaleX = (float)srcW / dstW;
  float scaleY = (float)srcH / dstH;

  // Downscale Y plane
  for (int y = 0; y < dstH; ++y) {
    int srcY_row = (int)(y * scaleY) * srcW;
    int dstY_row = y * dstW;
    for (int x = 0; x < dstW; ++x) {
      dstY[dstY_row + x] = srcY[srcY_row + (int)(x * scaleX)];
    }
  }

  // Downscale UV plane
  int dstChromaH = dstH / 2;
  int dstChromaW = dstW / 2;
  for (int y = 0; y < dstChromaH; ++y) {
    int srcUV_row = (int)(y * scaleY) * srcW;
    int dstUV_row = y * dstW;
    for (int x = 0; x < dstChromaW; ++x) {
      int srcX = (int)(x * scaleX);
      dstUV[dstUV_row + 2 * x] = srcUV[srcUV_row + 2 * srcX];
      dstUV[dstUV_row + 2 * x + 1] = srcUV[srcUV_row + 2 * srcX + 1];
    }
  }
}

} // namespace

int Session::onCameraFrame(const std::vector<uint8_t> &frame, int width,
                           int height, int angle) {
  if (mEngine.isCancelled()) {
    LOG(INFO)
        << "onCameraFrame: operation is cancelled, skipping frame processing";
    return -1;
  }

  // Check if we are already processing a frame. If so, drop this frame immediately.
  // This keeps the camera pipeline flowing at 30 FPS and prevents preview jitter.
  bool expected = false;
  if (!mProcessingFrame.compare_exchange_strong(expected, true)) {
    return -1;
  }

  const std::vector<uint8_t> *processingFrame = &frame;
  int processingWidth = width;
  int processingHeight = height;
  std::vector<uint8_t> downscaledFrame;

  int optW = 1280;
  int optH = 720;
  if (!mCameraClient || !mCameraClient->getOptimalResolution(optW, optH)) {
    optW = 1280;
    optH = 720;
  }
  int maxDim = std::max(optW, optH);
  int minDim = std::min(optW, optH);

  if (width > maxDim || height > maxDim) {
    int targetW = width;
    int targetH = height;
    double aspectRatio = (double)width / height;
    if (width > height) {
      targetW = maxDim;
      targetH = (int)(maxDim / aspectRatio);
      if (targetH > minDim) {
        targetH = minDim;
        targetW = (int)(minDim * aspectRatio);
      }
    } else {
      targetH = maxDim;
      targetW = (int)(maxDim * aspectRatio);
      if (targetW > minDim) {
        targetW = minDim;
        targetH = (int)(minDim / aspectRatio);
      }
    }
    processingWidth = targetW & ~1;
    processingHeight = targetH & ~1;

    downscaleNv21(frame, width, height, downscaledFrame, processingWidth, processingHeight);
    processingFrame = &downscaledFrame;
  }

  // Copy frame data before offloading to background thread
  std::vector<uint8_t> frameCopy = *processingFrame;

  // Grab self shared pointer to keep Session alive
  std::shared_ptr<Session> self = ref<Session>();

  std::thread([self, frameCopy = std::move(frameCopy), processingWidth, processingHeight, angle]() {
    if (self->mEngine.isCancelled()) {
      self->mProcessingFrame = false;
      return;
    }

    if (self->mIsAuthenticating) {
      float score = 0.0f;
      int32_t matchedFaceId = -1;
      int res = self->mEngine.authenticate(frameCopy, processingWidth, processingHeight, self->mUserId, score,
                                     matchedFaceId);
      LOG(INFO) << "onCameraFrame async: authenticate res=" << res << " score=" << score
                << " faceId=" << matchedFaceId;
      if (res == 0) {
        self->mIsAuthenticating = false;
        self->mCurrentOperationReason = OperationReason::UNKNOWN;
        self->mCameraClient->stop();
        HardwareAuthToken hat;
        hat.challenge = self->mCurrentChallenge;
        hat.userId = self->mUserId;
        hat.authenticatorId = 0;
        hat.timestamp = aidl::android::hardware::keymaster::Timestamp{
            .milliSeconds = 0L
        };
        self->postCallback([cb = self->mCb, matchedFaceId, hat]() {
          cb->onAuthenticationSucceeded(matchedFaceId, hat);
        });
      } else if (res > 0) {
        if (res == 1 || res == VendorCode::FAILED) {
          self->postCallback([cb = self->mCb]() { cb->onAuthenticationFailed(); });
        } else {
          int32_t vendorCode = 0;
          AcquiredInfo info = AcquiredInfoMapper::mapVendorCode(res, vendorCode);
          AuthenticationFrame authFrame;
          authFrame.data.acquiredInfo = info;
          authFrame.data.vendorCode = vendorCode;
          self->postCallback(
              [cb = self->mCb, authFrame]() { cb->onAuthenticationFrame(authFrame); });
        }
      }
    } else if (self->mIsDetectingInteraction) {
      int res = self->mEngine.analyzeFaceQuality(frameCopy, processingWidth, processingHeight);
      LOG(INFO) << "onCameraFrame async: detectInteraction res=" << res;
      if (res == VendorCode::FACE_OK) {
        self->mIsDetectingInteraction = false;
        self->mCameraClient->stop();
        self->postCallback([cb = self->mCb]() { cb->onInteractionDetected(); });
      }
    } else if (self->mIsEnrolling) {
      int32_t outFaceId = 0;
      int res = self->mEngine.enroll(self->mUserId, frameCopy, processingWidth, processingHeight, outFaceId);
      int progress = self->mEngine.getEnrollmentProgress();
      int totalFrames = FaceEngine::ENROLL_REQUIRED_GOOD_FRAMES;
      int remaining = totalFrames - (self->mEngine.mEnrollFrameCount);

      LOG(INFO) << "onCameraFrame async: enroll result=" << res
                << " progress=" << progress << "%";

      if (res == VendorCode::FACE_OK) {
        self->mIsEnrolling = false;
        self->mCameraClient->stop();
        self->postCallback(
            [cb = self->mCb, outFaceId]() { cb->onEnrollmentProgress(outFaceId, 0); });
      } else if (res == VendorCode::KEEP) {
        self->postCallback([cb = self->mCb, outFaceId, remaining]() {
          cb->onEnrollmentProgress(outFaceId, remaining);
        });
      } else {
        int32_t vendorCode = 0;
        AcquiredInfo info = AcquiredInfoMapper::mapVendorCode(res, vendorCode);
        EnrollmentFrame enrollFrame;
        enrollFrame.data.acquiredInfo = info;
        enrollFrame.data.vendorCode = vendorCode;
        self->postCallback(
            [cb = self->mCb, enrollFrame]() { cb->onEnrollmentFrame(enrollFrame); });
      }
    }

    self->mProcessingFrame = false;
  }).detach();

  return 0;
}

ScopedAStatus Session::generateChallenge(void) {
  mCurrentChallenge = mCryptoClient->generateChallenge();

  if (mCb != nullptr) {
    postCallback([cb = mCb, challenge = mCurrentChallenge]() {
        cb->onChallengeGenerated(challenge);
    });
  }

  return ScopedAStatus::ok();
}

ScopedAStatus Session::revokeChallenge(int64_t challenge) {
  if (mCb != nullptr) {
    postCallback([cb = mCb, challenge]() {
        cb->onChallengeRevoked(challenge);
    });
  }
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::getEnrollmentConfig(EnrollmentType enrollmentType,
                             std::vector<EnrollmentStageConfig> *_aidl_return) {
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::enroll(const HardwareAuthToken & /*hat*/, EnrollmentType type,
                const std::vector<Feature> &features,
                const std::optional<NativeHandle> &previewSurface,
                std::shared_ptr<ICancellationSignal> *_aidl_return) {
  LOG(INFO) << "Session::enroll user=" << mUserId
            << " type=" << static_cast<int>(type)
            << " features=" << features.size();
  mEngine.startOperation();
  mIsEnrolling = true;

  // Start direct Camera Provider client for native enrollment.
  // Must route through FaceEngine::onCameraFrame so IVisionService (App preview)
  // and Session enroll both receive every frame.
  bool started = mCameraClient->start([this](const std::vector<uint8_t>& frame, int width, int height, int angle) {
      mEngine.onCameraFrame(frame, width, height, angle);
  });
  LOG(INFO) << "Session::enroll CameraClient->start returned " << started;
  if (!started) {
    int32_t v = static_cast<int32_t>(mCameraClient->lastStartFailureVendorCode());
    if (v == 0) v = VendorCode::CAMERA_NO_DEVICE;
    mIsEnrolling = false;
    postCallback([cb = mCb, v]() {
        cb->onError(Error::VENDOR, v);
    });
    LOG(ERROR) << "Session::enroll camera failed, onError VENDOR vendorCode=" << v;
  }

  *_aidl_return = ndk::SharedRefBase::make<CancellationSignal>(ref<Session>());
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::enrollWithOptions(const FaceEnrollOptions &options,
                           std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return enroll(options.hardwareAuthToken, options.enrollmentType,
                options.features, std::nullopt, _aidl_return);
}

ScopedAStatus Session::authenticate(int64_t operationId,
                                    std::shared_ptr<ICancellationSignal>* _aidl_return) {
    LOG(INFO) << "Session::authenticate user=" << mUserId
              << " operationId=" << operationId;
    mCurrentChallenge = operationId;
    mEngine.startOperation();
    mIsAuthenticating = true;
    mAuthStartTime = std::chrono::steady_clock::now();

    // Infer OperationReason if not explicitly set via authenticateWithContext
    if (mCurrentOperationReason == OperationReason::UNKNOWN) {
      if (mCurrentDisplayState == DisplayState::LOCKSCREEN) {
        mCurrentOperationReason = OperationReason::KEYGUARD;
        LOG(INFO) << "Session::authenticate: Inferred OperationReason::KEYGUARD from DisplayState::LOCKSCREEN";
      } else {
        mCurrentOperationReason = OperationReason::BIOMETRIC_PROMPT;
        LOG(INFO) << "Session::authenticate: Inferred OperationReason::BIOMETRIC_PROMPT from DisplayState ("
                  << (int)mCurrentDisplayState << ")";
      }
    }

    bool started = mCameraClient->start([this](const std::vector<uint8_t>& frame, int width, int height, int angle) {
        mEngine.onCameraFrame(frame, width, height, angle);
    });
    LOG(INFO) << "Session::authenticate CameraClient->start returned " << started;
    if (!started) {
      int32_t v = static_cast<int32_t>(mCameraClient->lastStartFailureVendorCode());
      if (v == 0) v = VendorCode::CAMERA_NO_DEVICE;
      mIsAuthenticating = false;
      postCallback([cb = mCb, v]() {
          cb->onError(Error::VENDOR, v);
      });
      LOG(ERROR) << "Session::authenticate camera failed, onError VENDOR vendorCode=" << v;
    }

    *_aidl_return = ndk::SharedRefBase::make<CancellationSignal>(ref<Session>());
    return ScopedAStatus::ok();
}

ScopedAStatus
Session::detectInteraction(std::shared_ptr<ICancellationSignal> *_aidl_return) {
    LOG(INFO) << "Session::detectInteraction";
    mEngine.startOperation();
    mIsDetectingInteraction = true;

    bool started = mCameraClient->start([this](const std::vector<uint8_t>& frame, int width, int height, int angle) {
        mEngine.onCameraFrame(frame, width, height, angle);
    });
    LOG(INFO) << "Session::detectInteraction CameraClient->start returned " << started;
    if (!started) {
      int32_t v = static_cast<int32_t>(mCameraClient->lastStartFailureVendorCode());
      if (v == 0) v = VendorCode::CAMERA_NO_DEVICE;
      mIsDetectingInteraction = false;
      postCallback([cb = mCb, v]() {
          cb->onError(Error::VENDOR, v);
      });
      LOG(ERROR) << "Session::detectInteraction camera failed, onError VENDOR vendorCode=" << v;
    }

    *_aidl_return = ndk::SharedRefBase::make<CancellationSignal>(ref<Session>());
    return ScopedAStatus::ok();
}

ScopedAStatus Session::enumerateEnrollments(void) {
  std::vector<int32_t> enrollments = mEngine.getEnrolledFaceIds(mUserId);
  postCallback([cb = mCb, enrollments]() {
      cb->onEnrollmentsEnumerated(enrollments);
  });
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::removeEnrollments(const std::vector<int32_t> &enrollmentIds) {
  for (int32_t id : enrollmentIds) {
    mEngine.deleteEnrollment(mUserId, id);
  }
  postCallback([cb = mCb, enrollmentIds]() {
      cb->onEnrollmentsRemoved(enrollmentIds);
  });
  return ScopedAStatus::ok();
}

ScopedAStatus Session::getFeatures() {
  std::vector<Feature> features;
  if (mEngine.getRequireAttention()) {
      features.push_back(Feature::REQUIRE_ATTENTION);
  }
  if (mEngine.getRequireDiversePoses()) {
      features.push_back(Feature::REQUIRE_DIVERSE_POSES);
  }
  postCallback([cb = mCb, features]() {
      cb->onFeaturesRetrieved(features);
  });
  return ScopedAStatus::ok();
}

ScopedAStatus Session::setFeature(const HardwareAuthToken &hat, Feature feature,
                                  bool enabled) {
  if (feature == Feature::REQUIRE_ATTENTION) {
      mEngine.setRequireAttention(enabled);
      LOG(INFO) << "REQUIRE_ATTENTION set to " << (enabled ? "true" : "false");
  } else if (feature == Feature::REQUIRE_DIVERSE_POSES) {
      mEngine.setRequireDiversePoses(enabled);
      LOG(INFO) << "REQUIRE_DIVERSE_POSES set to " << (enabled ? "true" : "false");
  }
  if (mCb != nullptr) {
    postCallback([cb = mCb, feature]() { cb->onFeatureSet(feature); });
  }
  return ScopedAStatus::ok();
}

ScopedAStatus Session::getAuthenticatorId(void) {
  postCallback([cb = mCb]() {
      cb->onAuthenticatorIdRetrieved(0);
  });
  return ScopedAStatus::ok();
}

ScopedAStatus Session::invalidateAuthenticatorId(void) {
  return ScopedAStatus::ok();
}

ScopedAStatus Session::resetLockout(const HardwareAuthToken &hat) {
  postCallback([cb = mCb]() {
      cb->onLockoutCleared();
  });
  return ScopedAStatus::ok();
}

ScopedAStatus Session::close(void) {
  cancel();
  postCallback([cb = mCb]() {
      cb->onSessionClosed();
  });
  return ScopedAStatus::ok();
}

ScopedAStatus
Session::enrollWithContext(const HardwareAuthToken &hat, EnrollmentType type,
                           const std::vector<Feature> &features,
                           const std::optional<NativeHandle> &previewSurface,
                           const OperationContext &context,
                           std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return enroll(hat, type, features, previewSurface, _aidl_return);
}

ScopedAStatus Session::authenticateWithContext(
    int64_t operationId, const OperationContext &context,
    std::shared_ptr<ICancellationSignal> *_aidl_return) {
  mCurrentOperationReason = context.reason;
  return authenticate(operationId, _aidl_return);
}

ScopedAStatus Session::detectInteractionWithContext(
    const OperationContext &context,
    std::shared_ptr<ICancellationSignal> *_aidl_return) {
  return detectInteraction(_aidl_return);
}

ScopedAStatus Session::onContextChanged(const OperationContext &context) {

  if (context.reason != OperationReason::UNKNOWN) {
    mCurrentOperationReason = context.reason;
  }

  LOG(INFO) << "Session::onContextChanged: reason=" << (int)context.reason
            << ", displayState=" << (int)context.displayState;

  if (mIsAuthenticating &&
      mCurrentOperationReason == OperationReason::BIOMETRIC_PROMPT &&
      context.reason != OperationReason::BIOMETRIC_PROMPT) {
    auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - mAuthStartTime).count();
    if (elapsedMs < 1000) {
      LOG(INFO) << "Session::onContextChanged: Ignoring prompt cancellation check due to session startup grace period (" << elapsedMs << "ms)";
    } else {
      LOG(INFO) << "Session::onContextChanged: Biometric prompt is no longer "
                   "active, cancelling face auth.";
      cancel();
      return ScopedAStatus::ok();
    }
  }

  mCurrentDisplayState = context.displayState;
  if ((context.displayState == DisplayState::AOD || context.displayState == DisplayState::NO_UI) &&
      mCurrentOperationReason != OperationReason::BIOMETRIC_PROMPT) {
    LOG(INFO) << "Session::onContextChanged: Display is AOD or NO_UI (OFF), cancelling active operations.";
    cancel();
    return ScopedAStatus::ok();
  }

  // If display transitions to LOCKSCREEN, dynamically correct operation reason to KEYGUARD
  if (context.displayState == DisplayState::LOCKSCREEN && mIsAuthenticating) {
    if (mCurrentOperationReason != OperationReason::KEYGUARD) {
      mCurrentOperationReason = OperationReason::KEYGUARD;
      LOG(INFO) << "Session::onContextChanged: Corrected operation reason to KEYGUARD due to DisplayState::LOCKSCREEN";
    }
  }

  if (mIsAuthenticating &&
      (mCurrentOperationReason == OperationReason::KEYGUARD || mCurrentDisplayState == DisplayState::LOCKSCREEN) &&
      context.displayState != DisplayState::LOCKSCREEN) {
    LOG(INFO) << "Session::onContextChanged: Lockscreen no longer active, cancelling face auth.";
    cancel();
    return ScopedAStatus::ok();
  }

  return ScopedAStatus::ok();
}

void Session::postCallback(std::function<void()> task) {
  if (mCallbackQueueState) {
    std::lock_guard<std::mutex> lock(mCallbackQueueState->lock);
    if (mCallbackQueueState->running) {
      mCallbackQueueState->queue.push(std::move(task));
      mCallbackQueueState->cv.notify_all();
    }
  }
}

} // namespace hal
} // namespace face
} // namespace milahaina
} // namespace org
