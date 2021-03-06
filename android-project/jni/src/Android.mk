LOCAL_PATH := $(call my-dir)
include $(CLEAR_VARS)

NAME := ufo

HAVE_SDL2_MIXER_SDL_MIXER_H := 1
HAVE_LIBPNG_PNG_H :=
HAVE_JPEGLIB_H :=
HAVE_MXML_MXML_H :=

ROOT_PROJECT=..
JNI_LIBS=$(LOCAL_PATH)/../libs
ROOT_JNI_SRC=../../$(ROOT_PROJECT)
SRC_PATH_PROJECT=$(ROOT_PROJECT)/src
SRC_PATH_JNI_SRC=$(ROOT_JNI_SRC)/src
LIBS_PATH_PROJECT=$(SRC_PATH_PROJECT)/libs
LIBS_PATH_JNI_SRC=$(SRC_PATH_JNI_SRC)/libs
REL_SRC_PATH_JNI_SRC=$(LOCAL_PATH)/$(SRC_PATH_JNI_SRC)
REL_LIBS_PATH_JNI_SRC=$(LOCAL_PATH)/$(LIBS_PATH_JNI_SRC)

SRC = $(SRC_PATH_PROJECT)
include ../build/default.mk
include ../build/modules/$(NAME).mk

LOCAL_MODULE      := main

android_$(NAME)_SRCS := $(addprefix $(SRC_PATH_JNI_SRC)/,$($(NAME)_SRCS))

LOCAL_SRC_FILES   := $(LIBS_PATH_JNI_SRC)/SDL/src/main/android/SDL_android_main.c \
	$(android_$(NAME)_SRCS)

LOCAL_CFLAGS += $($(NAME)_CFLAGS) -fexceptions

LOCAL_C_INCLUDES :=
LOCAL_C_INCLUDES += $(SRC_PATH_PROJECT)
LOCAL_C_INCLUDES += $(LIBS_PATH_PROJECT)
LOCAL_C_INCLUDES += $(JNI_LIBS)/curl/include
LOCAL_C_INCLUDES += $(JNI_LIBS)/intl/include
LOCAL_C_INCLUDES += $(JNI_LIBS)/theora/include/theora
LOCAL_C_INCLUDES += $(JNI_LIBS)/vorbis/include
LOCAL_C_INCLUDES += $(JNI_LIBS)/ogg/include
LOCAL_C_INCLUDES += $(JNI_LIBS)/jpeg
LOCAL_C_INCLUDES += $(JNI_LIBS)/libpng
LOCAL_C_INCLUDES += $(JNI_LIBS)/zlib

LOCAL_STATIC_LIBRARIES := libpng jpeg zlib curl vorbis intl theora

LOCAL_SHARED_LIBRARIES := SDL2 SDL2_mixer SDL_ttf
LOCAL_LDLIBS           := -lGLESv2 -llog -lz -lm

include $(BUILD_SHARED_LIBRARY)
