#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/audioout.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <vita2d.h>

#define SAMPLE_RATE 44100
#define BUFFER_SIZE 1024

static char text_buffer[256] = "Hello World From Vita";

static int formant_freqs[][3] = {
    {730, 1090, 2440},
    {530, 1840, 2480},
    {270, 2290, 3010},
    {570, 840, 2410},
    {300, 870, 2240},
};
static int formant_idx = 0;

static void generate_tone(short *buffer, int len, int freq, float duration_sec) {
    int samples = (int)(SAMPLE_RATE * duration_sec);
    if (samples > len / 2) samples = len / 2;

    for (int i = 0; i < samples; i++) {
        float t = (float)i / SAMPLE_RATE;
        float val = sinf(2.0f * M_PI * freq * t);
        for (int f = 0; f < 3; f++) {
            float ff = formant_freqs[formant_idx][f];
            val += 0.3f * sinf(2.0f * M_PI * ff * t);
        }
        val = val / 4.0f;
        buffer[i * 2]     = (short)(val * 32767);
        buffer[i * 2 + 1] = (short)(val * 32767);
    }
    for (int i = samples; i < len / 2; i++) {
        buffer[i * 2] = 0;
        buffer[i * 2 + 1] = 0;
    }
}

static void speak_text(const char *text) {
    int port = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN,
                                   BUFFER_SIZE / 2,
                                   SAMPLE_RATE,
                                   SCE_AUDIO_OUT_MODE_STEREO);
    if (port < 0) return;

    short *buffer = (short *)malloc(BUFFER_SIZE * sizeof(short));
    if (!buffer) { sceAudioOutReleasePort(port); return; }

    for (int i = 0; text[i] != '\0'; i++) {
        char c = text[i];
        if (c == ' ') {
            memset(buffer, 0, BUFFER_SIZE * sizeof(short));
            sceAudioOutOutput(port, buffer);
        } else {
            formant_idx = (c % 5);
            int base_freq = 100 + (c % 100);
            generate_tone(buffer, BUFFER_SIZE, base_freq, 0.15f);
            sceAudioOutOutput(port, buffer);
        }
    }

    free(buffer);
    sceAudioOutReleasePort(port);
}

// Рисуем интерфейс
static void draw_ui(vita2d_pgf *font) {
    vita2d_start_drawing();
    vita2d_clear_screen();

    vita2d_pgf_draw_text(font, 20, 40, RGBA8(255, 255, 255, 255), 1.0f, "Vita Simple TTS");

    // Рисуем текст в рамке
    vita2d_draw_rectangle(20, 60, 920, 40, RGBA8(40, 40, 60, 255)); // Фон поля
    vita2d_pgf_draw_text(font, 30, 90, RGBA8(255, 255, 255, 255), 1.0f, text_buffer);

    // Подсказки
    vita2d_pgf_draw_text(font, 20, 140, RGBA8(200, 200, 100, 255), 1.0f, "CONTROLS:");
    vita2d_pgf_draw_text(font, 20, 170, RGBA8(180, 180, 180, 255), 1.0f, "START   - Speak text");
    vita2d_pgf_draw_text(font, 20, 190, RGBA8(180, 180, 180, 255), 1.0f, "CIRCLE  - Clear text");
    vita2d_pgf_draw_text(font, 20, 210, RGBA8(180, 180, 180, 255), 1.0f, "TRIANGLE- Reset to default text");

    vita2d_pgf_draw_text(font, 20, 260, RGBA8(150, 150, 150, 255), 1.0f, "Press START to hear the voice.");

    vita2d_end_drawing();
    vita2d_swap_buffers();
}

int main(void) {
    // Инициализация графики
    vita2d_init();
    vita2d_pgf *font = vita2d_load_default_pgf();
    vita2d_set_clear_color(RGBA8(0, 0, 0, 255));

    SceCtrlData pad;
    int prev_start = 0;
    int prev_circle = 0;
    int prev_triangle = 0;

    while (1) {
        sceCtrlPeekBufferPositive(0, &pad, 1);

        int start_now    = (pad.buttons & SCE_CTRL_START)    ? 1 : 0;
        int circle_now   = (pad.buttons & SCE_CTRL_CIRCLE)   ? 1 : 0;
        int triangle_now = (pad.buttons & SCE_CTRL_TRIANGLE) ? 1 : 0;

        if (start_now && !prev_start) {
            speak_text(text_buffer);
        }
        if (circle_now && !prev_circle) {
            text_buffer[0] = '\0';
        }
        if (triangle_now && !prev_triangle) {
            strncpy(text_buffer, "Hello World From Vita", sizeof(text_buffer) - 1);
            text_buffer[sizeof(text_buffer) - 1] = '\0';
        }

        prev_start    = start_now;
        prev_circle   = circle_now;
        prev_triangle = triangle_now;

        // Рисуем интерфейс каждый кадр
        draw_ui(font);

        sceKernelDelayThread(16000); // ~60 FPS
    }

    vita2d_free_pgf(font);
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
