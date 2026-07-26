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

import android.content.Context
import java.lang.reflect.InvocationTargetException

object Util {

    const val IS_DEBUG_LOGGING = true

    fun getUserId(context: Context?): Int {
        return try {
            Context::class.java.getDeclaredMethod("getUserId", *arrayOfNulls(0))
                .invoke(context, *arrayOfNulls(0)) as Int
        } catch (e: NoSuchMethodException) {
            e.printStackTrace()
            0
        } catch (e: IllegalAccessException) {
            e.printStackTrace()
            0
        } catch (e: InvocationTargetException) {
            e.printStackTrace()
            0
        }
    }
}
