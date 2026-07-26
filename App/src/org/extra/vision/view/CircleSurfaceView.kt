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

package org.extra.vision.view

import android.animation.Animator
import android.animation.AnimatorListenerAdapter
import android.animation.ValueAnimator
import android.content.Context
import android.graphics.Bitmap
import android.graphics.Canvas
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.util.AttributeSet
import android.view.View
import android.view.animation.AccelerateDecelerateInterpolator
import org.extra.vision.R
import kotlin.math.abs

class CircleSurfaceView : View {

    private var mProgressAnimator: ValueAnimator? = null
    private var mProgress = 0.0f
    private var mFrameBitmap: Bitmap? = null

    constructor(context: Context?) : super(context) {}
    constructor(context: Context?, attributeSet: AttributeSet?) : super(context, attributeSet) {}
    constructor(context: Context?, attributeSet: AttributeSet?, defStyleAttr: Int) : super(
        context,
        attributeSet,
        defStyleAttr
    ) {
    }

    fun setProgress(progress: Float) {
        if (progress in 0.0f..100.0f) {
            if (mProgressAnimator != null) {
                mProgressAnimator!!.cancel()
                mProgressAnimator = null
            }
            mProgressAnimator = ValueAnimator.ofFloat(mProgress, progress)
            mProgressAnimator?.interpolator = AccelerateDecelerateInterpolator()
            val duration = abs(1000 * ((progress - mProgress) / 100)).toLong()
            mProgressAnimator?.duration = duration
            mProgressAnimator?.addUpdateListener { animation: ValueAnimator ->
                mProgress = animation.animatedValue as Float
                invalidate()
            }
            mProgressAnimator?.addListener(object : AnimatorListenerAdapter() {
                override fun onAnimationEnd(animation: Animator) {
                    super.onAnimationEnd(animation)
                    mProgressAnimator = null
                }
            })
            mProgressAnimator?.start()
        }
    }

    private var mRotationAngle = 0

    fun setFrame(bitmap: Bitmap, rotationAngle: Int = 0) {
        mFrameBitmap = bitmap
        mRotationAngle = rotationAngle
        postInvalidate()
    }

    override fun onDraw(canvas: Canvas) {
        super.onDraw(canvas)
        val cx = (measuredWidth / 2).toFloat()
        val cy = (measuredHeight / 2).toFloat()
        val min = cx.coerceAtMost(cy)
        val rectF = RectF(
            cx - min,
            cy - min,
            cx + min,
            cy + min
        )
        val paint = Paint()
        paint.isAntiAlias = true
        
        // Draw background circle
        paint.color = context.getColor(R.color.theme_accent_200)
        canvas.drawArc(rectF, 270.0f, 360.0f, true, paint)
        
        // Draw progress arc
        paint.color = context.getColor(R.color.theme_accent_primary)
        canvas.drawArc(rectF, 270.0f, mProgress * 3.6f, true, paint)
        
        // Clip to inner circle for the camera frame
        val path = Path()
        path.addCircle(cx, cy, min * 0.95f, Path.Direction.CCW)
        canvas.save()
        canvas.clipPath(path)
        
        // Draw the camera frame bitmap if available
        mFrameBitmap?.let {
            canvas.save()
            canvas.translate(cx, cy)
            canvas.scale(-1f, 1f)
            canvas.rotate(mRotationAngle.toFloat())

            // Calculate center-cropped source bounds to eliminate stretching
            val src = if (it.width > it.height) {
                val diff = it.width - it.height
                android.graphics.Rect(diff / 2, 0, diff / 2 + it.height, it.height)
            } else {
                val diff = it.height - it.width
                android.graphics.Rect(0, diff / 2, it.width, diff / 2 + it.width)
            }
            val dst = RectF(-min, -min, min, min)
            canvas.drawBitmap(it, src, dst, null)
            canvas.restore()
        }
        
        canvas.restore()
    }
}
