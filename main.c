#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/audioout.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

#include "sam.h"
#include "reciter.h"

#define SAMPLE_RATE 22050  // SAM работает на 22050 Гц [citation:14]
#define BUFFER_SIZE 2048

static char text_buffer[256] = "Hello World";
static unsigned char sam_buffer[64000]; // Буфер для PCM данных SAM
static int sam_buffer_pos = 0;

// Callback, который вызывает SAM для каждого сгенерированного байта
static void sam_output_callback(unsigned char b) {
    if (sam_buffer_pos < (int)sizeof(sam_buffer)) {
        sam_buffer[sam_buffer_pos++] = b;
    }
}

static void speak_sam(const char *text) {
    sam_buffer_pos = 0;

    // Настройки голоса (можно менять)
    SetSpeed(72);
    SetPitch(64);
    SetThroat(128);
    SetMouth(128);

    // Конвертируем текст в фонемы (если не фонетический режим)
    strcpy((char*)input, text);
    TextToPhonemes(&input[0]);

    // Устанавливаем входные данные для SAM и генерируем PCM
    SetInput(input);
    SAMMain(sam_output_callback);

    if (sam_buffer_pos == 0) return;

    // Открываем аудио порт (SAM выдает 8-бит моно)
    // Vita хочет 16-бит, поэтому будем конвертировать на лету
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_BGM,
                                   BUFFER_SIZE / 2,
                                   SAMPLE_RATE,
                                   SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return;

    // Конвертируем 8-бит unsigned в 16-бит signed и отправляем
    short *out = (short*)malloc(BUFFER_SIZE * sizeof(short));
    if (!out) { sceAudioOutReleasePort(port); return; }

    for (int i = 0; i < sam_buffer_pos; i += BUFFER_SIZE / 2) {
        int count = BUFFER_SIZE / 2;
        if (i + count > sam_buffer_pos) count = sam_buffer_pos - i;

        for (int j = 0; j < count; j++) {
            // SAM выдает unsigned 8-bit (0-255), нам нужно signed 16-bit
            short s = (short)((int)sam_buffer[i + j] - 128) * 256;
            out[j * 2] = s;      // Левый
            out[j * 2 + 1] = s;  // Правый
        }
        // Заполняем остаток тишиной
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
