#include "debugScreen.h"
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#define SCREEN_W 960
#define SCREEN_H 544
#define CHAR_W   8
#define CHAR_H   16
#define COLS     (SCREEN_W / CHAR_W)
#define ROWS     (SCREEN_H / CHAR_H)

static char *g_fb = NULL;
static SceUID g_fb_uid = -1;
static int g_cursor_x = 0;
static int g_cursor_y = 0;

// Упрощённый рендер: рисуем точки по битам кода символа.
// Не настоящий шрифт, но текст визуально различим.
static void draw_char(int x, int y, unsigned char c, unsigned int color) {
    for (int row = 0; row < CHAR_H; row++) {
        for (int col = 0; col < CHAR_W; col++) {
            int bit = ((c + row) >> (col % 5)) & 1;
            if (bit) {
                int px = x + col;
                int py = y + row;
                if (px >= 0 && px < SCREEN_W && py >= 0 && py < SCREEN_H) {
                    unsigned int *p = (unsigned int *)(g_fb + (py * SCREEN_W + px) * 4);
                    *p = color;
                }
            }
        }
    }
}

void psvDebugScreenInit() {
    g_fb_uid = sceKernelAllocMemBlock("debug_fb",
        SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
        SCREEN_W * SCREEN_H * 4, NULL);

    sceKernelGetMemBlockBase(g_fb_uid, (void **)&g_fb);

    SceDisplayFrameBuf fb;
    memset(&fb, 0, sizeof(fb));
    fb.size        = sizeof(fb);
    fb.base        = g_fb;
    fb.pitch       = SCREEN_W * 4;
    fb.width       = SCREEN_W;
    fb.height      = SCREEN_H;
    fb.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;

    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_UPDATETIMING_NEXTVSYNC);
    sceDisplayWaitVblankStart();
    sceDisplaySetFrameBuf(&fb, SCE_DISPLAY_UPDATETIMING_NEXTVSYNC);

    psvDebugScreenClear();
}

void psvDebugScreenClear() {
    if (!g_fb) return;
    unsigned int black = 0xFF000000;
    unsigned int *p = (unsigned int *)g_fb;
    for (int i = 0; i < SCREEN_W * SCREEN_H; i++) p[i] = black;
    g_cursor_x = 0;
    g_cursor_y = 0;
}

void psvDebugScreenSetXY(int x, int y) {
    g_cursor_x = x;
    g_cursor_y = y;
}

int psvDebugScreenPrintf(const char *format, ...) {
    char buf[512];
    va_list args;
    va_start(args, format);
    int n = vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    unsigned int white = 0xFFFFFFFF;

    for (int i = 0; i < n; i++) {
        char c = buf[i];
        if (c == '\n') {
            g_cursor_x = 0;
            g_cursor_y++;
            continue;
        }
        if (c == '\r') { g_cursor_x = 0; continue; }

        int px = g_cursor_x * CHAR_W;
        int py = g_cursor_y * CHAR_H;
        if (py + CHAR_H > SCREEN_H) {
            break;
        }
        draw_char(px, py, (unsigned char)c, white);

        g_cursor_x++;
        if (g_cursor_x >= COLS) {
            g_cursor_x = 0;
            g_cursor_y++;
        }
    }
    return n;
}
