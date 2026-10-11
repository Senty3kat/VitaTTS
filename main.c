#include <psp2/ctrl.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/audioout.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

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

int main(void) {
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

        sceKernelDelayThread(50000);
    }

    sceKernelExitProcess(0);
    return 0;
}
