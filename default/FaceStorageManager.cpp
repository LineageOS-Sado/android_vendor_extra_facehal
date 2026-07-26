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

#define LOG_TAG "ExtraFaceHal"
#include <log/log.h>
#include "FaceStorageManager.h"
#include <android-base/logging.h>
#include <android-base/file.h>
#include <filesystem>
#include <fstream>
#include <unordered_set>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

namespace fs = std::filesystem;

static const std::string BASE_DIR = "/data/vendor/biometrics/face/";

FaceStorageManager::FaceStorageManager() 
    : mCryptoClient(CryptoClient::getInstance()) {
    initialize();
}

FaceStorageManager::~FaceStorageManager() {}

bool FaceStorageManager::initialize() {
    std::error_code ec;
    if (!fs::exists(BASE_DIR, ec)) {
        fs::create_directories(BASE_DIR, ec);
        chmod(BASE_DIR.c_str(), 0770);
    }
    return true;
}

std::string FaceStorageManager::getUserDirectory(int32_t userId) {
    std::string dir = BASE_DIR + "user_" + std::to_string(userId) + "/";
    std::error_code ec;
    if (!fs::exists(dir, ec)) {
        fs::create_directories(dir, ec);
        chmod(dir.c_str(), 0770);
    }
    return dir;
}

std::string FaceStorageManager::getFilePath(int32_t userId, int32_t faceId) {
    return getUserDirectory(userId) + "face_" + std::to_string(faceId) + ".dat";
}

std::string FaceStorageManager::getPlaintextFilePath(int32_t userId, int32_t faceId) {
    return getUserDirectory(userId) + "face_" + std::to_string(faceId) + "_without_encryption.dat";
}

std::vector<uint8_t> FaceStorageManager::readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in.is_open()) return {};

    size_t size = in.tellg();
    in.seekg(0, std::ios::beg);

    std::vector<uint8_t> buffer(size);
    if (!in.read(reinterpret_cast<char*>(buffer.data()), size)) return {};
    in.close();

    return buffer;
}

bool FaceStorageManager::saveTemplate(const FaceTemplate& tmpl) {
    LOG(INFO) << "Saving template for user " << tmpl.userId << ", face " << tmpl.faceId;

    std::vector<uint8_t> serializedData = FaceTemplateSerializer::serialize(tmpl);
    if (serializedData.empty()) {
        LOG(ERROR) << "Failed to serialize template";
        return false;
    }

    std::string finalPath = getFilePath(tmpl.userId, tmpl.faceId);
    std::string stalePath = getPlaintextFilePath(tmpl.userId, tmpl.faceId);
    std::vector<uint8_t> dataToWrite = mCryptoClient->encrypt(tmpl.userId, serializedData);
    if (dataToWrite.empty()) {
        LOG(ERROR) << "Encryption failed; saving template without encryption at marked path";
        finalPath = stalePath;
        stalePath = getFilePath(tmpl.userId, tmpl.faceId);
        dataToWrite = std::move(serializedData);
    }

    std::string tempPath = finalPath + ".tmp";

    // Atomic write
    std::ofstream out(tempPath, std::ios::binary);
    if (!out.is_open()) {
        LOG(ERROR) << "Failed to open temporary file " << tempPath;
        return false;
    }

    out.write(reinterpret_cast<const char*>(dataToWrite.data()), dataToWrite.size());
    out.close();

    chmod(tempPath.c_str(), 0600);

    if (rename(tempPath.c_str(), finalPath.c_str()) != 0) {
        LOG(ERROR) << "Failed to atomically rename template file";
        unlink(tempPath.c_str());
        return false;
    }

    std::error_code ec;
    if (fs::exists(stalePath, ec)) {
        fs::remove(stalePath, ec);
    }

    return true;
}

