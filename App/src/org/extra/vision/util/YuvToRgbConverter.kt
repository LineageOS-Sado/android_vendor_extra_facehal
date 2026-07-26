package org.extra.vision.util

import android.content.Context
import android.graphics.Bitmap
import android.renderscript.Allocation
import android.renderscript.Element
import android.renderscript.RenderScript
import android.renderscript.ScriptIntrinsicYuvToRGB
import android.renderscript.Type

import kotlin.jvm.Synchronized

class YuvToRgbConverter(context: Context) {
    private val rs = RenderScript.create(context)
    private val yuvToRgbScript = ScriptIntrinsicYuvToRGB.create(rs, Element.U8_4(rs))

    private var yuvType: Type.Builder? = null
    private var rgbaType: Type.Builder? = null
    private var inputAllocation: Allocation? = null
    private var outputAllocation: Allocation? = null

    @Synchronized
    fun convert(nv21: ByteArray, width: Int, height: Int, outputBitmap: Bitmap) {
        val yuvLength = nv21.size

        if (inputAllocation == null || inputAllocation!!.type.x != yuvLength) {
            inputAllocation?.destroy()
            outputAllocation?.destroy()

            yuvType = Type.Builder(rs, Element.U8(rs)).setX(yuvLength)
            inputAllocation = Allocation.createTyped(rs, yuvType!!.create(), Allocation.USAGE_SCRIPT)

            rgbaType = Type.Builder(rs, Element.RGBA_8888(rs)).setX(width).setY(height)
            outputAllocation = Allocation.createTyped(rs, rgbaType!!.create(), Allocation.USAGE_SCRIPT or Allocation.USAGE_SHARED)

            yuvToRgbScript.setInput(inputAllocation)
        }

        inputAllocation!!.copyFrom(nv21)
        yuvToRgbScript.forEach(outputAllocation)
        outputAllocation!!.copyTo(outputBitmap)
    }

    fun destroy() {
        inputAllocation?.destroy()
        outputAllocation?.destroy()
        yuvToRgbScript.destroy()
        rs.destroy()
    }
}
