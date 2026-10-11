// VitaTTS - быстрый TTS для PS Vita (Pico/SVOX для английского, eSpeak-NG для RU/JA/EN-retro)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <vita2d.h>

#ifdef HAVE_PICO
#include "picoapi.h"
#include "picodefs.h"
#endif
#ifdef HAVE_ESPEAK
#include <espeak-ng/speak_lib.h>
#endif

int _newlib_heap_size_user = 100 * 1024 * 1024;

#define GRAIN 512
#define HN 5
#define HL 600

/* ---------------- состояние синтеза (общее для потоков) ---------------- */
static volatile int g_abort, g_req, g_busy, g_ready;
static char rq_text[HL];
static int rq_eng, rq_speed = 100, rq_pitch = 100;
static char rq_voice[32];
static int pico_ok[2], es_ok;

/* ---------------- вывод звука: кольцевой буфер + отдельный аудиопоток ---------------- */
#define RING (1 << 20)          /* ~21 c при 48 кГц */
#define PREBUF 14400            /* 0.3 c предбуфер: скрывает паузы синтеза */
static int aport = -1;
static short ring[RING];
static volatile unsigned rhead, rtail;
static volatile int g_flush, g_done, g_play;
static double rt;
static short rprev;

static int audio_thread(SceSize args, void *argp) {
    static short ab[GRAIN];
    int playing = 0;
    for (;;) {
        if (g_flush) { rtail = rhead; playing = 0; g_play = 0; g_flush = 0; continue; }
        unsigned avail = rhead - rtail;
        if (!playing) {
            if (avail >= PREBUF || (g_done && avail > 0)) playing = 1;
            else { sceKernelDelayThread(1000); continue; }
        }
        g_play = 1;
        if (avail >= GRAIN) {
            __sync_synchronize();
            for (int i = 0; i < GRAIN; i++) ab[i] = ring[(rtail + i) & (RING - 1)];
            rtail += GRAIN;
            if (aport >= 0) sceAudioOutOutput(aport, ab);
        } else if (g_done) {
            avail = rhead - rtail;
            if (avail >= GRAIN) continue;
            memset(ab, 0, sizeof ab);
            for (unsigned i = 0; i < avail; i++) ab[i] = ring[(rtail + i) & (RING - 1)];
            rtail += avail;
            if (avail && aport >= 0) sceAudioOutOutput(aport, ab);
            playing = 0; g_play = 0;
        } else {
            sceKernelDelayThread(1000);   /* недогруз: ждём синтез, а не трещим */
        }
    }
    return 0;
}

static void out_begin(void) {
    rt = 0; rprev = 0; g_done = 0;
    g_flush = 1;
    for (int i = 0; g_flush && i < 300; i++) sceKernelDelayThread(1000);
}

static void out_finish(void) {
    __sync_synchronize();
    if (g_abort) g_flush = 1; else g_done = 1;
}

static int emit(const short *p, int n, int rate) {
    if (n <= 0) return g_abort;
    double step = (double)rate / 48000.0;
    unsigned h = rhead;
    while (rt < n) {
        if (g_abort) { __sync_synchronize(); rhead = h; return 1; }
        if (h - rtail >= RING) {
            __sync_synchronize(); rhead = h;
            while (h - rtail >= RING && !g_abort) sceKernelDelayThread(1000);
            continue;
        }
        int i = (int)rt;
        double fr = rt - i;
        short a = i ? p[i - 1] : rprev;
        short b = p[i];
        ring[h & (RING - 1)] = (short)(a + (b - a) * fr);
        h++;
        rt += step;
    }
    rprev = p[n - 1];
    rt -= n;
    __sync_synchronize();
    rhead = h;
    return g_abort;
}

/* ---------------- Pico (SVOX) ---------------- */
#ifdef HAVE_PICO
#define PICO_MEM (24 * 1024 * 1024)
static pico_System ps;
static pico_Engine pe[2];

