cmake_minimum_required(VERSION 3.10)
project(vita-simple-tts C)

set(VITA_APP_NAME "Vita Simple TTS")
set(VITA_TITLEID  "ROBO12345")
set(VITA_VERSION  "01.00")

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -O2")

add_executable(vita-simple-tts main.c)

target_link_libraries(vita-simple-tts
    SceCtrl_stub
    SceAudio_stub
    SceKernelDmacMgr_stub
    SceAppMgr_stub
    SceSysmodule_stub
    ScePower_stub
    m
    c
)

vita_create_self(vita-simple-tts.self vita-simple-tts)
vita_create_vpk(vita-simple-tts.vpk ${VITA_TITLEID} vita-simple-tts.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
