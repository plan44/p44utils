#
# Component Makefile for using p44utils on ESP32 IDF platform
#

#COMPONENT_SRCDIRS := . slre sqlite3pp

COMPONENT_OBJS:= \
  application.o \
  p44obj.o \
  logger.o \
  utils.o \
  error.o \
  mainloop.o \
  $(call compile_only_if,$(CONFIG_P44UTILS_ENABLE_LEDCHAIN),ledchaincomm.o)

# %%%ugly hack for now%%% just take a boost checkout we already have
COMPONENT_ADD_INCLUDEDIRS = \
  . \
  /Volumes/CaseSens/openwrt/build_dir/target-mipsel_24kc_musl/boost_1_67_0

CPPFLAGS += \
  -Wno-reorder \
  -isystem \
  /Volumes/CaseSens/openwrt/build_dir/target-mipsel_24kc_musl/boost_1_67_0
