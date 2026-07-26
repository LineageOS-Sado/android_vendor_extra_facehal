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

package org.extra.vision.util

object Constants {

    const val EXTRA_KEY_CHALLENGE_TOKEN = "hw_auth_token"
    const val EXTRA_KEY_LAUNCHED_CONFIRM = "launched_confirm_lock"
    const val EXTRA_KEY_ENROLL_CAMERA_VENDOR_CODE = "enroll_camera_vendor_code"

    const val MSG_UNLOCK_DARKLIGHT = 30
    const val MSG_UNLOCK_FACE_BLUR = 28
    const val MSG_UNLOCK_FACE_MULTI = 27
    const val MSG_UNLOCK_FACE_NOT_COMPLETE = 29
    const val MSG_UNLOCK_FACE_NOT_FOUND = 5
    const val MSG_UNLOCK_FACE_OFFSET_LEFT = 8
    const val MSG_UNLOCK_FACE_OFFSET_RIGHT = 10
    const val MSG_UNLOCK_FACE_OFFSET_UP = 11
    const val MSG_UNLOCK_FACE_OFFSET_DOWN = 12
    const val MSG_UNLOCK_FACE_ROTATED_LEFT = 15
    const val MSG_UNLOCK_FACE_ROTATED_RIGHT = 17
    const val MSG_UNLOCK_FACE_SCALE_TOO_LARGE = 7
    const val MSG_UNLOCK_FACE_SCALE_TOO_SMALL = 6
    const val MSG_UNLOCK_FAILED = 3
    const val MSG_UNLOCK_HALF_SHADOW = 32
    const val MSG_UNLOCK_HIGHLIGHT = 31
    const val MSG_UNLOCK_KEEP = 19

    // Camera / Face HAL (Camera2NdkBackend Session camera start) — match ExtraFaceHal FaceEngine.h
    const val MSG_CAMERA_NO_DEVICE = 50
    const val MSG_CAMERA_ID_SELECT_FAILED = 51
    const val MSG_CAMERA_MANAGER_FAILED = 52
    const val MSG_CAMERA_OPEN_FAILED = 53
    const val MSG_CAMERA_IMAGE_READER_FAILED = 54
    const val MSG_CAMERA_WINDOW_FAILED = 55
    const val MSG_CAMERA_SESSION_FAILED = 56
    const val MSG_CAMERA_REQUEST_FAILED = 57
    const val MSG_CAMERA_STREAMING_FAILED = 58
}
