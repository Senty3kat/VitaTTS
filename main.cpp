#include <psp2/ctrl.h>
#include <psp2/ime.h>
#include <psp2/kernel/processmgr.h>
#include <string.h>
#include <stdio.h>

#include "debugScreen.h"
#include "soloud.h"
#include "soloud_speech.h"

#define printf psvDebugScreenPrintf

SoLoud::Soloud gSoloud;
SoLoud::Speech gSpeech;

// Буфер для текста (IME пишет сюда)
static char text_buffer[512] = "Hello PS Vita";
static SceImeParam ime_param;

// Флаг: открыта ли клавиатура
static volatile int ime_active = 0;

// Колбэк IME — вызывается когда ввод завершён или отменён
static void ime_handler(void *arg, const SceImeEventData *e) {
    if (e->id == SCE_IME_EVENT_PRESS_ENTER) {
        // ENTER = ввод завершён
        ime_active = 0;
    } else if (e->id == SCE_IME_EVENT_PRESS_CLOSE) {
        // Закрытие клавиатуры
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

int main(void) {
    psvDebugScreenInit();

    // Инициализация SoLoud
    gSoloud.init();
    printf("SoLoud TTS Ready!\n");
    printf("SQUARE: Open keyboard\n");
    printf("START: Speak text\n\n");
    printf("Current text: %s\n", text_buffer);

    SceCtrlData pad;
    int prev_square = 0;
    int prev_start = 0;

    while (1) {
        // Если клавиатура активна — обновляем IME и не читаем геймпад
        if (ime_active) {
            sceImeUpdate();
            sceKernelDelayThread(10000);
            continue;
        }

        sceCtrlPeekBufferPositive(0, &pad, 1);

        int square_now = (pad.buttons & SCE_CTRL_SQUARE);
        int start_now = (pad.buttons & SCE_CTRL_START);

        // SQUARE нажатие (фронт) — открыть клавиатуру
        if (square_now && !prev_square) {
            printf("Opening keyboard...\n");
            open_keyboard();
        }

        // START нажатие — произнести текст
        if (start_now && !prev_start) {
            if (strlen(text_buffer) > 0) {
                printf("Speaking: %s\n", text_buffer);
                gSpeech.setText(text_buffer);
                gSoloud.play(gSpeech);
            }
        }

        prev_square = square_now;
        prev_start = start_now;

        // Обновляем экран
        psvDebugScreenClear();
        printf("SoLoud TTS\n\n");
        printf("Text: %s\n\n", text_buffer);
        printf("SQUARE: Open keyboard\n");
        printf("START: Speak\n");

        sceKernelDelayThread(50000); // 20 FPS достаточно
    }

    gSoloud.deinit();
    sceKernelExitProcess(0);
    return 0;
}
