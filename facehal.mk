#
# Copyright (C) 2026 The Project MiLahaina
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

EXTRA_FACEHAL_PATH := vendor/extra/facehal

# Face HAL Service, Enrollment App, and Overlays
PRODUCT_PACKAGES += \
    android.hardware.biometrics.face-service.extra \
    ExtraVision \
    ExtraVisionOverlay

# Face HAL SELinux Policies
BOARD_VENDOR_SEPOLICY_DIRS += \
    $(EXTRA_FACEHAL_PATH)/sepolicy

# Enable Logging
EXTRA_FACEHAL_ENABLE_LOGGING ?= false
$(call add_soong_config_namespace,extra_facehal)
$(call add_soong_config_var_value,extra_facehal,enable_logging,$(EXTRA_FACEHAL_ENABLE_LOGGING))

# Face Engine Model Type (extra or megvii)
EXTRA_FACEHAL_ENGINE_MODEL ?= extra

ifeq ($(EXTRA_FACEHAL_ENGINE_MODEL),megvii)
PRODUCT_SOONG_NAMESPACES += $(EXTRA_FACEHAL_PATH)/lib/megvii
else
PRODUCT_SOONG_NAMESPACES += $(EXTRA_FACEHAL_PATH)/lib/extra
endif
