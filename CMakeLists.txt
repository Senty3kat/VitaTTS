cmake_minimum_required(VERSION 3.5)
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain")
endif()
project(VitaTTS C CXX)
include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaTTS")
set(VITA_TITLEID  "VTTS00001")
set(VITA_VERSION  "01.00")

# задаются из workflow: -DWITH_PICO=ON/OFF -DWITH_ESPEAK=ON/OFF -DWITH_MBROWRAP=ON/OFF -DWITH_SAM=ON/OFF -DWITH_RHVOICE=ON/OFF
# -DPICO_LIB=... -DESPEAK_DIR=... -DSAM_DIR=... -DRH_DIR=... -DPAYLOAD=... -DICON=...
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -O2 -w -std=gnu11 -D_GNU_SOURCE")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wl,-q -O2 -w -std=gnu++17 -D_GNU_SOURCE")

set(LIBS "")
set(VL "$ENV{VITASDK}/arm-vita-eabi/lib")

# общая заглушка endian.h (в newlib Vita её нет)
set(ENDIAN_SHIM "#ifndef VITA_ENDIAN_SHIM_H\n#define VITA_ENDIAN_SHIM_H\n#include <stdint.h>\n#ifndef __BYTE_ORDER\n#define __LITTLE_ENDIAN 1234\n#define __BIG_ENDIAN 4321\n#define __BYTE_ORDER __LITTLE_ENDIAN\n#endif\n#ifndef htobe16\n#define htobe16(x) __builtin_bswap16(x)\n#endif\n#ifndef htole16\n#define htole16(x) (x)\n#endif\n#ifndef be16toh\n#define be16toh(x) __builtin_bswap16(x)\n#endif\n#ifndef le16toh\n#define le16toh(x) (x)\n#endif\n#ifndef htobe32\n#define htobe32(x) __builtin_bswap32(x)\n#endif\n#ifndef htole32\n#define htole32(x) (x)\n#endif\n#ifndef be32toh\n#define be32toh(x) __builtin_bswap32(x)\n#endif\n#ifndef le32toh\n#define le32toh(x) (x)\n#endif\n#ifndef htobe64\n#define htobe64(x) __builtin_bswap64(x)\n#endif\n#ifndef htole64\n#define htole64(x) (x)\n#endif\n#ifndef be64toh\n#define be64toh(x) __builtin_bswap64(x)\n#endif\n#ifndef le64toh\n#define le64toh(x) (x)\n#endif\n#endif\n")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/gen/compat")
file(WRITE "${CMAKE_BINARY_DIR}/gen/endian.h" "${ENDIAN_SHIM}")
file(WRITE "${CMAKE_BINARY_DIR}/gen/compat/endian.h" "${ENDIAN_SHIM}")

# ---------- Pico (SVOX) ----------
if(WITH_PICO)
  file(GLOB PICO_SRC "${PICO_LIB}/*.c")
  add_library(ttspico STATIC ${PICO_SRC})
  target_include_directories(ttspico PRIVATE "${CMAKE_BINARY_DIR}/gen" "${PICO_LIB}")
  list(APPEND LIBS ttspico)
endif()

# ---------- SAM (Commodore 64) ----------
if(WITH_SAM)
  file(GLOB SAM_SRC "${SAM_DIR}/src/*.c")
  list(FILTER SAM_SRC EXCLUDE REGEX "/main\\.c$")
  add_library(ttssam STATIC ${SAM_SRC})
  target_include_directories(ttssam PRIVATE "${SAM_DIR}/src")
  target_compile_definitions(ttssam PRIVATE speed=sam_speed SetSpeed=SAM_SetSpeed)
  target_compile_options(ttssam PRIVATE -fcommon -fpermissive -Wno-implicit-function-declaration -Wno-implicit-int -Wno-int-conversion -Wno-incompatible-pointer-types)
  list(APPEND LIBS ttssam)
endif()

# ---------- eSpeak-NG ----------
if(WITH_ESPEAK)
  file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/gen")
  file(WRITE "${CMAKE_BINARY_DIR}/gen/config.h"
"#define PACKAGE_NAME \"espeak-ng\"\n#define PACKAGE_VERSION \"1.51.1\"\n#define PACKAGE_STRING \"espeak-ng 1.51.1\"\n#define HAVE_STDINT_H 1\n#define HAVE_INTTYPES_H 1\n")
  file(GLOB ES_SRC "${ESPEAK_DIR}/src/libespeak-ng/*.c" "${ESPEAK_DIR}/src/ucd-tools/src/*.c")
  list(FILTER ES_SRC EXCLUDE REGEX "compilembrola\\.c$|sPlayer\\.c$")
  if(NOT WITH_MBROWRAP)
    list(FILTER ES_SRC EXCLUDE REGEX "mbrowrap\\.c$")
  endif()
  add_library(ttsespeak STATIC ${ES_SRC})
  target_compile_definitions(ttsespeak PRIVATE HAVE_CONFIG_H)
  target_include_directories(ttsespeak PRIVATE
    "${CMAKE_BINARY_DIR}/gen"
    "${ESPEAK_DIR}/src/include"
    "${ESPEAK_DIR}/src/libespeak-ng"
    "${ESPEAK_DIR}/src/ucd-tools/src/include")
  list(APPEND LIBS ttsespeak)
endif()

# ---------- RHVoice (русский, HTS) ----------
# RH_DIR — подготовленная workflow копия RHVoice: src/ (с config.h и заглушкой package_client.hpp) и boost_inc/ (заголовки Boost)
if(WITH_RHVOICE)
  file(GLOB RH_CORE "${RH_DIR}/src/core/*.cpp")
  list(FILTER RH_CORE EXCLUDE REGEX "/(emoji_data|unidata|mage_hts_engine_impl)\\.cpp$")
  file(GLOB RH_HTS "${RH_DIR}/src/hts_engine/*.c")
  add_library(ttsrh STATIC ${RH_CORE} ${RH_HTS} "${RH_DIR}/src/lib/lib.cpp")
  target_include_directories(ttsrh PRIVATE
    "${RH_DIR}/src/include" "${RH_DIR}/src/include/core" "${RH_DIR}/src/core" "${RH_DIR}/src"
    "${RH_DIR}/src/hts_engine" "${RH_DIR}/src/third-party/utf8" "${RH_DIR}/src/third-party/rapidxml"
    "${RH_DIR}/boost_inc")
  target_compile_definitions(ttsrh PRIVATE "VERSION=\"1.16.0\"" HTS_EMBEDDED MAX_RATE=3)
  list(APPEND LIBS ttsrh)
endif()

add_executable(${PROJECT_NAME} main.c)
if(WITH_RHVOICE)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_RHVOICE)
  target_include_directories(${PROJECT_NAME} PRIVATE "${RH_DIR}/src/include")
  set_target_properties(${PROJECT_NAME} PROPERTIES LINKER_LANGUAGE CXX)   # libstdc++ для RHVoice
endif()
if(WITH_PICO)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_PICO)
  target_include_directories(${PROJECT_NAME} PRIVATE "${PICO_LIB}")
endif()
if(WITH_SAM)
  target_compile_definitions(${PROJECT_NAME} PRIVATE HAVE_SAM)
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
          pthread freetype png jpeg webp webpdemux webpmux sharpyuv bz2 brotlidec brotlicommon z m)
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
if(WITH_RHVOICE)
  file(GLOB_RECURSE RF RELATIVE "${PAYLOAD}" "${PAYLOAD}/rh/*")
  foreach(f ${RF})
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
