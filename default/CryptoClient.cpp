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

#include "CryptoClient.h"

#include <android-base/logging.h>
#include <android/binder_manager.h>
#include <log/log.h>

#include <aidl/android/hardware/security/keymint/Algorithm.h>
#include <aidl/android/hardware/security/keymint/BlockMode.h>
#include <aidl/android/hardware/security/keymint/Digest.h>
#include <aidl/android/hardware/security/keymint/KeyPurpose.h>
#include <aidl/android/hardware/security/keymint/PaddingMode.h>
#include <aidl/android/hardware/security/keymint/SecurityLevel.h>
#include <aidl/android/hardware/security/keymint/Tag.h>
#include <aidl/android/system/keystore2/CreateOperationResponse.h>
#include <aidl/android/system/keystore2/Domain.h>
#include <aidl/android/system/keystore2/KeyMetadata.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace org {
namespace milahaina {
namespace face {
namespace hal {

using aidl::android::hardware::security::keymint::Algorithm;
using aidl::android::hardware::security::keymint::BlockMode;
using aidl::android::hardware::security::keymint::Digest;
using aidl::android::hardware::security::keymint::KeyParameter;
using aidl::android::hardware::security::keymint::KeyParameterValue;
using aidl::android::hardware::security::keymint::KeyPurpose;
using aidl::android::hardware::security::keymint::PaddingMode;
using aidl::android::hardware::security::keymint::SecurityLevel;
using aidl::android::hardware::security::keymint::Tag;
using aidl::android::system::keystore2::CreateOperationResponse;
using aidl::android::system::keystore2::Domain;
using aidl::android::system::keystore2::KeyDescriptor;
using aidl::android::system::keystore2::KeyMetadata;

namespace {

constexpr int64_t kFaceHalKeystoreNamespace = 65000;
constexpr uint8_t kStoragePayloadVersion = 1;
constexpr size_t kMaxNonceSize = 32;

std::string StorageAliasForUser(int32_t userId) {
    return "extra_face_template_aes_gcm_v2_user_" + std::to_string(userId);
}

KeyParameter Purpose(KeyPurpose purpose) {
    return KeyParameter{
            .tag = Tag::PURPOSE,
            .value = KeyParameterValue::make<KeyParameterValue::keyPurpose>(purpose)};
}

KeyParameter Nonce(const std::vector<uint8_t>& nonce) {
    return KeyParameter{
            .tag = Tag::NONCE,
            .value = KeyParameterValue::make<KeyParameterValue::blob>(nonce)};
}

KeyParameter BlockModeParam(BlockMode mode) {
    return KeyParameter{
            .tag = Tag::BLOCK_MODE,
            .value = KeyParameterValue::make<KeyParameterValue::blockMode>(mode)};
}

KeyParameter PaddingParam(PaddingMode padding) {
    return KeyParameter{
            .tag = Tag::PADDING,
            .value = KeyParameterValue::make<KeyParameterValue::paddingMode>(padding)};
}

KeyParameter DigestParam(Digest digest) {
    return KeyParameter{
            .tag = Tag::DIGEST,
            .value = KeyParameterValue::make<KeyParameterValue::digest>(digest)};
}

KeyParameter MacLengthParam(int bits) {
    return KeyParameter{
            .tag = Tag::MAC_LENGTH,
            .value = KeyParameterValue::make<KeyParameterValue::integer>(bits)};
}

std::vector<uint8_t> JoinOperationOutput(
        const std::optional<std::vector<uint8_t>>& updateOutput,
        const std::optional<std::vector<uint8_t>>& finishOutput) {
    std::vector<uint8_t> out;
    if (updateOutput) {
        out.insert(out.end(), updateOutput->begin(), updateOutput->end());
    }
    if (finishOutput) {
        out.insert(out.end(), finishOutput->begin(), finishOutput->end());
    }
    return out;
}

std::optional<std::vector<uint8_t>> GetNonceFromResponse(
        const CreateOperationResponse& response) {
    for (const KeyParameter& param : response.parameters->keyParameter) {
        if (param.tag == Tag::NONCE) {
            return param.value.get<KeyParameterValue::blob>();
        }
    }
    return std::nullopt;
}

std::vector<uint8_t> SerializeStoragePayload(const std::vector<uint8_t>& nonce,
                                             const std::vector<uint8_t>& ciphertext) {
    if (nonce.empty() || nonce.size() > kMaxNonceSize || ciphertext.empty()) {
        return {};
    }
    std::vector<uint8_t> payload;
    payload.reserve(2 + nonce.size() + ciphertext.size());
    payload.push_back(kStoragePayloadVersion);
    payload.push_back(static_cast<uint8_t>(nonce.size()));
    payload.insert(payload.end(), nonce.begin(), nonce.end());
    payload.insert(payload.end(), ciphertext.begin(), ciphertext.end());
    return payload;
}

bool ParseStoragePayload(const std::vector<uint8_t>& payload,
                         std::vector<uint8_t>* nonce,
                         std::vector<uint8_t>* ciphertext) {
    if (payload.size() < 3) {
        LOG(ERROR) << "Encrypted template payload too small: " << payload.size();
        return false;
    }
    if (payload[0] != kStoragePayloadVersion) {
        LOG(ERROR) << "Unsupported encrypted template payload version: "
                   << static_cast<int>(payload[0]);
        return false;
    }

    const size_t nonceLen = payload[1];
    if (nonceLen == 0 || nonceLen > kMaxNonceSize) {
        LOG(ERROR) << "Invalid encrypted template nonce length: " << nonceLen;
        return false;
    }
    if (payload.size() <= 2 + nonceLen) {
        LOG(ERROR) << "Encrypted template payload missing ciphertext";
        return false;
    }

    nonce->assign(payload.begin() + 2, payload.begin() + 2 + nonceLen);
    ciphertext->assign(payload.begin() + 2 + nonceLen, payload.end());
    return true;
}

bool LooksKeyMissing(const std::string& message) {
    return message.find("KEY_NOT_FOUND") != std::string::npos ||
           message.find("Failed to load key blob") != std::string::npos ||
           message.find("does not exist") != std::string::npos;
}

bool LooksKeyAlreadyExists(const std::string& message) {
    return message.find("KEY_ALREADY_EXISTS") != std::string::npos ||
           message.find("already exists") != std::string::npos;
}

}  // namespace

CryptoClient::CryptoClient() {
    mKeystore = IKeystoreService::fromBinder(ndk::SpAIBinder(
            AServiceManager_waitForService("android.system.keystore2.IKeystoreService/default")));

    if (mKeystore) {
        auto status = mKeystore->getSecurityLevel(SecurityLevel::TRUSTED_ENVIRONMENT, &mSecLevel);
        if (!status.isOk()) {
            LOG(ERROR) << "Keystore2 getSecurityLevel(TRUSTED_ENVIRONMENT) failed: " << status.getMessage();
        }
    }

    if (!mKeystore || !mSecLevel) {
        LOG(ERROR) << "Could not initialize Keystore2 / KeyMint services";
    }
}

std::shared_ptr<CryptoClient> CryptoClient::getInstance() {
    static std::shared_ptr<CryptoClient> instance(new CryptoClient());
    return instance;
}

uint64_t CryptoClient::generateChallenge() {
    uint64_t challenge = 0;
    if (RAND_bytes(reinterpret_cast<uint8_t*>(&challenge), sizeof(challenge)) != 1) {
        LOG(ERROR) << "Secure RNG failed while generating challenge";
        return 0;
    }
    return challenge;
}

KeyDescriptor CryptoClient::getStorageKeyDescriptor(int32_t userId) const {
    return {
            .domain = Domain::SELINUX,
            .nspace = kFaceHalKeystoreNamespace,
            .alias = StorageAliasForUser(userId),
            .blob = {},
    };
}

KeyDescriptor CryptoClient::getMacKeyDescriptor() const {
    return {
            .domain = Domain::SELINUX,
            .nspace = kFaceHalKeystoreNamespace,
            .alias = "extra_face_template_hmac_sha256_v2",
            .blob = {},
    };
}

bool CryptoClient::generateStorageKey(int32_t userId) {
    if (!mSecLevel) {
        LOG(ERROR) << "Cannot generate storage key: KeyMint security level unavailable";
        return false;
    }

    std::vector<KeyParameter> params = {
            KeyParameter{.tag = Tag::ALGORITHM,
                         .value = KeyParameterValue::make<KeyParameterValue::algorithm>(
                                 Algorithm::AES)},
            KeyParameter{.tag = Tag::KEY_SIZE,
                         .value = KeyParameterValue::make<KeyParameterValue::integer>(256)},
            Purpose(KeyPurpose::ENCRYPT),
            Purpose(KeyPurpose::DECRYPT),
            BlockModeParam(BlockMode::GCM),
            PaddingParam(PaddingMode::NONE),
            KeyParameter{.tag = Tag::MIN_MAC_LENGTH,
                         .value = KeyParameterValue::make<KeyParameterValue::integer>(128)},
            KeyParameter{.tag = Tag::NO_AUTH_REQUIRED,
                         .value = KeyParameterValue::make<KeyParameterValue::boolValue>(true)},
    };

    KeyMetadata metadata;
    auto status = mSecLevel->generateKey(getStorageKeyDescriptor(userId), std::nullopt,
                                         params, 0, {}, &metadata);
    if (!status.isOk()) {
        if (LooksKeyAlreadyExists(status.getMessage())) {
            LOG(INFO) << "Template storage key already exists for user " << userId;
            return true;
        }
        LOG(ERROR) << "Storage key generation failed for user " << userId << ": "
                   << status.getMessage();
        return false;
    }

    LOG(INFO) << "Generated hardware-backed template storage key for user " << userId;
    return true;
}

bool CryptoClient::generateMacKey() {
    if (!mSecLevel) {
        LOG(ERROR) << "Cannot generate MAC key: KeyMint security level unavailable";
        return false;
    }

    std::vector<KeyParameter> params = {
            KeyParameter{.tag = Tag::ALGORITHM,
                         .value = KeyParameterValue::make<KeyParameterValue::algorithm>(
                                 Algorithm::HMAC)},
            KeyParameter{.tag = Tag::DIGEST,
                         .value = KeyParameterValue::make<KeyParameterValue::digest>(
                                 Digest::SHA_2_256)},
            KeyParameter{.tag = Tag::KEY_SIZE,
                         .value = KeyParameterValue::make<KeyParameterValue::integer>(256)},
            Purpose(KeyPurpose::SIGN),
            Purpose(KeyPurpose::VERIFY),
            KeyParameter{.tag = Tag::MIN_MAC_LENGTH,
                         .value = KeyParameterValue::make<KeyParameterValue::integer>(256)},
            KeyParameter{.tag = Tag::NO_AUTH_REQUIRED,
                         .value = KeyParameterValue::make<KeyParameterValue::boolValue>(true)},
    };

    KeyMetadata metadata;
    auto status = mSecLevel->generateKey(getMacKeyDescriptor(), std::nullopt, params,
                                         0, {}, &metadata);
    if (!status.isOk()) {
        if (LooksKeyAlreadyExists(status.getMessage())) {
            LOG(INFO) << "Template MAC key already exists";
            return true;
        }
        LOG(ERROR) << "Template MAC key generation failed: " << status.getMessage();
        return false;
    }

    LOG(INFO) << "Generated hardware-backed template MAC key";
    return true;
}

std::vector<uint8_t> CryptoClient::encrypt(int32_t userId, const std::vector<uint8_t>& data) {
    if (!mSecLevel) {
        LOG(ERROR) << "Cannot encrypt template: KeyMint security level unavailable";
        return {};
    }
    if (data.empty()) {
        LOG(ERROR) << "Refusing to encrypt empty template payload";
        return {};
    }

    std::vector<KeyParameter> opParams = {
            Purpose(KeyPurpose::ENCRYPT),
            BlockModeParam(BlockMode::GCM),
            PaddingParam(PaddingMode::NONE),
            MacLengthParam(128),
    };
    CreateOperationResponse result;
    auto status = mSecLevel->createOperation(getStorageKeyDescriptor(userId), opParams,
                                             false, &result);
    if (!status.isOk() || !result.iOperation) {
        LOG(WARNING) << "Keystore createOperation(ENCRYPT) failed for user " << userId
                     << ": " << status.getMessage();
        if (!LooksKeyMissing(status.getMessage()) || !generateStorageKey(userId)) {
            return {};
        }
        status = mSecLevel->createOperation(getStorageKeyDescriptor(userId), opParams,
                                            false, &result);
        if (!status.isOk() || !result.iOperation) {
            LOG(ERROR) << "Keystore createOperation(ENCRYPT) failed after key generation: "
                       << status.getMessage();
            return {};
        }
    }

    const std::optional<std::vector<uint8_t>> nonce = GetNonceFromResponse(result);
    if (!nonce || nonce->empty()) {
        LOG(ERROR) << "KeyMint did not return AES-GCM nonce";
        result.iOperation->abort();
        return {};
    }

    std::vector<uint8_t> ciphertext;
    size_t offset = 0;
    constexpr size_t kChunkSize = 4096;
    while (offset < data.size()) {
        size_t chunk = std::min(data.size() - offset, kChunkSize);
        std::vector<uint8_t> chunkData(data.begin() + offset, data.begin() + offset + chunk);
        std::optional<std::vector<uint8_t>> updateOutput;
        status = result.iOperation->update(chunkData, &updateOutput);
        if (!status.isOk()) {
            LOG(ERROR) << "AES-GCM encrypt update failed at offset " << offset << ": " << status.getMessage();
            result.iOperation->abort();
            return {};
        }
        if (updateOutput) {
            ciphertext.insert(ciphertext.end(), updateOutput->begin(), updateOutput->end());
        }
        offset += chunk;
    }

    std::optional<std::vector<uint8_t>> finishOutput;
    status = result.iOperation->finish(std::nullopt, std::nullopt, &finishOutput);
    if (!status.isOk()) {
        LOG(ERROR) << "AES-GCM encrypt finish failed: " << status.getMessage();
        result.iOperation->abort();
        return {};
    }

    if (finishOutput) {
        ciphertext.insert(ciphertext.end(), finishOutput->begin(), finishOutput->end());
    }

    std::vector<uint8_t> payload = SerializeStoragePayload(*nonce, ciphertext);
    if (payload.empty()) {
        LOG(ERROR) << "Failed to serialize encrypted template payload";
    }
    return payload;
}

std::vector<uint8_t> CryptoClient::decrypt(int32_t userId,
                                        const std::vector<uint8_t>& encryptedPayload) {
    if (!mSecLevel) {
        LOG(ERROR) << "Cannot decrypt template: KeyMint security level unavailable";
        return {};
    }

    std::vector<uint8_t> nonce;
    std::vector<uint8_t> ciphertext;
    if (!ParseStoragePayload(encryptedPayload, &nonce, &ciphertext)) {
        return {};
    }

    std::vector<KeyParameter> opParams = {
            Purpose(KeyPurpose::DECRYPT),
            BlockModeParam(BlockMode::GCM),
            PaddingParam(PaddingMode::NONE),
            MacLengthParam(128),
            Nonce(nonce),
    };
    CreateOperationResponse result;
    auto status = mSecLevel->createOperation(getStorageKeyDescriptor(userId), opParams,
                                             false, &result);
    if (!status.isOk() || !result.iOperation) {
        LOG(ERROR) << "Keystore createOperation(DECRYPT) failed for user " << userId
                   << ": " << status.getMessage();
        return {};
    }

    std::vector<uint8_t> decrypted;
    size_t offset = 0;
    constexpr size_t kChunkSize = 4096;
    while (offset < ciphertext.size()) {
        size_t chunk = std::min(ciphertext.size() - offset, kChunkSize);
        std::vector<uint8_t> chunkData(ciphertext.begin() + offset, ciphertext.begin() + offset + chunk);
        std::optional<std::vector<uint8_t>> updateOutput;
        status = result.iOperation->update(chunkData, &updateOutput);
        if (!status.isOk()) {
            LOG(ERROR) << "AES-GCM decrypt update failed at offset " << offset << ": " << status.getMessage();
            result.iOperation->abort();
            return {};
        }
        if (updateOutput) {
            decrypted.insert(decrypted.end(), updateOutput->begin(), updateOutput->end());
        }
        offset += chunk;
    }

    std::optional<std::vector<uint8_t>> finishOutput;
    status = result.iOperation->finish(std::nullopt, std::nullopt, &finishOutput);
    if (!status.isOk()) {
        LOG(ERROR) << "AES-GCM decrypt finish failed: " << status.getMessage();
        result.iOperation->abort();
        return {};
    }

    if (finishOutput) {
        decrypted.insert(decrypted.end(), finishOutput->begin(), finishOutput->end());
    }

    return decrypted;
}

std::vector<uint8_t> CryptoClient::generateMac(const std::vector<uint8_t>& data) {
    if (data.empty()) {
        LOG(ERROR) << "Refusing to MAC empty template payload";
        return {};
    }
    return signWithHmacKey(data);
}

bool CryptoClient::verifyMac(const std::vector<uint8_t>& data, const std::vector<uint8_t>& mac) {
    if (mac.empty()) {
        LOG(ERROR) << "Template MAC is empty";
        return false;
    }

    std::vector<uint8_t> calculatedMac = signWithHmacKey(data);
    if (calculatedMac.empty()) {
        LOG(ERROR) << "Failed to calculate template MAC";
        return false;
    }
    if (mac.size() != calculatedMac.size()) {
        LOG(ERROR) << "Template MAC size mismatch: stored=" << mac.size()
                   << " calculated=" << calculatedMac.size();
        return false;
    }
    return CRYPTO_memcmp(mac.data(), calculatedMac.data(), mac.size()) == 0;
}

std::vector<uint8_t> CryptoClient::signWithHmacKey(const std::vector<uint8_t>& data) {
    if (!mSecLevel) {
        LOG(ERROR) << "Cannot sign template MAC: KeyMint security level unavailable";
        return {};
    }

    std::vector<KeyParameter> opParams = {
            Purpose(KeyPurpose::SIGN),
            DigestParam(Digest::SHA_2_256),
            MacLengthParam(256),
    };
    CreateOperationResponse result;
    auto status = mSecLevel->createOperation(getMacKeyDescriptor(), opParams, false, &result);
    if (!status.isOk() || !result.iOperation) {
        LOG(WARNING) << "Keystore createOperation(SIGN) failed for template MAC: "
                     << status.getMessage();
        if (!LooksKeyMissing(status.getMessage()) || !generateMacKey()) {
            return {};
        }
        status = mSecLevel->createOperation(getMacKeyDescriptor(), opParams, false, &result);
        if (!status.isOk() || !result.iOperation) {
            LOG(ERROR) << "Keystore createOperation(SIGN) failed after MAC key generation: "
                       << status.getMessage();
            return {};
        }
    }

    std::vector<uint8_t> mac_accum;
    size_t offset = 0;
    constexpr size_t kChunkSize = 4096;
    while (offset < data.size()) {
        size_t chunk = std::min(data.size() - offset, kChunkSize);
        std::vector<uint8_t> chunkData(data.begin() + offset, data.begin() + offset + chunk);
        std::optional<std::vector<uint8_t>> updateOutput;
        status = result.iOperation->update(chunkData, &updateOutput);
        if (!status.isOk()) {
            LOG(ERROR) << "HMAC update failed at offset " << offset << ": " << status.getMessage();
            result.iOperation->abort();
            return {};
        }
        if (updateOutput) {
            mac_accum.insert(mac_accum.end(), updateOutput->begin(), updateOutput->end());
        }
        offset += chunk;
    }

    std::optional<std::vector<uint8_t>> finishOutput;
    status = result.iOperation->finish(std::nullopt, std::nullopt, &finishOutput);
    if (!status.isOk()) {
        LOG(ERROR) << "HMAC finish failed: " << status.getMessage();
        result.iOperation->abort();
        return {};
    }

    if (finishOutput) {
        mac_accum.insert(mac_accum.end(), finishOutput->begin(), finishOutput->end());
    }

    if (mac_accum.empty()) {
        LOG(ERROR) << "HMAC operation returned empty MAC";
    }
    return mac_accum;
}

}  // namespace hal
}  // namespace face
}  // namespace milahaina
}  // namespace org
