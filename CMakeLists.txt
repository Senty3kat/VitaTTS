cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(
            CMAKE_TOOLCHAIN_FILE
            "$ENV{VITASDK}/share/vita.toolchain.cmake"
            CACHE PATH "VitaSDK toolchain"
        )
    else()
        message(FATAL_ERROR "VITASDK is not configured")
    endif()
endif()

project(VitaTTS LANGUAGES C CXX)

include("$ENV{VITASDK}/share/vita.cmake" REQUIRED)

set(VITA_APP_NAME "VitaTTS")
set(VITA_TITLEID "VTTS00001")
set(VITA_VERSION "01.00")

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11")

set(VITA_SAMPLES_DIR "${CMAKE_SOURCE_DIR}/vitasdk-samples"
    CACHE PATH "VitaSDK samples directory")

include_directories(
    "${VITA_SAMPLES_DIR}/common"
)

add_executable(VitaTTS
    src/main.cpp
    "${VITA_SAMPLES_DIR}/common/debugScreen.c"
)

target_link_libraries(VitaTTS
    soloud
    stdc++
    pthread
    m
    SceDisplay_stub
    SceCtrl_stub
    SceAudio_stub
)

vita_create_self(VitaTTS.self VitaTTS)

vita_create_vpk(VitaTTS.vpk VTTS00001 VitaTTS.self
    VERSION 01.00
    NAME "VitaTTS"
)
