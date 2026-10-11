cmake_minimum_required(VERSION 3.5)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
endif()
project(VitaTTS C)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaTTS")
set(VITA_TITLEID  "VTTS00001")
set(VITA_VERSION  "01.00")

# задаются из workflow: -DWITH_PICO=ON/OFF -DWITH_ESPEAK=ON/OFF -DWITH_MBROWRAP=ON/OFF
# -DPICO_LIB=... -DESPEAK_DIR=... -DPAYLOAD=... -DICON=...
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O2 -w -std=gnu11 -D_GNU_SOURCE")

set(LIBS "")
set(VL "$ENV{VITASDK}/arm-vita-eabi/lib")

# ---------- Pico (SVOX) ----------
if(WITH_PICO)
  file(GLOB PICO_SRC "${PICO_LIB}/*.c")
  add_library(ttspico STATIC ${PICO_SRC})
  target_include_directories(ttspico PRIVATE "${PICO_LIB}")
  list(APPEND LIBS ttspico)
endif()

# ---------- eSpeak-NG ----------
if(WITH_ESPEAK)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/gen")
  file(WRITE "${CMAKE_BINARY_DIR}/gen/config.h"
"#define PACKAGE_NAME \"espeak-ng\"\n#define PACKAGE_VERSION \"1.51.1\"\n#define PACKAGE_STRING \"espeak-ng 1.51.1\"\n#define HAVE_STDINT_H 1\n#define HAVE_INTTYPES_H 1\n")
  file(GLOB ES_SRC "${ESPEAK_DIR}/src/libespeak-ng/*.c" "${ESPEAK_DIR}/src/ucd-tools/src/*.c")
  if(NOT WITH_MBROWRAP)
    list(FILTER ES_SRC EXCLUDE REGEX "mbrowrap\\.c$")
  endif()
  add_library(ttsespeak STATIC ${ES_SRC})
  target_compile_definitions(ttsespeak PRIVATE HAVE_CONFIG_H)
  target_include_directories(ttsespeak PRIVATE
    "${CMAKE_BINARY_DIR}/gen"
    "${ESPEAK_DIR}/src/include"
    "${ESPEAK_DIR}/src/include/compat"
    "${ESPEAK_DIR}/src/libespeak-ng"
    "${ESPEAK_DIR}/src/ucd-tools/src/include")
  list(APPEND LIBS ttsespeak)
endif()

add_executable(${PROJECT_NAME} main.c)
if(WITH_PICO)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_PICO)
  target_include_directories(${PROJECT_NAME} PRIVATE "${PICO_LIB}")
endif()
if(WITH_ESPEAK)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_ESPEAK)
  target_include_directories(${PROJECT_NAME} PRIVATE "${ESPEAK_DIR}/src/include")
endif()

# системные/внешние библиотеки добавляются только если они реально есть в SDK
list(APPEND LIBS vita2d)
foreach(l SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub ScePgf_stub ScePvf_stub
          SceCommonDialog_stub SceImeDialog_stub SceAudio_stub SceTouch_stub SceAppMgr_stub
          SceLibKernel_stub SceProcessmgr_stub SceThreadmgr_stub SceIofilemgr_stub
          freetype png jpeg webp webpdemux webpmux sharpyuv bz2 brotlidec brotlicommon z m)
  if(EXISTS "${VL}/lib${l}.a")
    list(APPEND LIBS ${l})
  endif()
endforeach()
target_link_libraries(${PROJECT_NAME} -Wl,--start-group ${LIBS} -Wl,--end-group)

# ---------- VPK ----------
set(VPKF "")
if(WITH_PICO)
  foreach(b en-US_ta.bin en-US_lh0_sg.bin en-GB_ta.bin en-GB_kh0_sg.bin)
    list(APPEND VPKF "${PAYLOAD}/${b}" "${b}")
  endforeach()
endif()
if(WITH_ESPEAK)
  file(GLOB_RECURSE EF RELATIVE "${PAYLOAD}" "${PAYLOAD}/espeak-ng-data/*")
  foreach(f ${EF})
    list(APPEND VPKF "${PAYLOAD}/${f}" "${f}")
  endforeach()
endif()
if(EXISTS "${ICON}")
  list(APPEND VPKF "${ICON}" "sce_sys/icon0.png")
endif()

vita_create_self(${PROJECT_NAME}.self ${PROJECT_NAME})
vita_create_vpk(${PROJECT_NAME}.vpk ${VITA_TITLEID} ${PROJECT_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE ${VPKF})
