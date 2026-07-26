#define LOG_TAG "ExtraFaceHal"

#include "Camera2NdkBackend.h"
#include "FaceEngine.h"

#include <log/log.h>

#include <cstring>
#include <chrono>
#include <thread>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

namespace {

std::mutex gCameraWarmMutex;
std::string gWarmedCameraId;
bool gCameraWarmValid = false;

bool WaitForNonEmptyCameraList(ACameraManager* mgr, int timeoutMs) {
    constexpr int kStepMs = 100;
    for (int elapsed = 0; elapsed < timeoutMs; elapsed += kStepMs) {
        ACameraIdList* list = nullptr;
        camera_status_t cs = ACameraManager_getCameraIdList(mgr, &list);
        const int num = list ? list->numCameras : -1;
        if (cs == ACAMERA_OK && list != nullptr && num > 0) {
            if (elapsed > 0) {
                ALOGI("Camera2NdkBackend: camera id list ready after %d ms (count=%d)",
                      elapsed, num);
            }
            ACameraManager_deleteCameraIdList(list);
            return true;
        }
        if (cs != ACAMERA_OK) {
            ALOGE("Camera2NdkBackend: getCameraIdList status=%d num=%d", cs, num);
            if (list) ACameraManager_deleteCameraIdList(list);
            return false;
        }
        if (list) ACameraManager_deleteCameraIdList(list);
        if (elapsed == 0) {
            ALOGW("Camera2NdkBackend: camera list empty — waiting up to %d ms for provider",
                  timeoutMs);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kStepMs));
    }
    ALOGE("Camera2NdkBackend: camera list still empty after %d ms", timeoutMs);
    return false;
}

bool SelectFrontCameraId(ACameraManager* mgr, std::string& outId) {
    ACameraIdList* list = nullptr;
    camera_status_t cs = ACameraManager_getCameraIdList(mgr, &list);
    if (cs != ACAMERA_OK || list == nullptr) {
        ALOGE("Camera2NdkBackend: getCameraIdList status=%d (list=%s)",
              cs, list ? "ok" : "null");
        if (list) ACameraManager_deleteCameraIdList(list);
        return false;
    }
    if (list->numCameras <= 0) {
        ALOGE("Camera2NdkBackend: getCameraIdList returned empty list (status=OK)");
        ACameraManager_deleteCameraIdList(list);
        return false;
    }

    const uint8_t targetFacing = ACAMERA_LENS_FACING_FRONT;
    std::string firstId = list->cameraIds[0];
    std::string match;

    for (int i = 0; i < list->numCameras; ++i) {
        const char* id = list->cameraIds[i];
        ACameraMetadata* chars = nullptr;
        if (ACameraManager_getCameraCharacteristics(mgr, id, &chars) != ACAMERA_OK ||
            chars == nullptr) {
            continue;
        }
        ACameraMetadata_const_entry entry{};
        if (ACameraMetadata_getConstEntry(chars, ACAMERA_LENS_FACING, &entry) ==
                ACAMERA_OK &&
            entry.count > 0 && entry.data.u8[0] == targetFacing) {
            match = id;
            ACameraMetadata_free(chars);
            break;
        }
        ACameraMetadata_free(chars);
    }

    ACameraManager_deleteCameraIdList(list);
    outId = match.empty() ? firstId : match;
    if (match.empty()) {
        ALOGW("Camera2NdkBackend: no camera with facing=%u, falling back to %s",
              targetFacing, outId.c_str());
    }
    return true;
}

