/*
 * Copyright (C) 2026 The Project MiLahaina
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */

package org.extra.vision.util

import android.hardware.biometrics.BiometricConstants
import androidx.annotation.StringRes
import org.extra.vision.R

/**
 * Maps Extra face HAL vendor codes to user-visible strings.
 * Camera errors come from Session via [android.hardware.face.FaceManager.EnrollmentCallback].
 * Framework may report vendor codes as `BIOMETRIC_ERROR_VENDOR_BASE + halCode` (e.g. 1050 for HAL 50).
 */
object VendorCodeMessages {

    /** Mirror of `com.android.server.biometrics.AcquisitionClient.ACQUIRED_VENDOR_BASE`. */
    private const val ACQUIRED_VENDOR_BASE = 1000

    private val cameraVendorRange =
        Constants.MSG_CAMERA_NO_DEVICE..Constants.MSG_CAMERA_STREAMING_FAILED

    private val faceQualityVendorRange =
        Constants.MSG_UNLOCK_FAILED..Constants.MSG_UNLOCK_HALF_SHADOW

    /**
     * Returns the HAL vendor code (50–58) if this enrollment error is a known camera failure.
     */
    fun resolveCameraVendorCode(errorMessageId: Int, errString: CharSequence?): Int? {
        val base = BiometricConstants.BIOMETRIC_ERROR_VENDOR_BASE
        val fromOffset = errorMessageId - base
        if (fromOffset in cameraVendorRange) return fromOffset
        if (errorMessageId in cameraVendorRange) return errorMessageId
        if (errorMessageId == BiometricConstants.BIOMETRIC_ERROR_VENDOR) {
            errString?.toString()?.trim()?.toIntOrNull()?.let {
                if (it in cameraVendorRange) return it
            }
            Regex("(\\d+)").find(errString?.toString().orEmpty())?.groupValues?.getOrNull(1)
                ?.toIntOrNull()
                ?.let { if (it in cameraVendorRange) return it }
        }
        return null
    }

    /**
     * Returns the HAL face-quality vendor code (5..32) if this help message
     * came from a VENDOR acquired-info report (HAL `EnrollmentFrame`).
     * Handles both `helpMessageId = ACQUIRED_VENDOR_BASE + code` and the raw
     * `helpMessageId = code` shapes seen across framework versions.
     */
    fun resolveFaceQualityVendorCode(helpMessageId: Int, helpString: CharSequence?): Int? {
        val fromVendorBase = helpMessageId - ACQUIRED_VENDOR_BASE
        if (fromVendorBase in faceQualityVendorRange) return fromVendorBase
        if (helpMessageId in faceQualityVendorRange) return helpMessageId
        helpString?.toString()?.trim()?.toIntOrNull()?.let {
            if (it in faceQualityVendorRange) return it
        }
        return null
    }

    @StringRes
    fun stringResForCameraVendor(halCode: Int): Int? = when (halCode) {
        Constants.MSG_CAMERA_NO_DEVICE -> R.string.camera_error_no_device
        Constants.MSG_CAMERA_ID_SELECT_FAILED -> R.string.camera_error_id_select_failed
        Constants.MSG_CAMERA_MANAGER_FAILED -> R.string.camera_error_manager
        Constants.MSG_CAMERA_OPEN_FAILED -> R.string.camera_error_open
        Constants.MSG_CAMERA_IMAGE_READER_FAILED -> R.string.camera_error_image_reader
        Constants.MSG_CAMERA_WINDOW_FAILED -> R.string.camera_error_window
        Constants.MSG_CAMERA_SESSION_FAILED -> R.string.camera_error_session
        Constants.MSG_CAMERA_REQUEST_FAILED -> R.string.camera_error_request
        Constants.MSG_CAMERA_STREAMING_FAILED -> R.string.camera_error_streaming
        else -> null
    }

    /**
     * Localized hint for face quality codes emitted by the HAL during enrollment.
     * Returns null for `KEEP` (no hint) and any code without a user-actionable
     * message so the UI label can stay clear.
     */
    @StringRes
    fun stringResForFaceQualityVendor(halCode: Int): Int? = when (halCode) {
        Constants.MSG_UNLOCK_FACE_NOT_FOUND -> R.string.unlock_failed_face_not_found
        Constants.MSG_UNLOCK_FACE_SCALE_TOO_SMALL -> R.string.unlock_failed_face_small
        Constants.MSG_UNLOCK_FACE_SCALE_TOO_LARGE -> R.string.unlock_failed_face_large
        Constants.MSG_UNLOCK_FACE_OFFSET_LEFT -> R.string.unlock_failed_face_offset_left
        Constants.MSG_UNLOCK_FACE_OFFSET_RIGHT -> R.string.unlock_failed_face_offset_right
        Constants.MSG_UNLOCK_FACE_OFFSET_UP -> R.string.unlock_failed_face_offset_up
        Constants.MSG_UNLOCK_FACE_OFFSET_DOWN -> R.string.unlock_failed_face_offset_down
        Constants.MSG_UNLOCK_FACE_ROTATED_LEFT -> R.string.unlock_failed_face_rotated_left
        Constants.MSG_UNLOCK_FACE_ROTATED_RIGHT -> R.string.unlock_failed_face_rotated_right
        Constants.MSG_UNLOCK_FACE_MULTI -> R.string.unlock_failed_face_multi
        Constants.MSG_UNLOCK_FACE_BLUR -> R.string.unlock_failed_face_blur
        Constants.MSG_UNLOCK_FACE_NOT_COMPLETE -> R.string.unlock_failed_face_not_complete
        Constants.MSG_UNLOCK_DARKLIGHT -> R.string.attr_light_dark
        Constants.MSG_UNLOCK_HIGHLIGHT -> R.string.attr_light_high
        Constants.MSG_UNLOCK_HALF_SHADOW -> R.string.attr_light_shadow
        else -> null
    }
}
