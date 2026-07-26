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

package org.extra.vision.activities

import android.content.Intent
import android.content.res.Resources
import android.hardware.face.FaceManager
import android.media.AudioAttributes
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.graphics.ImageFormat
import android.graphics.Rect
import android.graphics.YuvImage
import android.os.*
import android.os.ServiceManager
import android.util.Log
import android.widget.TextView
import org.extra.vision.R
import org.extra.vision.util.Constants
import org.extra.vision.util.PreferenceHelper
import org.extra.vision.util.Util
import org.extra.vision.util.VendorCodeMessages
import org.extra.vision.util.YuvToRgbConverter
import org.extra.vision.view.CircleSurfaceView
import com.google.android.setupdesign.GlifLayout
import com.google.android.setupdesign.util.ThemeHelper

import vendor.extra.biometrics.face.IVisionService


open class EnrollActivity : FaceBaseActivity() {
    private var mEnrollmentCancel: CancellationSignal? = null
    private var mFaceManager: FaceManager? = null
    private var mPreferenceHelper: PreferenceHelper? = null
    private var mSurfaceView: CircleSurfaceView? = null
    private var mEnrollVendorMessage: TextView? = null
    private var mProgress = 0.0f
    private var mFinishScheduled = false
    private var mIsActivityPaused = false
    private var mVisionService: IVisionService? = null
    private var mYuvToRgbConverter: YuvToRgbConverter? = null
    private var mReusableBitmap1: Bitmap? = null
    private var mReusableBitmap2: Bitmap? = null
    private var mUsingBitmap1 = true

    private val mBackgroundHandlerThread = HandlerThread("FrameProcessor").apply { start() }
    private val mBackgroundHandler = Handler(mBackgroundHandlerThread.looper)
    private val mIsProcessingFrame = java.util.concurrent.atomic.AtomicBoolean(false)

    private val mVisionServiceImpl = object : vendor.extra.biometrics.face.IVisionService.Stub() {
        override fun onFrame(pfd: ParcelFileDescriptor, width: Int, height: Int, angle: Int) {
            if (mIsProcessingFrame.compareAndSet(false, true)) {
                try {
                    val size = (width * height * 1.5).toInt() // NV21
                    val fd = pfd.fileDescriptor

                    val data = ByteArray(size)
                    val fis = java.io.FileInputStream(fd)
                    fis.read(data)
                    pfd.close()

                    mBackgroundHandler.post {
                        try {
                            renderFrame(data, width, height, angle)
                        } catch (e: Exception) {
                            Log.e(TAG, "Error processing frame in background", e)
                        } finally {
                            mIsProcessingFrame.set(false)
                        }
                    }
                } catch (e: Exception) {
                    Log.e(TAG, "Error receiving frame from HAL", e)
                    try {
                        pfd.close()
                    } catch (ignored: Exception) {}
                    mIsProcessingFrame.set(false)
                }
            } else {
                try {
                    pfd.close()
                } catch (ignored: Exception) {}
            }
        }

        override fun setCallback(callback: IVisionService?) {
            // Callback-to-App direction: not used
        }

        override fun getVendorCode(): Int = 0
        override fun getLastLandmarks(): FloatArray = FloatArray(0)
        override fun getInterfaceVersion(): Int = IVisionService.VERSION
        override fun getInterfaceHash(): String = IVisionService.HASH
    }