int GetPreviewRotationDegrees(ACameraManager* mgr, const std::string& cameraId) {
    ACameraMetadata* chars = nullptr;
    if (ACameraManager_getCameraCharacteristics(mgr, cameraId.c_str(), &chars) != ACAMERA_OK ||
        chars == nullptr) {
        ALOGW("Camera2NdkBackend: getCameraCharacteristics failed for %s; using rotation=0",
              cameraId.c_str());
        return 0;
    }

    int32_t sensorOrientation = 0;
    uint8_t facing = ACAMERA_LENS_FACING_FRONT;

    ACameraMetadata_const_entry entry{};
    if (ACameraMetadata_getConstEntry(chars, ACAMERA_SENSOR_ORIENTATION, &entry) ==
            ACAMERA_OK && entry.count > 0) {
        sensorOrientation = entry.data.i32[0];
    }
    if (ACameraMetadata_getConstEntry(chars, ACAMERA_LENS_FACING, &entry) ==
            ACAMERA_OK && entry.count > 0) {
        facing = entry.data.u8[0];
    }
    ACameraMetadata_free(chars);

    // Display rotation is portrait/0 for the enrollment UI. The app rotates a
    // decoded bitmap, so use the sensor-to-display rotation directly.
    const int degrees = 0;
    int rotation = (sensorOrientation - degrees + 360) % 360;
    ALOGI("Camera2NdkBackend: camera id=%s sensorOrientation=%d facing=%u previewRotation=%d",
          cameraId.c_str(), sensorOrientation, facing, rotation);
    return rotation;
}

bool GetOptimalSupportedResolution(ACameraManager* mgr, const std::string& cameraId, int& outWidth, int& outHeight) {
    ACameraMetadata* chars = nullptr;
    camera_status_t cs = ACameraManager_getCameraCharacteristics(mgr, cameraId.c_str(), &chars);
    if (cs != ACAMERA_OK || chars == nullptr) {
        ALOGE("Camera2NdkBackend: failed to get characteristics for camera %s", cameraId.c_str());
        return false;
    }

    ACameraMetadata_const_entry entry{};
    cs = ACameraMetadata_getConstEntry(chars, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: failed to get stream configurations");
        ACameraMetadata_free(chars);
        return false;
    }

    int absoluteMaxW = 0;
    int absoluteMaxH = 0;
    long long maxArea = 0;

    int bestW = 0;
    int bestH = 0;
    long long bestDiff = -1;

    // Each configuration entry has 4 int32 elements: format, width, height, isInput
    for (uint32_t i = 0; i < entry.count; i += 4) {
        int32_t format = entry.data.i32[i + 0];
        int32_t width  = entry.data.i32[i + 1];
        int32_t height = entry.data.i32[i + 2];
        int32_t isInput = entry.data.i32[i + 3];

        if (format == AIMAGE_FORMAT_YUV_420_888 && isInput == 0) {
            long long area = static_cast<long long>(width) * height;
            if (area > maxArea) {
                maxArea = area;
                absoluteMaxW = width;
                absoluteMaxH = height;
            }

            long long diff = std::abs(static_cast<long long>(width) - 1280) +
                             std::abs(static_cast<long long>(height) - 720);
            if (bestDiff == -1 || diff < bestDiff) {
                bestDiff = diff;
                bestW = width;
                bestH = height;
            }
        }
    }

    ACameraMetadata_free(chars);

    // If the device max supported resolution is less than expected (1280x720),
    // use the maximum supported resolution of the camera.
    if (absoluteMaxW > 0 && (absoluteMaxW < 1280 || absoluteMaxH < 720)) {
        outWidth = absoluteMaxW;
        outHeight = absoluteMaxH;
        ALOGI("Camera2NdkBackend: max resolution %dx%d is less than expected 1280x720. Using max supported resolution.",
              outWidth, outHeight);
        return true;
    }

    if (bestW > 0 && bestH > 0) {
        outWidth = bestW;
        outHeight = bestH;
        return true;
    }
    return false;
}