static void pico_setup(void) {
    static const char *vn[2] = {"VUS", "VGB"};
    static const char *fl[2][2] = {
        {"app0:/en-US_ta.bin", "app0:/en-US_lh0_sg.bin"},
        {"app0:/en-GB_ta.bin", "app0:/en-GB_kh0_sg.bin"}};
    void *mem = malloc(PICO_MEM);
    if (!mem) return;
    if (pico_initialize(mem, PICO_MEM, &ps)) return;
    for (int v = 0; v < 2; v++) {
        pico_Resource r;
        pico_Char nm[PICO_MAX_RESOURCE_NAME_SIZE];
        int bad = 0;
        if (pico_createVoiceDefinition(ps, (const pico_Char *)vn[v])) continue;
        for (int k = 0; k < 2; k++) {
            if (pico_loadResource(ps, (const pico_Char *)fl[v][k], &r) ||
                pico_getResourceName(ps, r, (char *)nm) ||
                pico_addResourceToVoiceDefinition(ps, (const pico_Char *)vn[v], nm)) { bad = 1; break; }
        }
        if (!bad && !pico_newEngine(ps, (const pico_Char *)vn[v], &pe[v])) pico_ok[v] = 1;
    }
}

static void pico_say(int v, const char *txt) {
    static short buf[1024];
    pico_Engine e = pe[v];
    pico_resetEngine(e, 0);
    const pico_Char *in = (const pico_Char *)txt;
    pico_Int16 rem = (pico_Int16)(strlen(txt) + 1), sent = 0, got = 0, dt = 0;
    int guard = 0;
    while (rem > 0 && !g_abort && guard++ < 1000) {
        if (pico_putTextUtf8(e, in, rem, &sent)) break;
        in += sent; rem -= sent;
        int st;
        do {
            got = 0;
            st = pico_getData(e, buf, sizeof(buf), &got, &dt);
            if (got > 0) emit(buf, got / 2, 16000);
        } while (st == PICO_STEP_BUSY && !g_abort);
    }
}
#endif

/* ---------------- eSpeak-NG ---------------- */
#ifdef HAVE_ESPEAK
static int es_rate = 22050;
static int es_cb(short *w, int n, espeak_EVENT *ev) {
    if (!w) return 0;
    emit(w, n, es_rate);
    return g_abort ? 1 : 0;
}
#endif

/* ---------------- рабочий поток ---------------- */
static int worker(SceSize args, void *argp) {
#ifdef HAVE_PICO
    pico_setup();
#endif
#ifdef HAVE_ESPEAK
    es_rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, "app0:", 0);
    if (es_rate > 0) { espeak_SetSynthCallback(es_cb); es_ok = 1; }
#endif
    g_ready = 1;
    char last[32] = "";
    for (;;) {
        if (!g_req) { sceKernelDelayThread(2000); continue; }
        g_busy = 1; g_req = 0;
        char t[HL], buf[HL + 100];
        snprintf(t, sizeof t, "%s", rq_text);
        out_begin();
        if (rq_eng == 0) {
#ifdef HAVE_PICO
            int v = atoi(rq_voice) & 1;
            if (pico_ok[v]) {
                snprintf(buf, sizeof buf, "<speed level='%d'><pitch level='%d'>%s</pitch></speed>", rq_speed, rq_pitch, t);
                pico_say(v, buf);
            }
#endif
        } else {
#ifdef HAVE_ESPEAK
            if (es_ok) {
                if (strcmp(last, rq_voice)) { espeak_SetVoiceByName(rq_voice); snprintf(last, sizeof last, "%s", rq_voice); }
                int wpm = 170 * rq_speed / 100; if (wpm < 80) wpm = 80; if (wpm > 450) wpm = 450;
                int pt = 50 * rq_pitch / 100; if (pt < 0) pt = 0; if (pt > 99) pt = 99;
                espeak_SetParameter(espeakRATE, wpm, 0);
                espeak_SetParameter(espeakPITCH, pt, 0);
                espeak_Synth(t, strlen(t) + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);
            }
#endif
        }
        out_finish();
        g_busy = 0;
    }
    return 0;
}

