#
# Component Makefile for using p44utils on ESP32 IDF platform
#

LEDCHAIN_TPDIR = thirdparty/esp32_ws281x
LEDCHAIN_OBJS = \
  ledchaincomm.o \
  $(LEDCHAIN_TPDIR)/esp32_ws281x.o

JSON_TPDIR = thirdparty/json-c
JSON_OBJS = \
  jsonobject.o \
  jsoncomm.o \
  jsonrpccomm.o \
  $(JSON_TPDIR)/arraylist.o \
  $(JSON_TPDIR)/debug.o \
  $(JSON_TPDIR)/json_c_version.o \
  $(JSON_TPDIR)/json_object.o \
  $(JSON_TPDIR)/json_object_iterator.o \
  $(JSON_TPDIR)/json_tokener.o \
  $(JSON_TPDIR)/json_util.o \
  $(JSON_TPDIR)/linkhash.o \
  $(JSON_TPDIR)/printbuf.o \
  $(JSON_TPDIR)/random_seed.o


COMPONENT_SRCDIRS := \
  . \
  $(LEDCHAIN_TPDIR)/esp32_ws281x \
  $(JSON_TPDIR)


COMPONENT_OBJS:= \
  application.o \
  p44obj.o \
  logger.o \
  utils.o \
  error.o \
  mainloop.o \
  fdcomm.o \
  socketcomm.o \
  $(call compile_only_if,$(CONFIG_P44UTILS_ENABLE_LEDCHAIN),$(LEDCHAIN_OBJS)) \
  $(call compile_only_if,$(CONFIG_P44UTILS_ENABLE_JSON),$(JSON_OBJS))


COMPONENT_ADD_INCLUDEDIRS = \
  . \
  thirdparty \
  $(LEDCHAIN_TPDIR) \
  $(JSON_TPDIR)

CPPFLAGS += \
  -Wno-reorder \
  -isystem \
  /Volumes/CaseSens/openwrt/build_dir/target-mipsel_24kc_musl/boost_1_67_0

# %%%ugly hack above ^^^^^^ for now%%% just take a boost checkout we already have