bool GetMaxSupportedResolution(ACameraManager *mgr,
                               const std::string &cameraId, int &outWidth,
                               int &outHeight) {
  ACameraMetadata *chars = nullptr;
  camera_status_t cs =
      ACameraManager_getCameraCharacteristics(mgr, cameraId.c_str(), &chars);
  if (cs != ACAMERA_OK || chars == nullptr) {
    ALOGE("Camera2NdkBackend: failed to get characteristics for camera %s",
          cameraId.c_str());
    return false;
  }

  ACameraMetadata_const_entry entry{};
  cs = ACameraMetadata_getConstEntry(
      chars, ACAMERA_SCALER_AVAILABLE_STREAM_CONFIGURATIONS, &entry);
  if (cs != ACAMERA_OK) {
    ALOGE("Camera2NdkBackend: failed to get stream configurations");
    ACameraMetadata_free(chars);
    return false;
  }

  int absoluteMaxW = 0;
  int absoluteMaxH = 0;
  long long maxArea = 0;

  for (uint32_t i = 0; i < entry.count; i += 4) {
    int32_t format = entry.data.i32[i + 0];
    int32_t width = entry.data.i32[i + 1];
    int32_t height = entry.data.i32[i + 2];
    int32_t isInput = entry.data.i32[i + 3];

    if (format == AIMAGE_FORMAT_YUV_420_888 && isInput == 0) {
      long long area = static_cast<long long>(width) * height;
      if (area > maxArea) {
        maxArea = area;
        absoluteMaxW = width;
        absoluteMaxH = height;
      }
    }
  }

  ACameraMetadata_free(chars);

  if (absoluteMaxW > 0 && absoluteMaxH > 0) {
    outWidth = absoluteMaxW;
    outHeight = absoluteMaxH;
    return true;
  }
  return false;
}

}  // namespace

namespace {

// Pack an AImage (YUV_420_888) into NV21 (Y plane + interleaved V,U pairs),
// honoring the source row/pixel strides. Works for both semi-planar (NV12/NV21
// in memory) and fully planar layouts.
bool packAImageToNv21(AImage* img, std::vector<uint8_t>& out, int* outW, int* outH) {
    int32_t w = 0, h = 0, format = 0, planeCount = 0;
    AImage_getWidth(img, &w);
    AImage_getHeight(img, &h);
    AImage_getFormat(img, &format);
    AImage_getNumberOfPlanes(img, &planeCount);
    if (w <= 0 || h <= 0 || planeCount < 2) return false;

    const size_t ySize  = static_cast<size_t>(w) * h;
    const size_t uvSize = ySize / 2;
    out.resize(ySize + uvSize);

    // Y plane.
    uint8_t* ySrc = nullptr;
    int yLen = 0;
    int32_t yRow = 0, yPx = 1;
    AImage_getPlaneRowStride(img, 0, &yRow);
    AImage_getPlanePixelStride(img, 0, &yPx);
    AImage_getPlaneData(img, 0, &ySrc, &yLen);
    if (ySrc == nullptr) return false;
    if (yPx == 1 && yRow == w) {
        std::memcpy(out.data(), ySrc, ySize);
    } else {
        for (int row = 0; row < h; ++row) {
            std::memcpy(out.data() + static_cast<size_t>(row) * w,
                        ySrc + static_cast<size_t>(row) * yRow,
                        static_cast<size_t>(w));
        }
    }

    uint8_t* uvDst = out.data() + ySize;

    // U plane (1) and V plane (2).
    uint8_t* uSrc = nullptr; int uLen = 0; int32_t uRow = 0, uPx = 1;
    uint8_t* vSrc = nullptr; int vLen = 0; int32_t vRow = 0, vPx = 1;
    AImage_getPlaneRowStride(img, 1, &uRow);
    AImage_getPlanePixelStride(img, 1, &uPx);
    AImage_getPlaneData(img, 1, &uSrc, &uLen);
    AImage_getPlaneRowStride(img, 2, &vRow);
    AImage_getPlanePixelStride(img, 2, &vPx);
    AImage_getPlaneData(img, 2, &vSrc, &vLen);
    if (uSrc == nullptr || vSrc == nullptr) return false;

    const int chromaH = h / 2;
    const int chromaW = w / 2;
    for (int row = 0; row < chromaH; ++row) {
        uint8_t* dst = uvDst + static_cast<size_t>(row) * w;
        const uint8_t* uLine = uSrc + static_cast<size_t>(row) * uRow;
        const uint8_t* vLine = vSrc + static_cast<size_t>(row) * vRow;
        for (int x = 0; x < chromaW; ++x) {
            dst[2 * x]     = vLine[x * vPx]; // V
            dst[2 * x + 1] = uLine[x * uPx]; // U
        }
    }

    if (outW) *outW = w;
    if (outH) *outH = h;
    return true;
}

}  // namespace

