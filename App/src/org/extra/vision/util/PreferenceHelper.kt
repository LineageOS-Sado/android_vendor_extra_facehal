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

import android.annotation.SuppressLint
import android.content.Context
import android.preference.PreferenceManager
import android.text.TextUtils

class PreferenceHelper(context: Context) {

    private val mContext: Context

    @SuppressLint("ApplySharedPref")
    fun saveIntValue(key: String?, value: Int) {
        val edit = PreferenceManager.getDefaultSharedPreferences(mContext).edit()
        edit.putInt(key, value)
        edit.commit()
    }

    @SuppressLint("ApplySharedPref")
    fun saveBooleanValue(key: String?, value: Boolean) {
        val edit = PreferenceManager.getDefaultSharedPreferences(mContext).edit()
        edit.putBoolean(key, value)
        edit.commit()
    }

    @SuppressLint("ApplySharedPref")
    fun removeSharePreferences(value: String?) {
        val edit = PreferenceManager.getDefaultSharedPreferences(mContext).edit()
        edit.remove(value)
        edit.commit()
    }

    fun getIntValueByKey(key: String?, value: Int): Int {
        return PreferenceManager.getDefaultSharedPreferences(mContext).getInt(key, value)
    }

    fun getIntValueByKey(key: String?): Int {
        return getIntValueByKey(key, -1)
    }

    @SuppressLint("ApplySharedPref")
    fun saveStringValue(key: String?, value: String?) {
        val edit = PreferenceManager.getDefaultSharedPreferences(mContext).edit()
        edit.putString(key, value)
        edit.commit()
    }

    fun getStringValueByKey(key: String?): String? {
        return PreferenceManager.getDefaultSharedPreferences(mContext).getString(key, null)
    }

    @SuppressLint("ApplySharedPref")
    fun saveByteArrayValue(key: String?, value: ByteArray?) {
        val edit = PreferenceManager.getDefaultSharedPreferences(mContext).edit()
        edit.putString(key, String(value!!))
        edit.commit()
    }

    fun getByteArrayValueByKey(key: String?): ByteArray? {
        val string = PreferenceManager.getDefaultSharedPreferences(mContext).getString(key, null)
        return if (TextUtils.isEmpty(string)) {
            null
        } else string!!.toByteArray()
    }

    fun getBooleanValueByKey(key: String?): Boolean {
        return PreferenceManager.getDefaultSharedPreferences(mContext).getBoolean(key, false)
    }

    init {
        mContext = context.applicationContext
    }
}