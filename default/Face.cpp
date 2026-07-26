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
#include "Face.h"
#include "Session.h"
#include <android-base/logging.h>
#include <aidl/android/hardware/biometrics/face/FaceSensorType.h>
#include <aidl/android/hardware/biometrics/common/SensorStrength.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::biometrics::face::FaceSensorType;
using aidl::android::hardware::biometrics::common::SensorStrength;

Face::Face() {
    SensorProps props;
    FaceEngine::getInstance().getSensorProps(props);    
    mSensorProps.push_back(props);
}

ScopedAStatus Face::getSensorProps(std::vector<SensorProps>* _aidl_return) {
    LOG(INFO) << "getSensorProps";
    *_aidl_return = mSensorProps;
    return ScopedAStatus::ok();
}

ScopedAStatus Face::createSession(int32_t sensorId, int32_t userId,
                                  const std::shared_ptr<ISessionCallback>& cb,
                                  std::shared_ptr<ISession>* _aidl_return) {
    LOG(INFO) << "createSession for sensor: " << sensorId << " user: " << userId;
    if (sensorId != 1008) {
        LOG(ERROR) << "Unknown sensorId: " << sensorId;
        return ScopedAStatus::fromExceptionCode(EX_ILLEGAL_ARGUMENT);
    }
    
    std::shared_ptr<Session> session = ndk::SharedRefBase::make<Session>(userId, cb);
    *_aidl_return = session;
    return ScopedAStatus::ok();
}

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