void Camera2NdkBackend::warmUpAtHalStart() {
    ACameraManager* mgr = ACameraManager_create();
    if (!mgr) {
        ALOGW("Camera2NdkBackend: warm-up ACameraManager_create failed");
        return;
    }
    constexpr int kWarmTimeoutMs = 20000;
    if (!WaitForNonEmptyCameraList(mgr, kWarmTimeoutMs)) {
        ACameraManager_delete(mgr);
        return;
    }
    std::string id;
    if (!SelectFrontCameraId(mgr, id) || id.empty()) {
        ACameraManager_delete(mgr);
        return;
    }
    ACameraManager_delete(mgr);
    {
        std::lock_guard<std::mutex> lk(gCameraWarmMutex);
        gWarmedCameraId = id;
        gCameraWarmValid = true;
    }
    ALOGI("Camera2NdkBackend: HAL warm-up cached camera id=%s", gWarmedCameraId.c_str());
}

Camera2NdkBackend::Camera2NdkBackend() {}

Camera2NdkBackend::~Camera2NdkBackend() {
    stop();
}

void Camera2NdkBackend::noteFailure(int vendorCode) {
    mLastStartFailure = vendorCode;
}

int Camera2NdkBackend::lastStartFailureVendorCode() const {
    return mLastStartFailure;
}

bool Camera2NdkBackend::getOptimalResolution(int &width, int &height) const {
  ACameraManager *mgr = ACameraManager_create();
  if (!mgr) return false;
  std::string id;
  if (!SelectFrontCameraId(mgr, id) || id.empty()) {
    ACameraManager_delete(mgr);
    return false;
  }
  bool ret = GetOptimalSupportedResolution(mgr, id, width, height);
  ACameraManager_delete(mgr);
  return ret;
}

