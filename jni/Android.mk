# Copyright (C) 2010 The Android Open Source Project
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
LOCAL_PATH := $(call my-dir)

include $(CLEAR_VARS)

LOCAL_MODULE    := sayhi
LOCAL_SRC_FILES := SayHi.c
LOCAL_LDFLAGS	:= $(LOCAL_LDFLAGS) -L$(LOCAL_PATH)/$(TARGET_ARCH_ABI)/lib 
LOCAL_C_INCLUDES :=  $(LOCAL_PATH)/$(TARGET_ARCH_ABI)/include
# for native audio
LOCAL_LDLIBS    += -lOpenSLES
# for logging
LOCAL_LDLIBS    += -llog
# for native asset manager
LOCAL_LDLIBS    += -landroid
# for Speex
LOCAL_LDLIBS    += -lspeex -lspeexdsp
# for rtmpdump
LOCAL_LDLIBS    += -lrtmp
include $(BUILD_SHARED_LIBRARY)
