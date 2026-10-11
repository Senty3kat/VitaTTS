cmake_minimum_required(VERSION 3.10)
project(vita-sam-tts C)

set(VITA_APP_NAME "Vita SAM TTS")
set(VITA_TITLEID  "TTSA00004")
set(VITA_VERSION  "01.00")

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -std=gnu11 -O2")

# Добавляем исходники SAM
add_executable(vita-sam-tts
    main.c
    sam.c
    reciter.c
)

target_link_libraries(vita-sam-tts
    SceCtrl_stub
    SceAudio_stub
    SceKernelThreadMgr_stub
    SceAppMgr_stub
    SceSysmodule_stub
    ScePower_stub
    m
    c
)

vita_create_self(vita-sam-tts.self vita-sam-tts)
vita_create_vpk(vita-sam-tts.vpk ${VITA_TITLEID} vita-sam-tts.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