bool Camera2NdkBackend::start(FrameCallback cb) {
    std::unique_lock<std::mutex> lock(mLock);
    if (mIsStopping) {
        mCallbackCv.wait(lock, [this]() { return !mIsStopping; });
    }
    if (mIsRunning) return true;

    mCallback = cb;
    mLastStartFailure = 0;

    mMgr = ACameraManager_create();
    if (!mMgr) {
        ALOGE("Camera2NdkBackend: ACameraManager_create failed");
        noteFailure(VendorCode::CAMERA_MANAGER_FAILED);
        teardown(lock);
        return false;
    }

    bool usedWarmId = false;
    {
        std::lock_guard<std::mutex> lk(gCameraWarmMutex);
        if (gCameraWarmValid && !gWarmedCameraId.empty()) {
            mCameraId = gWarmedCameraId;
            usedWarmId = true;
        }
    }

    if (!usedWarmId) {
        if (!waitForNonEmptyCameraList(/*timeoutMs=*/10000)) {
            ALOGE("Camera2NdkBackend: no cameras reported after wait (see SELinux "
                  "hal_client_domain / binder_call for hal_camera vs hal_camera_default)");
            noteFailure(VendorCode::CAMERA_NO_DEVICE);
            teardown(lock);
            return false;
        }
        if (!selectFrontCamera(mCameraId) || mCameraId.empty()) {
            ALOGE("Camera2NdkBackend: no camera matching desired facing");
            noteFailure(VendorCode::CAMERA_ID_SELECT_FAILED);
            teardown(lock);
            return false;
        }
    } else {
        ALOGI("Camera2NdkBackend: using warm-up camera id=%s", mCameraId.c_str());
    }
    mFrameAngle = GetPreviewRotationDegrees(mMgr, mCameraId);

    int maxW = 640;
    int maxH = 480;
    if (GetMaxSupportedResolution(mMgr, mCameraId, maxW, maxH)) {
        mWidth = maxW;
        mHeight = maxH;
        ALOGI("Camera2NdkBackend: dynamically selected max supported resolution %dx%d for camera %s",
              mWidth, mHeight, mCameraId.c_str());
    } else {
        mWidth = 640;
        mHeight = 480;
        ALOGW("Camera2NdkBackend: failed to determine max supported resolution, using fallback 640x480");
    }

    mDevCb = {};
    mDevCb.context = this;
    mDevCb.onDisconnected = &Camera2NdkBackend::onDeviceDisconnected;
    mDevCb.onError        = &Camera2NdkBackend::onDeviceError;

    for (int openAttempt = 0;; ++openAttempt) {
        media_status_t mst = AImageReader_new(mWidth, mHeight,
                                               AIMAGE_FORMAT_YUV_420_888,
                                               /*maxImages=*/4, &mRdr);
        if (mst != AMEDIA_OK || mRdr == nullptr) {
            ALOGE("Camera2NdkBackend: AImageReader_new failed (%d)", mst);
            noteFailure(VendorCode::CAMERA_IMAGE_READER_FAILED);
            teardown(lock);
            return false;
        }

        mImgCb = { this, &Camera2NdkBackend::onImageAvailable };
        AImageReader_setImageListener(mRdr, &mImgCb);

        mst = AImageReader_getWindow(mRdr, &mWin);
        if (mst != AMEDIA_OK || mWin == nullptr) {
            ALOGE("Camera2NdkBackend: AImageReader_getWindow failed (%d)", mst);
            noteFailure(VendorCode::CAMERA_WINDOW_FAILED);
            AImageReader_setImageListener(mRdr, nullptr);
            AImageReader_delete(mRdr);
            mRdr = nullptr;
            mWin = nullptr;
            teardown(lock);
            return false;
        }

        camera_status_t cs = ACameraManager_openCamera(mMgr, mCameraId.c_str(),
                                                       &mDevCb, &mDev);
        if (cs == ACAMERA_OK && mDev != nullptr) {
            break;
        }
        ALOGE("Camera2NdkBackend: ACameraManager_openCamera failed (%d)", cs);
        AImageReader_setImageListener(mRdr, nullptr);
        AImageReader_delete(mRdr);
        mRdr = nullptr;
        mWin = nullptr;

        if (openAttempt == 0 && usedWarmId) {
            ALOGW("Camera2NdkBackend: warm-up id failed to open; clearing cache and re-probing");
            {
                std::lock_guard<std::mutex> lk(gCameraWarmMutex);
                gCameraWarmValid = false;
                gWarmedCameraId.clear();
            }
            usedWarmId = false;
            if (!waitForNonEmptyCameraList(/*timeoutMs=*/8000)) {
                noteFailure(VendorCode::CAMERA_NO_DEVICE);
                teardown(lock);
                return false;
            }
            if (!selectFrontCamera(mCameraId) || mCameraId.empty()) {
                noteFailure(VendorCode::CAMERA_ID_SELECT_FAILED);
                teardown(lock);
                return false;
            }
            mFrameAngle = GetPreviewRotationDegrees(mMgr, mCameraId);
            continue;
        }
        noteFailure(VendorCode::CAMERA_OPEN_FAILED);
        teardown(lock);
        return false;
    }

    ALOGI("Camera2NdkBackend: opened camera id=%s", mCameraId.c_str());

    camera_status_t cs = ACaptureSessionOutput_create(mWin, &mOut);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: ACaptureSessionOutput_create failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_SESSION_FAILED);
        teardown(lock);
        return false;
    }

    cs = ACaptureSessionOutputContainer_create(&mOutC);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: ACaptureSessionOutputContainer_create failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_SESSION_FAILED);
        teardown(lock);
        return false;
    }
    cs = ACaptureSessionOutputContainer_add(mOutC, mOut);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: ACaptureSessionOutputContainer_add failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_SESSION_FAILED);
        teardown(lock);
        return false;
    }

    mSessCb = {};
    mSessCb.context  = this;
    mSessCb.onActive = &Camera2NdkBackend::onSessionActive;
    mSessCb.onReady  = &Camera2NdkBackend::onSessionReady;
    mSessCb.onClosed = &Camera2NdkBackend::onSessionClosed;

    mSessionClosed = false;
    cs = ACameraDevice_createCaptureSession(mDev, mOutC, &mSessCb, &mSess);
    if (cs != ACAMERA_OK || mSess == nullptr) {
        ALOGE("Camera2NdkBackend: createCaptureSession failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_SESSION_FAILED);
        teardown(lock);
        return false;
    }

    cs = ACameraDevice_createCaptureRequest(mDev, TEMPLATE_PREVIEW, &mReq);
    if (cs != ACAMERA_OK || mReq == nullptr) {
        ALOGE("Camera2NdkBackend: createCaptureRequest failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_REQUEST_FAILED);
        teardown(lock);
        return false;
    }

    cs = ACameraOutputTarget_create(mWin, &mTgt);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: ACameraOutputTarget_create failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_REQUEST_FAILED);
        teardown(lock);
        return false;
    }
    cs = ACaptureRequest_addTarget(mReq, mTgt);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: ACaptureRequest_addTarget failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_REQUEST_FAILED);
        teardown(lock);
        return false;
    }

    cs = ACameraCaptureSession_setRepeatingRequest(mSess, /*cb=*/nullptr,
                                                   /*numRequests=*/1, &mReq,
                                                   /*captureSequenceId=*/nullptr);
    if (cs != ACAMERA_OK) {
        ALOGE("Camera2NdkBackend: setRepeatingRequest failed (%d)", cs);
        noteFailure(VendorCode::CAMERA_STREAMING_FAILED);
        teardown(lock);
        return false;
    }

    mIsRunning = true;
    ALOGI("Camera2NdkBackend: started (%dx%d YUV_420_888)", mWidth, mHeight);
    return true;
}

