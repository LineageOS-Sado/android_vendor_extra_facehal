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

package org.extra.vision

import android.app.Application
import android.content.ComponentName
import android.content.pm.PackageManager
import android.util.Log
import org.extra.vision.activities.EnrollActivity
import org.extra.vision.util.Util

class VisionApp : Application() {

    override fun onCreate() {
        if (Util.IS_DEBUG_LOGGING) Log.i(TAG, "onCreate")
        super.onCreate()
        app = this
        packageManager.setComponentEnabledSetting(
            ComponentName(this, EnrollActivity::class.java),
            PackageManager.COMPONENT_ENABLED_STATE_ENABLED,
            PackageManager.DONT_KILL_APP
        )
    }

    override fun onTerminate() {
        if (Util.IS_DEBUG_LOGGING) {
            Log.i(TAG, "onTerminate")
        }
        super.onTerminate()
    }

    companion object {
        private const val TAG = "VisionApp"
        var app: VisionApp? = null
    }
}
