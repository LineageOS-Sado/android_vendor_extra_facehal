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

import android.app.Activity
import android.content.Intent
import android.content.res.Resources
import android.os.Bundle
import com.google.android.setupcompat.template.FooterBarMixin
import com.google.android.setupcompat.template.FooterButton
import com.google.android.setupdesign.GlifLayout
import com.google.android.setupdesign.util.ThemeHelper
import org.extra.vision.R
import org.extra.vision.util.Constants

class FaceEnrollFinishActivity : FaceBaseActivity() {

    override fun onCreate(bundle: Bundle?) {
        ThemeHelper.applyTheme(this)
        ThemeHelper.trySetDynamicColor(this)
        super.onCreate(bundle)
        setContentView(R.layout.face_enroll_finish)
        setHeaderText(R.string.face_enroll_finish_title)

        val footerBarMixin = getLayout().getMixin(FooterBarMixin::class.java) as FooterBarMixin
        footerBarMixin.primaryButton =
            FooterButton.Builder(this)
                .setText(R.string.face_enroll_done)
                .setListener { finishEnrollment() }
                .setButtonType(FooterButton.ButtonType.NEXT)
                .setTheme(R.style.SudGlifButton_Primary)
                .build()
    }

    override fun onApplyThemeResource(theme: Resources.Theme, resid: Int, first: Boolean) {
        theme.applyStyle(R.style.SetupWizardPartnerResource, true)
        super.onApplyThemeResource(theme, resid, first)
    }

    override fun getLayout(): GlifLayout {
        return findViewById(R.id.face_enroll_finish)!!
    }

    private fun finishEnrollment() {
        val result = Intent()
        if (mToken != null) {
            result.putExtra(Constants.EXTRA_KEY_CHALLENGE_TOKEN, mToken)
        }
        if (mUserId != android.os.UserHandle.USER_NULL) {
            result.putExtra(Intent.EXTRA_USER_ID, mUserId)
        }
        result.putExtra(EXTRA_FINISHED_ENROLL_FACE, true)
        setResult(RESULT_FINISHED, result)
        finish()
    }

    companion object {
        const val EXTRA_FINISHED_ENROLL_FACE = "finished_enrolling_face"
        const val RESULT_FINISHED = Activity.RESULT_FIRST_USER
    }
}