/* ---------------- утилиты текста ---------------- */
static int dec(const unsigned char **p) {
    unsigned c = *(*p)++;
    if (c < 0x80) return c;
    int n = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : 1;
    unsigned cp = c & (0x3F >> n);
    while (n-- && **p) cp = (cp << 6) | (*(*p)++ & 0x3F);
    return cp;
}

/* 0 = латиница/англ, 1 = кириллица, 2 = японский */
static int detect(const char *s) {
    int cy = 0, jp = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        int c = dec(&p);
        if (c >= 0x400 && c <= 0x52F) cy++;
        else if ((c >= 0x3040 && c <= 0x30FF) || (c >= 0x4E00 && c <= 0x9FFF) || (c >= 0xFF66 && c <= 0xFF9F)) jp++;
    }
    if (jp && jp >= cy) return 2;
    if (cy) return 1;
    return 0;
}

static void u8to16(const char *s, uint16_t *d, int max) {
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p && n < max - 2) {
        unsigned c = dec(&p);
        if (c >= 0x10000) { c -= 0x10000; d[n++] = 0xD800 | (c >> 10); d[n++] = 0xDC00 | (c & 0x3FF); }
        else d[n++] = c;
    }
    d[n] = 0;
}

static void u16to8(const uint16_t *s, char *d, int max) {
    int n = 0;
    while (*s && n < max - 5) {
        unsigned c = *s++;
        if (c >= 0xD800 && c < 0xDC00 && *s >= 0xDC00 && *s < 0xE000) c = 0x10000 + ((c - 0xD800) << 10) + (*s++ - 0xDC00);
        if (c < 0x80) d[n++] = c;
        else if (c < 0x800) { d[n++] = 0xC0 | (c >> 6); d[n++] = 0x80 | (c & 0x3F); }
        else if (c < 0x10000) { d[n++] = 0xE0 | (c >> 12); d[n++] = 0x80 | ((c >> 6) & 0x3F); d[n++] = 0x80 | (c & 0x3F); }
        else { d[n++] = 0xF0 | (c >> 18); d[n++] = 0x80 | ((c >> 12) & 0x3F); d[n++] = 0x80 | ((c >> 6) & 0x3F); d[n++] = 0x80 | (c & 0x3F); }
    }
    d[n] = 0;
}

static void clean(char *s) {
    for (char *p = s; *p; p++) if (*p == '<' || *p == '>' || *p == '&' || *p == '\n' || *p == '\r' || *p == '\t') *p = ' ';
    char *a = s; while (*a == ' ') a++;
    memmove(s, a, strlen(a) + 1);
    int n = strlen(s); while (n > 0 && s[n - 1] == ' ') s[--n] = 0;
}

static void clip(const char *s, char *d, int max) {
    int n = strlen(s);
    if (n <= max) { strcpy(d, s); return; }
    int k = max;
    while (k > 0 && (((unsigned char)s[k]) & 0xC0) == 0x80) k--;
    memcpy(d, s, k); strcpy(d + k, "...");
}

/* ---------------- голоса / история ---------------- */
typedef struct { const char *label; int eng; int arg; } Voice;
static Voice ev[4];
static int nev, cur;
static int speed = 100, pitch = 100;
static char hist[HN][HL];
static int hn, sel;

static void hist_save(void) {
    FILE *f = fopen("ux0:data/vitatts.txt", "w");
    if (!f) return;
    for (int i = 0; i < hn; i++) fprintf(f, "%s\n", hist[i]);
    fclose(f);
}
static void hist_load(void) {
    FILE *f = fopen("ux0:data/vitatts.txt", "r");
    if (!f) return;
    char b[HL];
    while (hn < HN && fgets(b, sizeof b, f)) {
        b[strcspn(b, "\r\n")] = 0;
        if (b[0]) snprintf(hist[hn++], HL, "%s", b);
    }
    fclose(f);
}
static void hist_add(const char *t) {
    int k = -1;
    for (int i = 0; i < hn; i++) if (!strcmp(hist[i], t)) k = i;
    if (k < 0) { if (hn < HN) hn++; k = hn - 1; }
    for (int i = k; i > 0; i--) snprintf(hist[i], HL, "%s", hist[i - 1]);
    snprintf(hist[0], HL, "%s", t);
    sel = 0;
    hist_save();
}

