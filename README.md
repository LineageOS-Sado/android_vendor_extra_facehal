# Extra Face HAL

This repository contains the biometric Face Hardware Abstraction Layer (HAL) implementation for the Extra project, supporting Android's stable AIDL interfaces (`android.hardware.biometrics.face`).

> [!NOTE]
> This is a hobby project shared/made for educational and experimentation purposes. No one has any obligation to use it. You have full access to the source code and can design/integrate your own face unlock logic.

## How to Use

To integrate this Face HAL implementation into your device build:

1. Clone this repository into your AOSP root (e.g., under `vendor/extra/facehal`):
   ```bash
   git clone https://github.com/IPSBHANGU/AOSP-FACEHAL vendor/extra/facehal
   ```
2. Inherit/include `facehal.mk` in your device's configuration Makefile (`device.mk`):
   ```makefile
   $(call inherit-product, vendor/extra/facehal/facehal.mk)
   ```
3. (Optional) Configure build system flags in your `device.mk` before inheriting `facehal.mk`:
   * **Engine Model Selection** (default is `extra`):
     ```makefile
     # Select "extra" or "megvii"
     EXTRA_FACEHAL_ENGINE_MODEL := extra
     ```
   * **Logging Control** (default is `false`):
     ```makefile
     # Set to true to enable verbose HAL service debug logging
     EXTRA_FACEHAL_ENABLE_LOGGING := true
     ```


---

## How It Works: High-Level Architecture

The Extra Face HAL is designed with a modular separation of concerns between open-source framework glue and proprietary/compiled biometric engines.

```
                  +-----------------------------------------+
                  |  Android System (Biometrics Framework)  |
                  +--------------------+--------------------+
                                       | (AIDL Calls)
                                       v
                  +--------------------+--------------------+
                  |    android.hardware.biometrics.face     |
                  |          HAL Daemon (Service)           |
                  +--------------------+--------------------+
                                       |
                    (Statically links libextra_facehal_core)
                                       v
                  +--------------------+--------------------+
                  |     libextra_facehal_core (Static)  |
                  |  - Session management & camera pipeline |
                  |  - Keystore2 & template encryption      |
                  +--------------------+--------------------+
                                       |
                    (Dynamically links libextra_face_engine.so)
                                       v
                  +--------------------+--------------------+
                  |      libextra_face_engine (Shared)  |
                  |  - Receives storage callbacks at init   |
                  |  - Resolves landmarks & face scores     |
                  |  - [Stub] or [Proprietary Engine]       |
                  +-----------------------------------------+
```

1. **Service Daemon & Core HAL (`libextra_facehal_core`)**:
   - Manages the biometric session lifecycle (enrollment, authentication, cancellation).
   - Operates the NDK Camera2 backend to capture incoming video frames.
   - Manages cryptographic keys via Android Keystore2 and handles persistent database storage/encryption.

2. **Face Engine Interface (`libextra_face_engine.so`)**:
   - Acts as the main brain logic of the face-unlock pipeline (responsible for face detection, quality analysis, embedding extraction, and matching).
   - The core HAL registers functional callbacks (`FaceEngineCallbacks`) during initialization. Whenever the engine needs to save, load, or delete face embeddings, it invokes these callbacks.
   - Exposes a clean C++ public interface utilizing the **Pimpl (Pointer to Implementation)** pattern, hiding execution details from compiling clients.

3. **Vision Service Interface (`vendor.extra.biometrics.face.IVisionService`)**:
   - A custom AIDL interface hosted by the HAL daemon alongside the standard Android `IFace` biometric interface.
   - Purpose: It serves as the framework bridge that allows system/platform application clients (such as the `FaceUnlock` app) to register callbacks and securely receive real-time camera frames and calculated landmark/pose coordinates.
   - Utilizes Ashmem (shared memory buffers) to transfer high-resolution frames efficiently, enabling fluid camera previews and face mesh animations during enrollment without blocking the core biometric pipeline.

---

