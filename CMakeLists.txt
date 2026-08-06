if(ESP_PLATFORM)
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

  set(incl "." "../../main" "thirdparty")

  if(CONFIG_P44UTILS_ENABLE_P44SCRIPT)
    list(APPEND srcs "p44script.cpp")
  endif()

  if(CONFIG_P44UTILS_ENABLE_LEDCHAIN)
    list(APPEND srcs
      "ledchaincomm.cpp"
      "thirdparty/esp32_ws281x/esp32_ws281x.c")
    list(APPEND incl "../../components/p44lrgraphics" "thirdparty/esp32_ws281x")
  endif()

  if(NOT DEFINED ENABLE_ARTNET OR ENABLE_ARTNET)
    list(APPEND srcs "artnetcomm.cpp")
  endif()

  idf_component_register(SRCS "${srcs}" INCLUDE_DIRS "${incl}")
  target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-reorder -Wno-missing-field-initializers -frtti)
  return()
endif()

set(P44UTILS_VENDOR_INCLUDE "${CMAKE_CURRENT_BINARY_DIR}/vendor_include")
file(MAKE_DIRECTORY "${P44UTILS_VENDOR_INCLUDE}")
foreach(vendor_include_dir IN ITEMS sqlite3pp slre uwsc compat_glue)
  if(EXISTS "${P44UTILS_VENDOR_INCLUDE}/${vendor_include_dir}")
    file(REMOVE_RECURSE "${P44UTILS_VENDOR_INCLUDE}/${vendor_include_dir}")
  endif()
  file(CREATE_LINK
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/${vendor_include_dir}"
    "${P44UTILS_VENDOR_INCLUDE}/${vendor_include_dir}"
    SYMBOLIC
  )
endforeach()

if(NOT DEFINED P44UTILS_SOURCES)
  set(P44UTILS_SOURCES
    analogio.cpp
    application.cpp
    artnetcomm.cpp
    backlight.cpp
    colorutils.cpp
    consolekey.cpp
    crc32.cpp
    dcmotor.cpp
    digitalio.cpp
    dmxhandler.cpp
    error.cpp
    extutils.cpp
    fdcomm.cpp
    fnv.cpp
    gpio.cpp
    httpcomm.cpp
    i2c.cpp
    igmp.cpp
    iopin.cpp
    jsoncomm.cpp
    jsonobject.cpp
    jsonrpccomm.cpp
    jsonwebclient.cpp
    ledchaincomm.cpp
    logger.cpp
    macaddress.cpp
    mainloop.cpp
    midi.cpp
    operationqueue.cpp
    p44obj.cpp
    p44script.cpp
    persistentparams.cpp
    pwm.cpp
    regexp.cpp
    serialcomm.cpp
    serialqueue.cpp
    socketcomm.cpp
    spi.cpp
    sqlite3persistence.cpp
    ssdpsearch.cpp
    timeutils.cpp
    tlv.cpp
    utils.cpp
    valueanimator.cpp
    valueunits.cpp
    thirdparty/civetweb/civetweb.c
    thirdparty/slre/slre.c
    thirdparty/sqlite3pp/sqlite3pp.cpp
    thirdparty/sqlite3pp/sqlite3ppext.cpp
  )

  if(NOT APPLE)
    list(APPEND P44UTILS_SOURCES dnssd.cpp)
  endif()

  if(ENABLE_UBUS)
    list(APPEND P44UTILS_SOURCES ubus.cpp)
  endif()

  if(ENABLE_UWSC)
    list(APPEND P44UTILS_SOURCES websocket.cpp)
    if(APPLE)
      list(APPEND P44UTILS_SOURCES
        thirdparty/uwsc/uwsc.c
        thirdparty/uwsc/log.c
        thirdparty/uwsc/sha1.c
        thirdparty/uwsc/ssl.c
        thirdparty/uwsc/utils.c
        thirdparty/uwsc/buffer/buffer.c
        thirdparty/uwsc/ssl/openssl.c
        thirdparty/compat_glue/src/socket.c
      )
    endif()
  endif()

  if(ENABLE_MODBUS)
    list(APPEND P44UTILS_SOURCES modbus.cpp)
  endif()

  if(ENABLE_MODBUS AND EMBEDDED_LIBMODBUS)
    list(APPEND P44UTILS_SOURCES
      thirdparty/libmodbus/src/modbus-data.c
      thirdparty/libmodbus/src/modbus-rtu.c
      thirdparty/libmodbus/src/modbus-tcp.c
      thirdparty/libmodbus/src/modbus.c
    )
  endif()

  if(ENABLE_RPIWS281X)
    list(APPEND P44UTILS_SOURCES
      thirdparty/rpi_ws281x/dma.c
      thirdparty/rpi_ws281x/pcm.c
      thirdparty/rpi_ws281x/pwm.c
      thirdparty/rpi_ws281x/rpihw.c
      thirdparty/rpi_ws281x/mailbox.c
      thirdparty/rpi_ws281x/ws2811.c
    )
  endif()

  if(ENABLE_LVGL)
    list(APPEND P44UTILS_SOURCES lvgl.cpp lvglui.cpp)
  endif()

  if(ENABLE_P44FEATURES)
    list(APPEND P44UTILS_SOURCES rfid.cpp)
  endif()
endif()

add_library(p44utils STATIC ${P44UTILS_SOURCES})

if(ENABLE_UWSC)
  target_include_directories(p44utils SYSTEM PUBLIC
    thirdparty/uwsc
    thirdparty/uwsc/buffer
    thirdparty/uwsc/ssl
  )
  if(APPLE)
    set_source_files_properties(websocket.cpp PROPERTIES
      COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
    )
  endif()
endif()

set_source_files_properties(
  regexp.cpp
  sqlite3persistence.cpp
  persistentparams.cpp
  PROPERTIES COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
)

set_source_files_properties(
  thirdparty/uwsc/uwsc.c
  thirdparty/uwsc/log.c
  thirdparty/uwsc/sha1.c
  thirdparty/uwsc/ssl.c
  thirdparty/uwsc/utils.c
  thirdparty/uwsc/buffer/buffer.c
  thirdparty/uwsc/ssl/openssl.c
  thirdparty/compat_glue/src/socket.c
  PROPERTIES COMPILE_OPTIONS "-I${CMAKE_CURRENT_SOURCE_DIR}/thirdparty"
)

set_source_files_properties(thirdparty/civetweb/civetweb.c PROPERTIES
  COMPILE_OPTIONS "-Wno-int-conversion"
)

if(ENABLE_MODBUS AND EMBEDDED_LIBMODBUS)
  target_include_directories(p44utils PUBLIC thirdparty/libmodbus/src)
  target_compile_definitions(p44utils PUBLIC EMBEDDED_LIBMODBUS=1)
endif()

if(ENABLE_RPIWS281X)
  target_include_directories(p44utils PUBLIC thirdparty/rpi_ws281x)
endif()

if(ENABLE_LVGL)
  target_link_libraries(p44utils PUBLIC lvgl)
endif()

target_include_directories(p44utils PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${P44UTILS_VENDOR_INCLUDE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/../p44lrgraphics"
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/civetweb"
  "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/slre"
)
target_link_libraries(p44utils PUBLIC vdcd_config)
target_compile_options(p44utils PRIVATE -Wno-reorder -Wno-missing-field-initializers)
