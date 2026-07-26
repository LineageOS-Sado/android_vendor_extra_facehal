package vendor.extra.biometrics.face;

import android.os.ParcelFileDescriptor;

@VintfStability
interface IVisionService {

    oneway void onFrame(in ParcelFileDescriptor fd, int width, int height, int angle);

    int getVendorCode();

    /**
     * Returns the face landmark coordinates from the last Face detection.
     * Format: [score, right_eye_x, right_eye_y, left_eye_x, left_eye_y,
     *          nose_x, nose_y, mouth_x, mouth_y, right_ear_x, right_ear_y,
     *          left_ear_x, left_ear_y]
     * All coordinates are normalized [0,1] relative to frame dimensions.
     * Returns an empty array if no face was detected.
     */
    float[] getLastLandmarks();

    void setCallback(in IVisionService callback);
}