## Build System Configuration (Stub vs. Proprietary)

The compilation behavior is governed by the Android Soong build configuration namespace `extra_facehal` and the modules defined in `lib/Android.bp`.

### 1. Prebuilt Proprietary Mode (Default)
By default, the repository targets the prebuilt proprietary shared library (`lib/libextra_face_engine.so`) using the `cc_prebuilt_library_shared` module in `lib/Android.bp`. This contains the fully optimized vendor engine with actual biometric algorithms.

### 2. Open-Source Stub Mode
If you wish to compile the engine from source using the open-source dummy stub (`lib/FaceEngineStub.cpp`) containing dummy biometric calls:
1. Open `lib/Android.bp` and comment out the `cc_prebuilt_library_shared` module for `libextra_face_engine`.
2. Uncomment the `cc_library_shared` module for `libextra_face_engine` that lists `FaceEngineStub.cpp` as a source.
3. Build the targets from your Android build environment root:
   ```bash
   m libextra_face_engine
   m android.hardware.biometrics.face-service.extra
   ```

---

## Cryptographic Architecture & Secure Storage

To satisfy Android biometrics security requirements, user enrollment templates are serialized, signed, and encrypted before being written to `/data/vendor/biometrics/face/`.

### 1. Serialization Protocol (`FaceTemplateSerializer.cpp`)
Biometric templates are serialized into a binary stream containing:
- `uint32_t` Version
- `int32_t` Face ID
- `int32_t` User ID
- `uint32_t` Template Size
- `uint8_t[]` Template Raw Embedding Data
- `uint32_t` FNV-1a Checksum (calculated over the preceding fields)

### 2. Integrity Protection (KeyMint HMAC-SHA256)
To detect offline tampering, the serialized binary is signed using a hardware-backed HMAC key.
- **Key Alias**: `extra_face_template_hmac_sha256_v2`
- **Namespace**: `65000` (Domain: `SELINUX`)
- **Key Type**: 256-bit HMAC key generated and managed inside KeyMint.
- The 256-bit signature (32 bytes) is appended directly to the end of the serialized payload. During deserialization, the MAC signature is verified prior to parsing.

### 3. Data Confidentiality (KeyMint AES-GCM-256)
The MAC-signed serialized payload is encrypted using hardware-backed AES-GCM-256 keys generated per Android user.
- **Key Alias**: `extra_face_template_aes_gcm_v2_user_<userId>`
- **Namespace**: `65000` (Domain: `SELINUX`)
- **Mode**: AES-GCM with a random 96-bit nonce and 128-bit authentication tag.
- The encrypted output is formatted as a versioned storage payload:
  `[1-byte Version] [1-byte Nonce Length] [Nonce Bytes...] [Ciphertext + Auth Tag Bytes...]`

### 4. Non-Encrypted Plaintext Fallback
If KeyMint/Keystore2 services are unavailable (e.g. during disabled verity/encryption), the core HAL will gracefully fall back to storing only the MAC-signed, checksum-validated serialization stream.
- **Path**: `/data/vendor/biometrics/face/user_<userId>/face_<faceId>_without_encryption.dat`
- **Security Warning**: Although templates stored in this fallback path are still protected against tampering via the hardware-backed HMAC signature, they are not encrypted. As soon as Keystore2 becomes available and a new enrollment session is started, fallback files are processed, re-encrypted, and the unencrypted file versions are securely wiped.

---

## State Management & Custom Error Propagation

The HAL utilizes a thread-safe singleton, `VendorStateManager`, to coordinate asynchronous event states across multiple system boundaries.

### Asynchronous Vendor Codes
Standard AIDL `IFace` sessions communicate status frames. When non-standard events occur, the HAL propagates them using custom vendor codes (e.g., to trigger specific user interface prompts):
- `VendorCode::REQUIRE_REENROLLMENT` (`1059`): Signals the framework/app that the stored template version has changed or is incompatible, requiring a user re-enrollment prompt.
- `VendorCode::DARKLIGHT` (`30`), `VendorCode::HIGHLIGHT` (`31`), `VendorCode::HALF_SHADOW` (`32`): Camera/ambient lighting state quality feedback.
- `VendorCode::CAMERA_NO_DEVICE` (`50`), `VendorCode::CAMERA_OPEN_FAILED` (`53`): Low-level camera acquisition/configuration failures.

