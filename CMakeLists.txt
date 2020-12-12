set(srcs
  "application.cpp"
  "p44obj.cpp"
  "logger.cpp"
  "utils.cpp"
  "extutils.cpp"
  "timeutils.cpp"
  "colorutils.cpp"
  "error.cpp"
  "mainloop.cpp"
  "fdcomm.cpp"
  "socketcomm.cpp"
  "valueanimator.cpp"
  "analogio.cpp"
  "digitalio.cpp"
  "dcmotor.cpp"
  "iopin.cpp"
  "gpio.cpp"
  "pwm.cpp")

set(incl
  "." "../../main" "thirdparty")

if(CONFIG_P44UTILS_ENABLE_P44SCRIPT)
    list(APPEND srcs
      "p44script.cpp")
endif()

if(CONFIG_P44UTILS_ENABLE_LEDCHAIN)
    list(APPEND srcs
      "ledchaincomm.cpp"
      "thirdparty/esp32_ws281x/esp32_ws281x.c")
    list(APPEND incl
      "thirdparty/esp32_ws281x"
      "../../components/p44lrgraphics")
endif()

if(CONFIG_P44UTILS_ENABLE_JSON)
    list(APPEND srcs
      "jsonobject.cpp"
      "jsonobject.cpp"
      "jsoncomm.cpp"
      "jsonrpccomm.cpp"
      "thirdparty/json-c/arraylist.c"
      "thirdparty/json-c/debug.c"
      "thirdparty/json-c/json_c_version.c"
      "thirdparty/json-c/json_object.c"
      "thirdparty/json-c/json_object_iterator.c"
      "thirdparty/json-c/json_tokener.c"
      "thirdparty/json-c/json_util.c"
      "thirdparty/json-c/linkhash.c"
      "thirdparty/json-c/printbuf.c"
      "thirdparty/json-c/random_seed.c")
    list(APPEND incl
      "thirdparty/json-c")
endif()

idf_component_register(SRCS "${srcs}"
                    INCLUDE_DIRS "${incl}")

target_compile_options(${COMPONENT_LIB} PRIVATE
  -Wno-reorder
  -Wno-missing-field-initializers
  -frtti
  -isystem /Volumes/CaseSens/openwrt/build_dir/target-mipsel_24kc_musl/boost_1_71_0)

# %%%ugly hack above ^^^^^^ for now%%% just take a boost checkout we already have

