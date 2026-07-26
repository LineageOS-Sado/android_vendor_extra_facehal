#define LOG_TAG "ExtraFaceHal"

#include "CameraClient.h"
#include "Camera2NdkBackend.h"

#include <log/log.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

CameraClient::CameraClient() {
    ALOGI("CameraClient instantiating Camera2NdkBackend (libcamera2ndk_vendor)");
    mBackend = std::make_shared<Camera2NdkBackend>();
}

CameraClient::~CameraClient() {
    stop();
}

void CameraClient::warmUpAtHalStart() {
    Camera2NdkBackend::warmUpAtHalStart();
}

int CameraClient::lastStartFailureVendorCode() const {
    return mBackend ? mBackend->lastStartFailureVendorCode() : 0;
}

bool CameraClient::getOptimalResolution(int &width, int &height) const {
    return mBackend ? mBackend->getOptimalResolution(width, height) : false;
}

bool CameraClient::start(FrameCallback cb) {
    if (mBackend) {
        return mBackend->start(cb);
    }
    return false;
}

void CameraClient::stop() {
    if (mBackend) {
        mBackend->stop();
    }
}

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