---

## IVisionService & Landmark Visualization

The Face HAL exposes a vendor-specific AIDL interface alongside standard biometrics APIs: `vendor.extra.biometrics.face.IVisionService`.

### 1. High-Performance Preview Delivery (Ashmem)
Since raw high-resolution NV21 frames are too large to pass over Binder IPC, `IVisionService` uses Android Shared Memory (Ashmem):
1. The camera capture thread writes NV21 frames directly into an ashmem region.
2. The HAL passes a read-only `ParcelFileDescriptor` to client applications.
3. The client maps the descriptor to instantly access frame bytes with zero copy overhead, maintaining high framerates for UI rendering.

### 2. Face Landmarks Coordinate Layout
The interface method `getLastLandmarks()` returns a 13-element float array representing the key coordinates calculated from the last successful face detection frame.
- **Coordinate format**: `[score, right_eye_x, right_eye_y, left_eye_x, left_eye_y, nose_x, nose_y, mouth_x, mouth_y, right_ear_x, right_ear_y, left_ear_x, left_ear_y]`
- **Normalization**: All coordinates are mapped to the `[0.0, 1.0]` range relative to the frame dimensions.
- **Fallback**: Returns an empty array if no face is detected in the frame.

---

## SELinux / SEPolicy Configuration

The SELinux policies required by the Face HAL are located in the [sepolicy/](./sepolicy/) directory.

These policies are automatically integrated into the Android build system when you include [facehal.mk](./facehal.mk) inside your `device.mk`.

---

## Face Engine Integration Options (`libextra_face_engine`)

The build system supports selecting between two different prebuilt face engine implementations:

### 1. Custom Extra Library (`extra`)
This is the default configuration, incorporating the custom face engine built specifically for this project.

* **Face Detection & Landmarks:** Real-time CNN-based face detection (`FaceDetector`) tracking 6 key facial landmarks (12 coordinate values) to determine face position and size.
* **Biometric Embeddings:** A neural network (`FaceEmbedder`) generating a 512-dimensional face recognition vector.
* **Multi-Modal Anti-Spoofing (Liveness):**
  * *Texture Analysis:* Laplacian variance calculations detect defocus and screen refresh patterns.
  * *Color Analysis:* YCrCb chrominance range checks verify human skin color spectra.
  * *Instant Parallax:* 3D geometric ratios between landmarks identify flat 2D photos.
  * *Multi-Frame Parallax:* Tracked landmark history configurations verify depth changes through head motion.
* **Attention Detection (Eye Openness CNN):**
  * *Custom Lightweight CNN:* A dedicated 3-layer Convolutional Neural Network performs eye openness classification for left and right eyes.
  * *Zero-Malloc Runtime:* All CNN layer buffers are pre-allocated during initialization, guaranteeing zero heap allocations during inference.
  * *Heuristic-Free Detection:* Replaces legacy pixel contrast heuristics with learned CNN-based eye state estimation for improved robustness across lighting conditions.
* **Lighting & Exposure Analysis (FaceLighting)**: Real-time Y-channel frame analysis diagnosing underexposure (`DARKLIGHT`), overexposure (`HIGHLIGHT`), low contrast, or rotation-safe side shadows (`HALF_SHADOW`).

### 2. Megvii Library (`megvii`)
This builds using the Megvii proprietary prebuilt face engine and relies on Megvii's proprietary logic.

