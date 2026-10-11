#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/audioout.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sam.h"
#include "reciter.h"

#define SAMPLE_RATE 22050
#define BUFFER_SIZE 2048

static char text_buffer[256] = "Hello World";
static unsigned char sam_buffer[64000];
static int sam_buffer_pos = 0;

// SAM генерирует звук через этот колбэк
static void sam_output_callback(unsigned char b) {
    if (sam_buffer_pos < (int)sizeof(sam_buffer)) {
        sam_buffer[sam_buffer_pos++] = b;
    }
}

static void speak_sam(const char *text) {
    sam_buffer_pos = 0;

    // Настройки голоса (стандарт SAM)
    SetSpeed(72);
    SetPitch(64);
    SetThroat(128);
    SetMouth(128);

    // Вызов парсера текста в фонемы
    // В этом форке TextToPhonemes() возвращает 0 при успехе, 1 при ошибке
    int result = TextToPhonemes((char*)text);
    if (result != 0) {
        // Ошибка парсинга (например, пустой текст)
        return;
    }

    // Указываем SAM, куда выводить байты, и запускаем синтез
    SetInput(input); // 'input' теперь объявлена в reciter.h
    SAMMain(sam_output_callback);

    if (sam_buffer_pos == 0) return;

    // Открываем аудио-порт
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                   BUFFER_SIZE / 2,
                                   SAMPLE_RATE,
                                   SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return;

    short *out = (short*)malloc(BUFFER_SIZE * sizeof(short));
    if (!out) { sceAudioOutReleasePort(port); return; }

    // Конвертация и отправка звука
    for (int i = 0; i < sam_buffer_pos; i += BUFFER_SIZE / 2) {
        int count = BUFFER_SIZE / 2;
        if (i + count > sam_buffer_pos) count = sam_buffer_pos - i;

        for (int j = 0; j < count; j++) {
            // Конвертируем 8-bit unsigned (0-255) в 16-bit signed
            short s = (short)((int)sam_buffer[i + j] - 128) * 256;
            out[j * 2] = s;      // Левый канал
            out[j * 2 + 1] = s;  // Правый канал
        }
        // Тишина в конце буфера
        for (int j = count; j < BUFFER_SIZE / 2; j++) {
            out[j * 2] = 0;
            out[j * 2 + 1] = 0;
        }
        sceAudioOutOutput(port, out);
    }

    free(out);
    sceAudioOutReleasePort(port);
}

int main(void) {
    SceCtrlData pad;
    int prev_start = 0;

    sceKernelDelayThread(100000);

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        int start_now = (pad.buttons & SCE_CTRL_START) ? 1 : 0;

        if (start_now && !prev_start) {
            speak_sam(text_buffer);
        }
        prev_start = start_now;

        sceKernelDelayThread(50000);
    }

    sceKernelExitProcess(0);
    return 0;
}