bool FaceStorageManager::loadTemplate(int32_t userId, int32_t faceId, FaceTemplate& outTmpl) {
    auto deserializeAndValidate = [&](const std::vector<uint8_t>& data) {
        if (!FaceTemplateSerializer::deserialize(data, outTmpl)) {
            LOG(ERROR) << "Deserialization or checksum validation failed for face " << faceId;
            return false;
        }

        if (outTmpl.userId != userId || outTmpl.faceId != faceId) {
            LOG(ERROR) << "Cross-user or cross-face ID mismatch! Stored: U" << outTmpl.userId
                       << " F" << outTmpl.faceId << " Requested: U" << userId << " F" << faceId;
            return false;
        }
        return true;
    };

    std::vector<uint8_t> encryptedData = readFile(getFilePath(userId, faceId));
    if (!encryptedData.empty()) {
        std::vector<uint8_t> decryptedData = mCryptoClient->decrypt(userId, encryptedData);
        if (!decryptedData.empty() && deserializeAndValidate(decryptedData)) {
            return true;
        }
        LOG(ERROR) << "Encrypted template failed for face " << faceId
                   << "; checking plaintext fallback";
    }

    std::string plaintextPath = getPlaintextFilePath(userId, faceId);
    std::vector<uint8_t> plaintextData = readFile(plaintextPath);
    if (plaintextData.empty()) {
        return false; // File doesn't exist or is empty
    }

    LOG(WARNING) << "Loading face template without encryption: " << plaintextPath;
    if (!deserializeAndValidate(plaintextData)) {
        return false;
    }

    return true;
}

std::vector<FaceTemplate> FaceStorageManager::loadAllTemplatesForUser(int32_t userId) {
    std::vector<FaceTemplate> templates;
    std::unordered_set<int32_t> loadedFaceIds;
    std::string dir = getUserDirectory(userId);
    std::error_code ec;

    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        std::string filename = entry.path().filename().string();

        // Parse "face_<id>.dat" and explicit plaintext fallback
        // "face_<id>_without_encryption.dat".
        if (filename.find("face_") == 0 && filename.find(".dat") != std::string::npos) {
            int faceId = -1;
            if (filename.find("_without_encryption.dat") != std::string::npos) {
                sscanf(filename.c_str(), "face_%d_without_encryption.dat", &faceId);
            } else {
                sscanf(filename.c_str(), "face_%d.dat", &faceId);
            }
            if (faceId != -1) {
                if (loadedFaceIds.find(faceId) != loadedFaceIds.end()) {
                    continue;
                }
                FaceTemplate tmpl;
                if (loadTemplate(userId, faceId, tmpl)) {
                    templates.push_back(tmpl);
                    loadedFaceIds.insert(faceId);
                } else {
                    LOG(WARNING) << "Auto-cleaning corrupted/invalid template: " << filename;
                    fs::remove(entry.path(), ec);
                }
            }
        }
    }

    return templates;
}

bool FaceStorageManager::deleteTemplate(int32_t userId, int32_t faceId) {
    LOG(INFO) << "Deleting template face " << faceId << " for user " << userId;
    std::string path = getFilePath(userId, faceId);
    std::string plaintextPath = getPlaintextFilePath(userId, faceId);
    std::error_code ec;

    if (fs::exists(path, ec)) {
        fs::remove(path, ec);
    }
    if (fs::exists(plaintextPath, ec)) {
        fs::remove(plaintextPath, ec);
    }

    // Check if directory is now empty (except for the key file)
    std::string dir = getUserDirectory(userId);
    bool hasFaces = false;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (entry.path().filename().string().find("face_") == 0 && 
            entry.path().filename().string().find(".dat") != std::string::npos) {
            hasFaces = true;
            break;
        }
    }

    if (!hasFaces) {
        LOG(INFO) << "No templates remaining. Wiping user directory.";
        removeUser(userId);
    }

    return true;
}

bool FaceStorageManager::removeUser(int32_t userId) {
    LOG(INFO) << "Wiping all face data for user " << userId;

    std::string dir = getUserDirectory(userId);
    std::error_code ec;
    if (fs::exists(dir, ec)) {
        fs::remove_all(dir, ec);
    }
    return true;
}

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