The source code for this face engine configuration can be picked from the [FaceLib-Megvii](https://github.com/IPSBHANGU/FaceLib-Megvii.git) repository.

---

## Implementing a Custom Face Engine

To implement your own face unlock engine, you can replace the stub library (`FaceEngineStub.cpp`) with your own custom logic.

Because the architecture decouples core system tasks (like camera capture, database serialization, and Keystore2 encryption) from the raw face recognition algorithms, your custom engine does not need to handle database storage, hashing, or crypto keys. Instead, your engine only needs to handle face detection, quality analysis, embedding extraction, and similarity comparisons.

### 1. The FaceEngineCallbacks System
During initialization, the core HAL registers callbacks (`FaceEngineCallbacks`) that decouple the engine from the database and encryption layers:
- `callbacks.saveTemplate(userId, faceId, embedding)`: Saves a float vector embedding to secure storage.
- `callbacks.loadTemplates(userId)`: Loads all templates for a user (returning a list of `pair<int32_t, vector<float>>`).
- `callbacks.deleteTemplate(userId, faceId)`: Deletes a specific template from secure storage.

### 2. Implementation Template (`lib/FaceEngineCustom.cpp`)
Create your custom implementation file implementing the interface in `include/FaceEngine.h`. You can use the **Pimpl (Pointer to Implementation)** pattern to keep your internal engine state private:

```cpp
#include "FaceEngine.h"
#include <android-base/logging.h>
#include <map>
#include <mutex>

namespace org {
namespace extra {
namespace face {
namespace hal {

struct FaceEngine::Impl {
    FaceEngineCallbacks mCallbacks;
    std::map<int32_t, std::vector<float>> enrolledEmbeddings;
    std::mutex enrollmentMutex;
};

FaceEngine &FaceEngine::getInstance() {
    static FaceEngine instance;
    return instance;
}

FaceEngine::FaceEngine() : mImpl(std::make_unique<Impl>()) {}
FaceEngine::~FaceEngine() = default;

bool FaceEngine::init(const FaceEngineCallbacks &callbacks) {
    mImpl->mCallbacks = callbacks;
    LOG(INFO) << "Custom FaceEngine initialized";
    return true;
}

void FaceEngine::release() {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.clear();
}

bool FaceEngine::restoreEnrollments(int userId) {
    if (!mImpl->mCallbacks.loadTemplates) {
        return false;
    }
    auto loaded = mImpl->mCallbacks.loadTemplates(userId);
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.clear();
    for (auto &pair : loaded) {
        mImpl->enrolledEmbeddings[pair.first] = std::move(pair.second);
    }
    return true;
}

int FaceEngine::enroll(int userId, const std::vector<uint8_t> &nv21Frame, int width, int height, int32_t &outFaceId) {
    if (isCancelled()) {
        LOG(INFO) << "Stub enroll: operation cancelled, skipping frame processing";
        return -1;
    }
    // 1. Run quality logic & pose detection (analyze lighting, size, etc.)
    // 2. Extract float embedding vector (e.g., 512 dimensions)
    std::vector<float> embedding(512, 0.0f); // Compute real embedding here
    
    // 3. Generate new face ID
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    int32_t maxId = 0;
    for (const auto &[faceId, _] : mImpl->enrolledEmbeddings) {
        if (faceId > maxId) maxId = faceId;
    }
    outFaceId = maxId + 1;
    
    // 4. Save to secure storage via callback
    if (mImpl->mCallbacks.saveTemplate) {
        if (!mImpl->mCallbacks.saveTemplate(userId, outFaceId, embedding)) {
            LOG(ERROR) << "Failed to save enrollment template";
            return VendorCode::FAILED;
        }
    }
    
    mImpl->enrolledEmbeddings[outFaceId] = std::move(embedding);
    return VendorCode::FACE_OK;
}

int FaceEngine::authenticate(const std::vector<uint8_t> &nv21Frame, int width, int height, int userId, float &outScore, int32_t &outFaceId) {
    if (isCancelled()) {
        LOG(INFO) << "Stub authenticate: operation cancelled, skipping frame processing";
        return -1;
    }
    // 1. Extract live embedding
    std::vector<float> liveEmbedding = { /* ... extracted features ... */ };
    
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    float bestScore = -1.0f;
    int32_t matchedId = -1;
    
    // 2. Compare against cached templates
    for (const auto &[faceId, templateEmbedding] : mImpl->enrolledEmbeddings) {
        float score = 0.0f; // Calculate similarity (e.g. Cosine Similarity)
        for (size_t i = 0; i < liveEmbedding.size(); ++i) {
             score += liveEmbedding[i] * templateEmbedding[i];
        }
        if (score > bestScore) {
             bestScore = score;
             matchedId = faceId;
        }
    }
    
    outScore = bestScore;
    outFaceId = matchedId;
    
    // Threshold comparison
    return (bestScore >= 0.75f) ? 0 : 1;
}

int FaceEngine::analyzeFaceQuality(const std::vector<uint8_t> &nv21, int width, int height) {
    if (isCancelled()) {
        LOG(INFO) << "Stub analyzeFaceQuality: operation cancelled, skipping frame processing";
        return -1;
    }
    return VendorCode::FACE_OK;
}

int FaceEngine::deleteEnrollment(int userId, int faceId) {
    if (mImpl->mCallbacks.deleteTemplate) {
        mImpl->mCallbacks.deleteTemplate(userId, faceId);
    }
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    mImpl->enrolledEmbeddings.erase(faceId);
    return 0;
}

int FaceEngine::getEnrollmentCount(int userId) {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    return mImpl->enrolledEmbeddings.size();
}

std::vector<int32_t> FaceEngine::getEnrolledFaceIds(int userId) {
    std::lock_guard<std::mutex> lock(mImpl->enrollmentMutex);
    std::vector<int32_t> ids;
    for (const auto &[faceId, _] : mImpl->enrolledEmbeddings) {
        ids.push_back(faceId);
    }
    return ids;
}

std::vector<float> FaceEngine::getLastLandmarks() {
    return {};
}

void FaceEngine::resetEnrollment() {
    mEnrollFrameCount = 0;
}

void FaceEngine::cancelAll() {
    LOG(INFO) << "Stub FaceEngine::cancelAll: cancelling and resetting all operations";
    mCancelled = true;
    resetEnrollment();
    mCaptureRequested = false;
}

void FaceEngine::getSensorProps(aidl::android::hardware::biometrics::face::SensorProps &props) {
    // Populate sensor configuration info
}

} // namespace hal
} // namespace face
} // namespace extra
} // namespace org
```

### 3. Integrating with `lib/Android.bp`
Update the target compiler rules to build the custom source file (e.g. by commenting out the prebuilt shared target and using `cc_library_shared` module):
```bp
cc_library_shared {
    name: "libextra_face_engine",
    vendor: true,
    defaults: [
        "extra_facehal_logging_defaults",
    ],
    header_libs: ["libextra_facehal_headers"],
    export_header_lib_headers: ["libextra_facehal_headers"],
    srcs: [
        "FaceEngineCustom.cpp",
    ],
    shared_libs: [
        "android.hardware.biometrics.face-V4-ndk",
        "android.hardware.biometrics.common-V4-ndk",
        "libbase",
        "libcrypto",
        "liblog",
        // Add any libraries your engine requires
    ],
}
```

---

## Custom Enrollment App & Overlay (FaceUnlock)

The repository includes a ready-to-use custom enrollment application (`ExtraVision`) and its runtime resource overlay configuration (`ExtraVisionOverlay`).

* **Enrollment App (`App/`)**: A system-privileged vendor application (`ExtraVision`) that binds to `IVisionService`, receives camera frames from the HAL, decodes them to show on screen, handles progress callbacks from `FaceManager`, and updates a face mesh overlay with calculated landmarks.
* **System Overlay (`overlay/`)**: Overrides the Android Settings configuration to point Settings app face enrollment flow directly to the custom activity (`ExtraVision`).

These components are automatically compiled and included in the vendor image when you include [facehal.mk](./facehal.mk) in your device's `device.mk`.