    private fun renderFrame(nv21: ByteArray, width: Int, height: Int, angle: Int) {
        try {
            if (mReusableBitmap1 == null || mReusableBitmap1!!.width != width || mReusableBitmap1!!.height != height) {
                mReusableBitmap1?.recycle()
                mReusableBitmap1 = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            }
            if (mReusableBitmap2 == null || mReusableBitmap2!!.width != width || mReusableBitmap2!!.height != height) {
                mReusableBitmap2?.recycle()
                mReusableBitmap2 = Bitmap.createBitmap(width, height, Bitmap.Config.ARGB_8888)
            }

            val bitmap = if (mUsingBitmap1) mReusableBitmap2 else mReusableBitmap1
            if (bitmap == null) return

            mYuvToRgbConverter?.convert(nv21, width, height, bitmap)
            mUsingBitmap1 = !mUsingBitmap1
            runOnUiThread {
                mSurfaceView?.setFrame(bitmap, angle)
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error rendering frame", e)
        }
    }

    private val mEnrollmentCallback: FaceManager.EnrollmentCallback =
        object : FaceManager.EnrollmentCallback() {
            override fun onEnrollmentProgress(remaining: Int) {
                runOnUiThread {
                    val nextProgress = if (remaining == 0) {
                        100.0f
                    } else {
                        ((ENROLL_REQUIRED_GOOD_FRAMES - remaining)
                            .coerceIn(0, ENROLL_REQUIRED_GOOD_FRAMES) * 100.0f) /
                            ENROLL_REQUIRED_GOOD_FRAMES
                    }

                    if (nextProgress > mProgress) {
                        mProgress = nextProgress
                        mSurfaceView?.setProgress(mProgress)
                    }
                    mEnrollVendorMessage?.text = ""

                    if (remaining == 0 && !mFinishScheduled) {
                        mFinishScheduled = true
                        Handler(Looper.getMainLooper()).postDelayed({
                            val enrollActivity = this@EnrollActivity
                            if (!enrollActivity.isDestroyed) {
                                startFinishActivity()
                            }
                        }, 2000)
                    }
                }
            }

            override fun onEnrollmentHelp(helpMessageId: Int, charSequence: CharSequence?) {
                if (helpMessageId < 1000) {
                    runOnUiThread {
                        if (!charSequence.isNullOrEmpty()) {
                            mEnrollVendorMessage?.text = charSequence
                        } else {
                            mEnrollVendorMessage?.text = ""
                        }
                    }
                } else {
                    val vendorCode = helpMessageId - 1000
                    val stringRes = VendorCodeMessages.stringResForFaceQualityVendor(vendorCode)
                    runOnUiThread {
                        if (stringRes != null) {
                            mEnrollVendorMessage?.setText(stringRes)
                        } else if (!charSequence.isNullOrEmpty()) {
                            mEnrollVendorMessage?.text = charSequence
                        } else {
                            // KEEP / unknown vendor codes shouldn't pollute the hint.
                            mEnrollVendorMessage?.text = ""
                        }
                    }
                }
            }

            override fun onEnrollmentError(errorMessageId: Int, charSequence: CharSequence?) {
                if (!mIsActivityPaused) {
                    val intent = Intent()
                    intent.setClass(this@EnrollActivity, TryAgainActivity::class.java)

                    if (errorMessageId >= 1000) {
                        val vendorCode = errorMessageId - 1000
                        if (vendorCode in 50..58) {
                            intent.putExtra(Constants.EXTRA_KEY_ENROLL_CAMERA_VENDOR_CODE, vendorCode)
                        } else if (!charSequence.isNullOrEmpty()) {
                            intent.putExtra("error_message", charSequence.toString())
                        }
                    } else {
                        if (!charSequence.isNullOrEmpty()) {
                            intent.putExtra("error_message", charSequence.toString())
                        }
                    }

                    Log.i(TAG, "Enrollment error: frameworkId=$errorMessageId message=$charSequence")
                    if (errorMessageId != Constants.MSG_UNLOCK_FAILED) {
                        parseIntent(intent)
                    } else {
                        setResult(-1)
                    }
                    startActivity(intent)
                }
                finish()
            }
        }


    override fun onCreate(bundle: Bundle?) {
        ThemeHelper.applyTheme(this)
        ThemeHelper.trySetDynamicColor(this)
        super.onCreate(bundle)
        mPreferenceHelper = PreferenceHelper(this)
        mFaceManager = getSystemService(FaceManager::class.java)
        setContentView(R.layout.face_enroll)
        setHeaderText(R.string.face_enroll_title)
        getLayout().setDescriptionText(R.string.face_enroll_description)
        mSurfaceView = findViewById(R.id.camera_surface)
        mEnrollVendorMessage = findViewById(R.id.face_vendor_message)

        mYuvToRgbConverter = YuvToRgbConverter(this)

        // Bind to the HAL's IVisionService once
        bindVisionService()
    }

    private fun bindVisionService() {
        try {
            Log.i(TAG, "Searching for HAL IVisionService...")
            val binder = ServiceManager.getService("vendor.extra.biometrics.face.IVisionService/default")
            if (binder != null) {
                mVisionService = IVisionService.Stub.asInterface(binder)
                // Pass our local implementation as the callback to the HAL
                mVisionService?.setCallback(mVisionServiceImpl)
                Log.i(TAG, "Connected to HAL IVisionService and registered callback")
            } else {
                Log.e(TAG, "HAL IVisionService not found")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Error interacting with IVisionService", e)
        }
    }

    override fun onApplyThemeResource(theme: Resources.Theme, resid: Int, first: Boolean) {
        theme.applyStyle(R.style.SetupWizardPartnerResource, true)
        super.onApplyThemeResource(theme, resid, first)
    }

    private fun startEnrollment() {
        mProgress = 0.0f
        mFinishScheduled = false
        mSurfaceView?.setProgress(0.0f)

        if (mToken != null && mToken!!.isNotEmpty()) {
            mEnrollmentCancel = CancellationSignal()
            mFaceManager!!.enroll(
                Util.getUserId(this),
                mToken,
                mEnrollmentCancel,
                mEnrollmentCallback,
                intArrayOf(1)
            )
        }
    }

    override fun getLayout(): GlifLayout {
        return findViewById(R.id.face_enroll)!!
    }

    override fun onDestroy() {
        super.onDestroy()
        mBackgroundHandlerThread.quitSafely()
        mYuvToRgbConverter?.destroy()
        mYuvToRgbConverter = null
        mReusableBitmap1?.recycle()
        mReusableBitmap1 = null
        mReusableBitmap2?.recycle()
        mReusableBitmap2 = null
        Log.i(TAG, "onDestroy")
    }

    override fun onPause() {
        super.onPause()
        mIsActivityPaused = true
        mEnrollmentCancel?.cancel()
    }

    override fun onResume() {
        super.onResume()
        mIsActivityPaused = false
        startEnrollment()
    }

    private fun startFinishActivity() {
        val intent = Intent()
        intent.setClass(this@EnrollActivity, FaceEnrollFinishActivity::class.java)
        if (mToken != null) {
            intent.putExtra(Constants.EXTRA_KEY_CHALLENGE_TOKEN, mToken)
        }
        if (mUserId != UserHandle.USER_NULL) {
            intent.putExtra(Intent.EXTRA_USER_ID, mUserId)
        }
        startActivityForResult(intent, REQUEST_FINISH)
    }

    override fun onBackPressed() {
        super.onBackPressed()
        finish()
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (requestCode == REQUEST_FINISH) {
            setResult(resultCode, data)
            finish()
        }
    }

    companion object {
        private const val REQUEST_FINISH = 1
        private val TAG = EnrollActivity::class.java.simpleName
        private val CLICK_VIBRATION: VibrationEffect = VibrationEffect.get(0)
        private val SONIFICATION = AudioAttributes.Builder()
            .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
            .setUsage(AudioAttributes.USAGE_ASSISTANCE_SONIFICATION).build()

        private const val ENROLL_REQUIRED_GOOD_FRAMES = 5
    }
}
