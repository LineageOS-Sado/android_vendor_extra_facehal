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
import android.os.Bundle
import android.text.TextUtils
import android.widget.TextView
import androidx.fragment.app.FragmentActivity
import org.extra.vision.R
import org.extra.vision.util.Constants
import com.google.android.setupdesign.GlifLayout

abstract class FaceBaseActivity : FragmentActivity() {

    private var mLaunchedConfirmLock = false
    @JvmField
    protected var mToken: ByteArray? = null
    protected var mUserId = 0

    override fun onCreate(bundle: Bundle?) {
        super.onCreate(bundle)
        setTheme(R.style.SudThemeGlifV4)
        mToken = intent.getByteArrayExtra(Constants.EXTRA_KEY_CHALLENGE_TOKEN)
        if (bundle != null && mToken == null) {
            mLaunchedConfirmLock = bundle.getBoolean(Constants.EXTRA_KEY_LAUNCHED_CONFIRM)
            mToken = bundle.getByteArray(Constants.EXTRA_KEY_CHALLENGE_TOKEN)
            mUserId = bundle.getInt(Intent.EXTRA_USER_ID)
        }
    }

    override fun onSaveInstanceState(bundle: Bundle) {
        super.onSaveInstanceState(bundle)
        bundle.putBoolean(Constants.EXTRA_KEY_LAUNCHED_CONFIRM, mLaunchedConfirmLock)
        bundle.putByteArray(Constants.EXTRA_KEY_CHALLENGE_TOKEN, mToken)
    }

    protected fun parseIntent(intent: Intent) {
        intent.putExtra(Constants.EXTRA_KEY_CHALLENGE_TOKEN, mToken)
        intent.putExtra(Intent.EXTRA_USER_ID, mUserId)
    }

    abstract fun getLayout(): GlifLayout

    open fun setHeaderText(res: Int) {
        val header: TextView = getLayout().headerTextView
        val previous: CharSequence = header.text
        val current = getText(res)
        if (previous !== current) {
            if (!TextUtils.isEmpty(current)) {
                header.accessibilityLiveRegion = 1
            }
            getLayout().headerText = current
            title = current
        }
    }

}