void Camera2NdkBackend::stop() {
    std::unique_lock<std::mutex> lock(mLock);
    if (!mIsRunning && !mIsStopping && mMgr == nullptr) return;

    if (mIsStopping) {
        mCallbackCv.wait(lock, [this]() { return !mIsStopping; });
        return;
    }

    mIsRunning = false;
    mIsStopping = true;

    if (mInCallback && std::this_thread::get_id() == mCallbackThreadId) {
        ALOGI("Camera2NdkBackend: stop() called from callback thread. Deferring teardown.");
        std::thread([this]() {
            std::unique_lock<std::mutex> lock(mLock);
            mCallbackCv.wait(lock, [this]() { return !mInCallback; });
            teardown(lock);
            mIsStopping = false;
            mCallbackCv.notify_all();
        }).detach();
        return;
    }

    mCallbackCv.wait(lock, [this]() { return !mInCallback; });
    teardown(lock);
    mIsStopping = false;
    mCallbackCv.notify_all();
}

void Camera2NdkBackend::teardown(std::unique_lock<std::mutex>& lock) {
    ACameraCaptureSession* sess = mSess;
    ACameraOutputTarget* tgt = mTgt;
    ACaptureRequest* req = mReq;
    ACaptureSessionOutputContainer* outC = mOutC;
    ACaptureSessionOutput* out = mOut;
    ACameraDevice* dev = mDev;
    AImageReader* rdr = mRdr;
    ACameraManager* mgr = mMgr;

    mSess = nullptr;
    mTgt = nullptr;
    mReq = nullptr;
    mOutC = nullptr;
    mOut = nullptr;
    mDev = nullptr;
    mRdr = nullptr;
    mWin = nullptr;
    mMgr = nullptr;

    // Unlock to perform blocking/synchronous close calls outside the lock
    lock.unlock();

    if (sess) {
        ACameraCaptureSession_stopRepeating(sess);
        ACameraCaptureSession_close(sess);

        // Re-acquire lock to wait for onSessionClosed callback to prevent race/hang
        lock.lock();
        mCallbackCv.wait_for(lock, std::chrono::seconds(1), [this]() { return mSessionClosed; });
        lock.unlock();
    }
    if (tgt) {
        ACameraOutputTarget_free(tgt);
    }
    if (req) {
        ACaptureRequest_free(req);
    }
    if (outC) {
        ACaptureSessionOutputContainer_free(outC);
    }
    if (out) {
        ACaptureSessionOutput_free(out);
    }
    if (dev) {
        ACameraDevice_close(dev);
    }
    if (rdr) {
        AImageReader_setImageListener(rdr, nullptr);
        AImageReader_delete(rdr);
    }
    if (mgr) {
        ACameraManager_delete(mgr);
    }

    // Re-acquire lock to restore expected locking state for caller
    lock.lock();
}

bool Camera2NdkBackend::waitForNonEmptyCameraList(int timeoutMs) {
    return WaitForNonEmptyCameraList(mMgr, timeoutMs);
}

bool Camera2NdkBackend::selectFrontCamera(std::string& outId) {
    return SelectFrontCameraId(mMgr, outId);
}