static void speak(const char *t) {
    if (!g_ready || !t[0]) return;
    int lang = detect(t), eng;
    char vn[32];
    if (lang == 1) { eng = 1; strcpy(vn, "ru"); }
    else if (lang == 2) { eng = 1; strcpy(vn, "ja"); }
    else {
        eng = ev[cur].eng;
        if (eng == 0) snprintf(vn, sizeof vn, "%d", ev[cur].arg); else strcpy(vn, "en-us");
    }
    g_abort = 1;
    g_flush = 1;
    while (g_busy) sceKernelDelayThread(500);
    snprintf(rq_text, sizeof rq_text, "%s", t);
    snprintf(rq_voice, sizeof rq_voice, "%s", vn);
    rq_eng = eng; rq_speed = speed; rq_pitch = pitch;
    g_abort = 0;
    g_req = 1;
}

/* ---------------- интерфейс ---------------- */
static vita2d_pvf *font;

static void T(int x, int y, unsigned c, float s, const char *fmt, ...) {
    char b[700];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    vita2d_pvf_draw_text(font, x, y, c, s, b);
}

static int ime(const char *init, char *out, int outsz) {
    static uint16_t title[] = {'T', 'e', 'x', 't', 0};
    static uint16_t ini[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1], buf[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
    u8to16(init, ini, 125);
    memset(buf, 0, sizeof buf);
    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = 0;
    p.languagesForced = 0;
    p.type = SCE_IME_TYPE_DEFAULT;
    p.option = 0;
    p.title = title;
    p.maxTextLength = 120;
    p.initialText = ini;
    p.inputTextBuffer = buf;
    if (sceImeDialogInit(&p) < 0) return 0;
    while (sceImeDialogGetStatus() != SCE_COMMON_DIALOG_STATUS_FINISHED) {
        vita2d_start_drawing();
        vita2d_clear_screen();
        vita2d_end_drawing();
        vita2d_common_dialog_update();
        vita2d_swap_buffers();
    }
    SceImeDialogResult r;
    memset(&r, 0, sizeof r);
    sceImeDialogGetResult(&r);
    int ok = (r.button == SCE_IME_DIALOG_BUTTON_ENTER);
    sceImeDialogTerm();
    if (!ok) return 0;
    u16to8(buf, out, outsz);
    clean(out);
    return out[0] != 0;
}

#define COL_BG   RGBA8(18, 18, 28, 255)
#define COL_TXT  RGBA8(235, 235, 235, 255)
#define COL_DIM  RGBA8(130, 130, 150, 255)
#define COL_ACC  RGBA8(0, 230, 170, 255)
#define COL_BOX  RGBA8(40, 40, 60, 255)
#define COL_SEL  RGBA8(0, 90, 80, 255)

int main(void) {
    vita2d_init();
    vita2d_set_clear_color(COL_BG);
    font = vita2d_load_custom_pvf("sa0:data/font/pvf/jpn0.pvf");
    if (!font) font = vita2d_load_default_pvf();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);

#ifdef HAVE_PICO
    ev[nev++] = (Voice){"Pico US (SVOX)", 0, 0};
    ev[nev++] = (Voice){"Pico UK (SVOX)", 0, 1};
#endif
#ifdef HAVE_ESPEAK
    ev[nev++] = (Voice){"eSpeak EN (retro)", 1, 0};
#endif
    hist_load();

    aport = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, GRAIN, 48000, SCE_AUDIO_OUT_MODE_MONO);
    SceUID ath = sceKernelCreateThread("aud", audio_thread, 96, 0x10000, 0, 0, NULL);
    if (ath < 0) ath = sceKernelCreateThread("aud", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(ath, 0, NULL);

    SceUID th = sceKernelCreateThread("tts", worker, 0x10000100, 0x100000, 0, 0, NULL);
    sceKernelStartThread(th, 0, NULL);

    SceCtrlData pad;
    unsigned old = 0;
    int tprev = 0;
    char buf[HL];

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pr = pad.buttons & ~old;
        old = pad.buttons;

        SceTouchData td;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
        int tnow = td.reportNum > 0;
        int tap = tnow && !tprev, tx = 0, ty = 0;
        if (tap) { tx = td.report[0].x / 2; ty = td.report[0].y / 2; }
        tprev = tnow;
        (void)tx;

        int do_new = 0, do_edit = 0;
        if (pr & SCE_CTRL_START) break;
        if (pr & SCE_CTRL_LTRIGGER) cur = (cur + nev - 1) % nev;
        if (pr & SCE_CTRL_RTRIGGER) cur = (cur + 1) % nev;
        if (pr & SCE_CTRL_UP) { if (sel > 0) sel--; }
        if (pr & SCE_CTRL_DOWN) { if (sel < hn - 1) sel++; }
        if (pr & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
            int d = (pr & SCE_CTRL_RIGHT) ? 10 : -10;
            if (pad.buttons & SCE_CTRL_SELECT) { pitch += d; if (pitch < 50) pitch = 50; if (pitch > 200) pitch = 200; }
            else { speed += d; if (speed < 50) speed = 50; if (speed > 250) speed = 250; }
        }
        if (pr & SCE_CTRL_SQUARE) { g_abort = 1; g_flush = 1; }
        if ((pr & SCE_CTRL_CIRCLE) && hn) speak(hist[sel]);
        if (pr & SCE_CTRL_CROSS) do_new = 1;
        if ((pr & SCE_CTRL_TRIANGLE) && hn) do_edit = 1;
        if (tap) {
            if (ty < 160) do_new = 1;
            else if (ty >= 170 && (ty - 170) / 58 < hn) { sel = (ty - 170) / 58; speak(hist[sel]); }
        }

        if (do_new || do_edit) {
            if (ime(do_edit ? hist[sel] : "", buf, sizeof buf)) { hist_add(buf); speak(buf); }
            sceCtrlPeekBufferPositive(0, &pad, 1);
            old = pad.buttons;
            tprev = 1;
            continue;
        }

        /* --- рисование --- */
        vita2d_start_drawing();
        vita2d_clear_screen();
        T(20, 34, COL_ACC, 1.3f, "VitaTTS");
        T(180, 34, COL_DIM, 0.9f, "%s", g_ready ? ((g_busy || g_play) ? "SPEAKING" : "ready") : "loading...");
        T(20, 66, COL_TXT, 0.95f, "EN voice (L/R): %s", ev[cur].label);
        T(20, 92, COL_TXT, 0.95f, "Speed %d%%   Pitch %d%%   RU/JA auto-detect: %s", speed, pitch,
#ifdef HAVE_ESPEAK
          "eSpeak"
#else
          "unavailable"
#endif
        );
        vita2d_draw_rectangle(20, 110, 920, 46, COL_BOX);
        T(34, 142, COL_ACC, 1.1f, "[X] / tap here: type a phrase and speak");
        for (int i = 0; i < HN; i++) {
            int y = 170 + i * 58;
            vita2d_draw_rectangle(20, y, 920, 52, (i == sel && hn) ? COL_SEL : COL_BOX);
            if (i < hn) {
                char c[HL];
                clip(hist[i], c, 70);
                T(32, y + 34, COL_TXT, 1.1f, "%d  %s", i + 1, c);
            } else T(32, y + 34, COL_DIM, 1.0f, "%d  -", i + 1);
        }
        T(20, 504, COL_DIM, 0.8f, "X new  O repeat  Triangle edit  Square stop  Up/Down history  Left/Right speed");
        T(20, 528, COL_DIM, 0.8f, "Select+Left/Right pitch   L/R English voice   tap a row = speak   START exit");
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    g_abort = 1;
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
