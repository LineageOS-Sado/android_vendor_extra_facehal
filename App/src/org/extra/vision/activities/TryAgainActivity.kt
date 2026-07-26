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
import android.os.Bundle
import android.view.View
import org.extra.vision.R
import org.extra.vision.util.Constants
import org.extra.vision.util.VendorCodeMessages
import com.google.android.setupcompat.template.FooterBarMixin
import com.google.android.setupcompat.template.FooterButton
import com.google.android.setupdesign.GlifLayout
import com.google.android.setupdesign.util.ThemeHelper


class TryAgainActivity : FaceBaseActivity() {

    override fun onCreate(bundle: Bundle?) {
        ThemeHelper.applyTheme(this);
        ThemeHelper.trySetDynamicColor(this);
        super.onCreate(bundle)
        setContentView(R.layout.face_enroll_try_again)
        setHeaderText(R.string.face_try_again_title)
        val errorMsg = intent.getStringExtra("error_message")
        val cameraVendor = intent.getIntExtra(Constants.EXTRA_KEY_ENROLL_CAMERA_VENDOR_CODE, -1)
        val descRes = VendorCodeMessages.stringResForCameraVendor(cameraVendor)
        if (!errorMsg.isNullOrEmpty()) {
            getLayout().setDescriptionText(errorMsg)
        } else if (descRes != null) {
            getLayout().setDescriptionText(descRes)
        } else {
            getLayout().setDescriptionText(R.string.face_try_again_description)
        }

        val footerBarMixin = getLayout().getMixin(FooterBarMixin::class.java) as FooterBarMixin
        footerBarMixin.primaryButton =
            FooterButton.Builder(this)
                .setText(R.string.btn_try_again)
                .setListener { setTryAgainButton() }
                .setButtonType(FooterButton.ButtonType.OTHER)
                .setTheme(R.style.SudGlifButton_Primary)
                .build()
        if (mToken == null) {
            footerBarMixin.primaryButton.visibility = View.INVISIBLE
        }
    }

    override fun onApplyThemeResource(theme: Resources.Theme, resid: Int, first: Boolean) {
        theme.applyStyle(R.style.SetupWizardPartnerResource, true)
        super.onApplyThemeResource(theme, resid, first)
    }

    override fun getLayout(): GlifLayout {
        return findViewById(R.id.face_enroll_try_again)!!
    }

    public override fun onPause() {
        super.onPause()
        finish()
    }

    private fun setTryAgainButton() {
        val intent = Intent()
        intent.setClass(this@TryAgainActivity, EnrollActivity::class.java)
        parseIntent(intent)
        startActivity(intent)
        finish()
    }

}