void Camera2NdkBackend::onImageAvailable(void* ctx, AImageReader* reader) {
    auto* self = static_cast<Camera2NdkBackend*>(ctx);
    if (!self) return;

    {
        std::lock_guard<std::mutex> lock(self->mLock);
        if (!self->mIsRunning) {
            return;
        }
    }

    struct CallbackGuard {
        Camera2NdkBackend* self;
        CallbackGuard(Camera2NdkBackend* s) : self(s) {
            std::lock_guard<std::mutex> lock(self->mLock);
            self->mInCallback = true;
            self->mCallbackThreadId = std::this_thread::get_id();
        }
        ~CallbackGuard() {
            std::lock_guard<std::mutex> lock(self->mLock);
            self->mInCallback = false;
            self->mCallbackThreadId = std::thread::id();
            self->mCallbackCv.notify_all();
        }
    } guard(self);

    AImage* img = nullptr;
    media_status_t mst = AImageReader_acquireLatestImage(reader, &img);
    if (mst != AMEDIA_OK || img == nullptr) {
        // EAGAIN/insufficient is normal under load; just drop.
        return;
    }
    std::vector<uint8_t> frame;
    int w = 0, h = 0;
    bool ok = packAImageToNv21(img, frame, &w, &h);
    AImage_delete(img);
    if (!ok) {
        ALOGW("Camera2NdkBackend: failed to pack AImage to NV21");
        return;
    }

    FrameCallback cb;
    int angle = 0;
    {
        std::lock_guard<std::mutex> lock(self->mLock);
        if (!self->mIsRunning || !self->mCallback) {
            return;
        }
        cb = self->mCallback;
        angle = self->mFrameAngle;
    }

    static std::atomic<uint32_t> sFrameCount{0};
    if ((sFrameCount.fetch_add(1) % 30) == 0) {
        ALOGI("Camera2NdkBackend: delivered frame #%u (%dx%d, %zuB, angle=%d)",
              sFrameCount.load(), w, h, frame.size(), angle);
    }

    cb(frame, w, h, angle);
}

void Camera2NdkBackend::onDeviceDisconnected(void* ctx, ACameraDevice* dev) {
    ALOGW("Camera2NdkBackend: camera disconnected id=%s",
          dev ? ACameraDevice_getId(dev) : "?");
    auto* self = static_cast<Camera2NdkBackend*>(ctx);
    if (self) {
        bool shouldStop = false;
        {
            std::lock_guard<std::mutex> lock(self->mLock);
            if (self->mIsRunning && !self->mIsStopping) {
                shouldStop = true;
            }
        }
        if (shouldStop) {
            self->stop();
        }
    }
}

void Camera2NdkBackend::onDeviceError(void* ctx, ACameraDevice* dev, int err) {
    ALOGE("Camera2NdkBackend: camera error id=%s err=%d",
          dev ? ACameraDevice_getId(dev) : "?", err);
    auto* self = static_cast<Camera2NdkBackend*>(ctx);
    if (self) {
        bool shouldStop = false;
        {
            std::lock_guard<std::mutex> lock(self->mLock);
            if (self->mIsRunning && !self->mIsStopping) {
                shouldStop = true;
            }
        }
        if (shouldStop) {
            self->stop();
        }
    }
}

void Camera2NdkBackend::onSessionActive(void* /*ctx*/, ACameraCaptureSession* /*s*/) {
    ALOGI("Camera2NdkBackend: session ACTIVE");
}

void Camera2NdkBackend::onSessionReady(void* /*ctx*/, ACameraCaptureSession* /*s*/) {
    ALOGI("Camera2NdkBackend: session READY");
}

void Camera2NdkBackend::onSessionClosed(void* ctx, ACameraCaptureSession* /*s*/) {
    ALOGI("Camera2NdkBackend: session CLOSED");
    auto* self = static_cast<Camera2NdkBackend*>(ctx);
    if (self) {
        bool shouldStop = false;
        {
            std::lock_guard<std::mutex> lock(self->mLock);
            self->mSessionClosed = true;
            self->mCallbackCv.notify_all();
            if (self->mIsRunning && !self->mIsStopping) {
                shouldStop = true;
            }
        }
        if (shouldStop) {
            self->stop();
        }
    }
}

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
