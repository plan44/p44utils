set(srcs
  "p44utils/application.cpp"
  "p44utils/p44obj.cpp"
  "p44utils/logger.cpp"
  "p44utils/utils.cpp"
  "p44utils/extutils.cpp"
  "p44utils/timeutils.cpp"
  "p44utils/colorutils.cpp"
  "p44utils/error.cpp"
  "p44utils/mainloop.cpp"
  "p44utils/fdcomm.cpp"
  "p44utils/socketcomm.cpp"
  "p44utils/valueanimator.cpp"
  "p44utils/analogio.cpp"
  "p44utils/digitalio.cpp"
  "p44utils/dcmotor.cpp"
  "p44utils/iopin.cpp"
  "p44utils/gpio.cpp"
  "p44utils/pwm.cpp")

set(incl
  ".")

if(CONFIG_P44UTILS_ENABLE_LEDCHAIN)
    list(APPEND srcs
      "p44utils/p44script.cpp")
endif()

if(CONFIG_P44UTILS_ENABLE_LEDCHAIN)
    list(APPEND srcs
      "p44utils/ledchaincomm.cpp"
      "p44utils/thirdparty/esp32_ws281x/esp32_ws281x.c")
    list(APPEND incl
      "p44utils/thirdparty/esp32_ws281x")
endif()

if(CONFIG_P44UTILS_ENABLE_JSON)
    list(APPEND srcs
      "p44utils/jsonobject.cpp"
      "p44utils/jsonobject.cpp"
      "p44utils/jsoncomm.cpp"
      "p44utils/jsonrpccomm.cpp"
      "p44utils/thirdparty/json-c/arraylist.c"
      "p44utils/thirdparty/json-c/debug.c"
      "p44utils/thirdparty/json-c/json_c_version.c"
      "p44utils/thirdparty/json-c/json_object.c"
      "p44utils/thirdparty/json-c/json_object_iterator.c"
      "p44utils/thirdparty/json-c/json_tokener.c"
      "p44utils/thirdparty/json-c/json_util.c"
      "p44utils/thirdparty/json-c/linkhash.c"
      "p44utils/thirdparty/json-c/printbuf.c"
      "p44utils/thirdparty/json-c/random_seed.c")
    list(APPEND incl
      "p44utils/thirdparty/json-c")
endif()

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${incl}")

target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-reorder
  -isystem /Volumes/CaseSens/openwrt/build_dir/target-mipsel_24kc_musl/boost_1_67_0)

# %%%ugly hack above ^^^^^^ for now%%% just take a boost checkout we already have

