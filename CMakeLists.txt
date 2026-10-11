cmake_minimum_required(VERSION 3.10)
project(vita-tts-test CXX)

set(VITA_APP_NAME "Vita TTS Test")
set(VITA_TITLEID  "VTT00002")
set(VITA_VERSION  "01.00")

include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -O2")

# SoLoud берём из сэмплов VitaSDK
set(SOLOUD_DIR "${VITASDK}/share/vitasdk/samples/soloud")
# Если по этому пути нет — раскомментируй и укажи свой путь:
# set(SOLOUD_DIR "/path/to/soloud")

add_executable(vita-tts-test
    main.cpp
    ${SOLOUD_DIR}/soloud.cpp
    ${SOLOUD_DIR}/soloud_audiosource.cpp
    ${SOLOUD_DIR}/soloud_bus.cpp
    ${SOLOUD_DIR}/soloud_core_3d.cpp
    ${SOLOUD_DIR}/soloud_core_basic.cpp
    ${SOLOUD_DIR}/soloud_core_fader.cpp
    ${SOLOUD_DIR}/soloud_core_filter.cpp
    ${SOLOUD_DIR}/soloud_core_getters.cpp
    ${SOLOUD_DIR}/soloud_core_setters.cpp
    ${SOLOUD_DIR}/soloud_core_voicegroup.cpp
    ${SOLOUD_DIR}/soloud_core_voiceops.cpp
    ${SOLOUD_DIR}/soloud_fader.cpp
    ${SOLOUD_DIR}/soloud_fft.cpp
    ${SOLOUD_DIR}/soloud_fft_lut.cpp
    ${SOLOUD_DIR}/soloud_file.cpp
    ${SOLOUD_DIR}/soloud_filter.cpp
    ${SOLOUD_DIR}/soloud_queue.cpp
    ${SOLOUD_DIR}/soloud_thread.cpp
    ${SOLOUD_DIR}/soloud_speech.cpp
    ${SOLOUD_DIR}/soloud_wav.cpp
    ${SOLOUD_DIR}/soloud_wavstream.cpp
    ${SOLOUD_DIR}/soloud_monotone.cpp
    ${SOLOUD_DIR}/soloud_biquadresonantfilter.cpp
    ${SOLOUD_DIR}/soloud_dcremovalfilter.cpp
    ${SOLOUD_DIR}/soloud_echofilter.cpp
    ${SOLOUD_DIR}/soloud_eqfilter.cpp
    ${SOLOUD_DIR}/soloud_fftfilter.cpp
    ${SOLOUD_DIR}/soloud_flangerfilter.cpp
    ${SOLOUD_DIR}/soloud_lofifilter.cpp
    ${SOLOUD_DIR}/soloud_pitchshiftfilter.cpp
    ${SOLOUD_DIR}/soloud_robotizefilter.cpp
    ${SOLOUD_DIR}/soloud_bassboostfilter.cpp
    ${SOLOUD_DIR}/soloud_freeverbfilter.cpp
    ${SOLOUD_DIR}/soloud_echofilter.cpp
    # rsynth для TTS — файлы лежат в soloud/speech/
    ${SOLOUD_DIR}/speech/rsynth.c
    ${SOLOUD_DIR}/speech/darray.c
    ${SOLOUD_DIR}/speech/hash.c
    ${SOLOUD_DIR}/speech/klatt.c
    ${SOLOUD_DIR}/speech/phones.c
    ${SOLOUD_DIR}/speech/say.c
    ${SOLOUD_DIR}/speech/tts.c
    ${SOLOUD_DIR}/speech/voice.c
    # Плюс файлы из speech/ — смотри список в папке
)

target_include_directories(vita-tts-test PRIVATE
    ${SOLOUD_DIR}
    ${SOLOUD_DIR}/speech
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(vita-tts-test
    SceDisplay_stub
    SceCtrl_stub
    SceAudio_stub
    SceIme_stub
    SceKernelDmacMgr_stub
    SceAppMgr_stub
    SceSysmodule_stub
    ScePower_stub
    m
    c
)

vita_create_self(vita-tts-test.self vita-tts-test)

vita_create_vpk(vita-tts-test.vpk ${VITA_TITLEID} vita-tts-test.self
    VERSION ${VITA_VERSION}
    NAME ${VITA_APP_NAME}
)
