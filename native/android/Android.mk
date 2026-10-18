LOCAL_PATH := $(call my-dir)

ifeq ($(strip $(SONORA_CONTROL_FILE)),)
  $(error SONORA_CONTROL_FILE must be chosen after reviewing the target device's SELinux policy)
endif

include $(CLEAR_VARS)
LOCAL_MODULE := sonora_spatial
LOCAL_SRC_FILES := src/sonora_effect.cpp ../dsp/src/sonora_dsp.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/include $(LOCAL_PATH)/../dsp/include
LOCAL_CPPFLAGS := -std=c++17 -O3 -fvisibility=hidden -fno-exceptions -fno-rtti -DSONORA_CONTROL_FILE=\"$(SONORA_CONTROL_FILE)\"
LOCAL_LDFLAGS := -Wl,--no-undefined -Wl,-z,relro -Wl,-z,now
include $(BUILD_SHARED_LIBRARY)

include $(CLEAR_VARS)
LOCAL_MODULE := sonora-ctl
LOCAL_SRC_FILES := src/sonora_ctl.cpp
LOCAL_C_INCLUDES := $(LOCAL_PATH)/../dsp/include
LOCAL_CPPFLAGS := -std=c++17 -O2 -fno-exceptions -fno-rtti -DSONORA_CONTROL_FILE=\"$(SONORA_CONTROL_FILE)\"
LOCAL_LDFLAGS := -Wl,-z,relro -Wl,-z,now
include $(BUILD_EXECUTABLE)
