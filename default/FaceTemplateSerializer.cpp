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
#include "FaceTemplateSerializer.h"
#include "CryptoClient.h"
#include <cstring>
#include <android-base/logging.h>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

std::vector<uint8_t> FaceTemplateSerializer::serialize(const FaceTemplate& tmpl) {
    uint32_t templateSize = static_cast<uint32_t>(tmpl.templateData.size());
    size_t totalSize = sizeof(uint32_t) + sizeof(int32_t) + sizeof(int32_t) + sizeof(uint32_t) + templateSize + sizeof(uint32_t);
    
    std::vector<uint8_t> buffer(totalSize);
    size_t offset = 0;

    // Write version
    std::memcpy(buffer.data() + offset, &tmpl.version, sizeof(tmpl.version));
    offset += sizeof(tmpl.version);

    // Write faceId
    std::memcpy(buffer.data() + offset, &tmpl.faceId, sizeof(tmpl.faceId));
    offset += sizeof(tmpl.faceId);

    // Write userId
    std::memcpy(buffer.data() + offset, &tmpl.userId, sizeof(tmpl.userId));
    offset += sizeof(tmpl.userId);

    // Write templateSize
    std::memcpy(buffer.data() + offset, &templateSize, sizeof(templateSize));
    offset += sizeof(templateSize);

    // Write templateData
    if (templateSize > 0) {
        std::memcpy(buffer.data() + offset, tmpl.templateData.data(), templateSize);
        offset += templateSize;
    }

    // Calculate and write checksum
    std::vector<uint8_t> dataToChecksum(buffer.begin(), buffer.begin() + offset);
    uint32_t checksum = calculateChecksum(dataToChecksum);
    std::memcpy(buffer.data() + offset, &checksum, sizeof(checksum));
    offset += sizeof(checksum);

    // Calculate and append hardware-backed MAC
    buffer.resize(offset);
    auto crypto = CryptoClient::getInstance();
    std::vector<uint8_t> mac = crypto->generateMac(buffer);
    if (mac.empty()) {
        LOG(ERROR) << "Failed to generate FaceTemplate MAC";
        return {};
    }
    buffer.insert(buffer.end(), mac.begin(), mac.end());

    return buffer;
}

bool FaceTemplateSerializer::deserialize(const std::vector<uint8_t>& data, FaceTemplate& outTmpl) {
    size_t macSize = 32; // Assuming 256-bit MAC
    size_t minSize = sizeof(uint32_t) + sizeof(int32_t) + sizeof(int32_t) + sizeof(uint32_t) + sizeof(uint32_t) + macSize;
    
    if (data.size() < minSize) {
        LOG(ERROR) << "FaceTemplate data too small: " << data.size();
        return false;
    }

    // Verify hardware-backed MAC first
    std::vector<uint8_t> actualData(data.begin(), data.end() - macSize);
    std::vector<uint8_t> storedMac(data.end() - macSize, data.end());
    
    auto crypto = CryptoClient::getInstance();
    if (!crypto->verifyMac(actualData, storedMac)) {
        LOG(ERROR) << "FaceTemplate MAC verification failed! Potential tampering detected.";
        return false;
    }
    
    outTmpl.mac = storedMac;
    size_t offset = 0;

    // Read version
    std::memcpy(&outTmpl.version, data.data() + offset, sizeof(outTmpl.version));
    offset += sizeof(outTmpl.version);

    if (outTmpl.version != CURRENT_VERSION) {
        LOG(ERROR) << "Unsupported FaceTemplate version: " << outTmpl.version;
        return false;
    }

    // Read faceId
    std::memcpy(&outTmpl.faceId, data.data() + offset, sizeof(outTmpl.faceId));
    offset += sizeof(outTmpl.faceId);

    // Read userId
    std::memcpy(&outTmpl.userId, data.data() + offset, sizeof(outTmpl.userId));
    offset += sizeof(outTmpl.userId);

    // Read templateSize
    uint32_t templateSize = 0;
    std::memcpy(&templateSize, data.data() + offset, sizeof(templateSize));
    offset += sizeof(templateSize);

    if (data.size() != minSize + templateSize) {
        LOG(ERROR) << "FaceTemplate size mismatch. Expected " << (minSize + templateSize) << ", got " << data.size();
        return false;
    }

    // Read templateData
    outTmpl.templateData.resize(templateSize);
    if (templateSize > 0) {
        std::memcpy(outTmpl.templateData.data(), data.data() + offset, templateSize);
        offset += templateSize;
    }

    // Read checksum
    uint32_t storedChecksum = 0;
    std::memcpy(&storedChecksum, data.data() + offset, sizeof(storedChecksum));

    // Verify checksum
    std::vector<uint8_t> dataToChecksum(data.begin(), data.begin() + offset);
    uint32_t calculatedChecksum = calculateChecksum(dataToChecksum);

    if (storedChecksum != calculatedChecksum) {
        LOG(ERROR) << "FaceTemplate checksum validation failed!";
        return false;
    }

    outTmpl.checksum = storedChecksum;
    return true;
}

uint32_t FaceTemplateSerializer::calculateChecksum(const std::vector<uint8_t>& data) {
    // Simple FNV-1a hash for checksum
    uint32_t hash = 2166136261u;
    for (uint8_t byte : data) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
