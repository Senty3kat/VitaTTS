#include <psp2/ctrl.h>
#include <psp2/ime.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/audioout.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 1024

static char text_buffer[256] = "Hello World";
static SceImeParam ime_param;
static volatile int ime_active = 0;

// --- Параметры синтезатора (просто для примера) ---
// Частоты формант для разных "гласных"
static int formant_freqs[][3] = {
    {730, 1090, 2440}, // "a" как в "father"
    {530, 1840, 2480}, // "e" как в "bed"
    {270, 2290, 3010}, // "i" как в "see"
    {570, 840, 2410},  // "o" как в "more"
    {300, 870, 2240},  // "u" как в "boot"
};
static int formant_idx = 0;

// Генератор простого тона с формантами
static void generate_tone(short *buffer, int len, int freq, float duration_sec) {
    int samples = (int)(SAMPLE_RATE * duration_sec);
    if (samples > len) samples = len;

    for (int i = 0; i < samples; i++) {
        float t = (float)i / SAMPLE_RATE;
        // Основной тон (голосовые связки)
        float val = sinf(2.0f * M_PI * freq * t);

        // Добавляем форманты (резонансы)
        for (int f = 0; f < 3; f++) {
            float formant_freq = formant_freqs[formant_idx][f];
            val += 0.3f * sinf(2.0f * M_PI * formant_freq * t);
        }

        // Нормализация и запись в стерео-буфер (левый и правый канал)
        val = val / 4.0f; // чтобы не клиппировало
        buffer[i * 2] = (short)(val * 32767);
        buffer[i * 2 + 1] = (short)(val * 32767);
    }

    // Заполняем остаток тишиной
    for (int i = samples; i < len / 2; i++) {
        buffer[i * 2] = 0;
        buffer[i * 2 + 1] = 0;
    }
}

// "Произнести" текст
static void speak_text(const char *text) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, BUFFER_SIZE / 2, SAMPLE_RATE, SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return;

    short *buffer = (short *)malloc(BUFFER_SIZE * sizeof(short));
    if (!buffer) {
        sceAudioOutReleasePort(port);
        return;
    }

    // Проходим по каждому символу и издаём звук
    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == ' ') {
            // Пауза
            memset(buffer, 0, BUFFER_SIZE * sizeof(short));
            sceAudioOutOutput(port, buffer);
        } else {
            // Выбираем "гласную" по индексу символа (просто чтобы было разнообразие)
            formant_idx = (c % 5);
            // Случайная частота основного тона от 100 до 200 Гц
            int base_freq = 100 + (c % 100);
            generate_tone(buffer, BUFFER_SIZE, base_freq, 0.15f); // 150 мс на символ
            sceAudioOutOutput(port, buffer);
        }
    }

    free(buffer);
    sceAudioOutReleasePort(port);
}

// --- IME (клавиатура) ---
static void ime_handler(void *arg, const SceImeEventData *e) {
    if (e->id == SCE_IME_EVENT_PRESS_ENTER || e->id == SCE_IME_EVENT_PRESS_CLOSE) {
        ime_active = 0;
    }
}

static void open_keyboard(void) {
    sceImeParamInit(&ime_param);
    ime_param.supportedLanguages = SCE_IME_LANGUAGE_ENGLISH;
    ime_param.languagesForced = SCE_TRUE;
    ime_param.type = SCE_IME_TYPE_DEFAULT;
    ime_param.option = SCE_IME_OPTION_NO_ASSISTANCE;
    ime_param.inputTextBuffer = text_buffer;
    ime_param.maxTextLength = sizeof(text_buffer) - 1;
    ime_param.handler = ime_handler;
    ime_param.arg = NULL;
    ime_param.initialText = text_buffer;
    ime_param.title = "TTS Input";

    ime_active = 1;
    sceImeOpen(&ime_param);
}

// --- Main ---
int main(void) {
    SceCtrlData pad;
    int prev_square = 0;
    int prev_start = 0;

    while (1) {
        if (ime_active) {
            sceImeUpdate();
            sceKernelDelayThread(10000);
            continue;
        }

        sceCtrlPeekBufferPositive(0, &pad, 1);
        int square_now = (pad.buttons & SCE_CTRL_SQUARE);
        int start_now = (pad.buttons & SCE_CTRL_START);

        if (square_now && !prev_square) {
            open_keyboard();
        }

        if (start_now && !prev_start) {
            speak_text(text_buffer);
        }

        prev_square = square_now;
        prev_start = start_now;

        sceKernelDelayThread(50000);
    }

    sceKernelExitProcess(0);
    return 0;
}
