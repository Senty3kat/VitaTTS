// VitaTTS v2.1 - быстрый TTS для PS Vita (Pico/SVOX и SAM C64 для английского, RHVoice для русского, eSpeak-NG для RU/JA/EN-retro)
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>
#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/audioout.h>
#include <psp2/ime_dialog.h>
#include <psp2/common_dialog.h>
#include <vita2d.h>
#include "xlate.h"

#ifdef HAVE_PICO
#include "picoapi.h"
#include "picodefs.h"
#endif
#ifdef HAVE_ESPEAK
#include <espeak-ng/speak_lib.h>
#endif
#ifdef HAVE_RHVOICE
#include "RHVoice.h"
#endif
#ifdef HAVE_SAM
/* SAM (Software Automatic Mouth, C64): прототипы объявляем сами, чтобы не тащить sam.h с его глобалами */
extern void SetInput(char *);
extern void SAM_SetSpeed(unsigned char);   /* в SAM переименована при сборке: у eSpeak тоже есть SetSpeed */
extern void SetPitch(unsigned char);
extern void SetMouth(unsigned char);
extern void SetThroat(unsigned char);
extern int SAMMain(void);
extern char *GetBuffer(void);
extern int GetBufferLength(void);
extern int TextToPhonemes(unsigned char *);
int debug __attribute__((weak)) = 0;
#endif

static int dec(const unsigned char **p);
static void tr_cyr2lat(const char *s, char *o, int om);
static void tr_lat2cyr(const char *s, char *o, int om);

int _newlib_heap_size_user = 150 * 1024 * 1024;   /* Pico 2x20 МБ + RHVoice ~40 МБ + буферы */

#define GRAIN 512
#define HN 5
#define HL 2048          /* байт на фразу */
#define IMEMAX 500       /* символов в окне ввода */

/* ---------------- состояние синтеза (общее для потоков) ---------------- */
static volatile int g_abort, g_req, g_busy, g_ready;
static char rq_text[HL];
static int rq_cap;
static int rq_eng, rq_speed = 100, rq_pitch = 100;   /* rq_eng: 0 pico, 1 espeak */
static double pitchf = 1.0;
static char rq_voice[40];
#define NPICO 6
static int pico_ok[NPICO], pico_err[NPICO], es_ok;
static int rq_tlang = -1;                          /* язык голоса для перевода «чужих» слов (XL_*), -1 — без перевода */
static volatile int g_first_ms;                    /* задержка до первого звука */
static volatile uint64_t g_req_t;
static int es_rc, es_get;
static volatile int g_synth_ms, g_audio_ms, g_under;

/* ---------------- слоговый режим: общие данные ---------------- */
#define PHR_MAX (48000 * 24)
#define MAXSYL 160
static short phr[PHR_MAX];                 /* готовая фраза 48 кГц */
static volatile int phn, capture;
static int sylb[MAXSYL + 2];               /* границы слогов (в сэмплах) */
static volatile int nsyl, sy_idx, g_sy_ready, g_sy_last = -1;
static volatile int g_sylmode, trig_pending, auto_run, quant, g_resync;
static volatile int bpm = 120, subdiv = 1, slices = 8;   /* slices: на сколько кусков резать фразу */
static volatile int fullsyl;      /* слоги проговариваются до конца: новый запуск не обрывает текущий */
static volatile unsigned long long g_tick_t;
static float gain = 1.0f;     /* громкость */

/* ---------------- вывод звука: кольцевой буфер + отдельный аудиопоток ---------------- */
#define RING (1 << 20)          /* ~21 c при 48 кГц */
#define PREBUF 19200            /* 0.4 c предбуфер */
#define GMAX 2048
static int aport = -1;
static int agrain = 512;
static volatile int g_reopen = 1;
static short ring[RING] __attribute__((aligned(64)));
static short bufs[3][GMAX] __attribute__((aligned(64)));
static volatile unsigned rhead, rtail;
static volatile int g_flush, g_done, g_play;

static int a_fail;
/* вывод буфера; при ошибках порта (звук «отвалился» после диалога, сна и т.п.) порт открывается заново */
static void aout(short *b) {
    int r = aport >= 0 ? sceAudioOutOutput(aport, b) : -1;
    if (r < 0) {
        sceKernelDelayThread(3000);
        if (++a_fail >= 10) { g_reopen = 1; a_fail = 0; }
    } else a_fail = 0;
}

static void audio_open(void) {
    if (aport >= 0) { sceAudioOutReleasePort(aport); aport = -1; }
    aport = sceAudioOutOpenPort(SCE_AUDIO_OUT_PORT_TYPE_MAIN, agrain, 48000, SCE_AUDIO_OUT_MODE_MONO);
}

/* Один «грейн» в слоговом режиме: опрос кнопок, сетка BPM, запуск следующего слога */
static void sy_grain(int *bip) {
    static int pdown = 0, vpos = 0, vend = 0, vfi = 0, ppos = 0, pleft = 0, ptot = 1;
    static double next_tick = 0;
    static long long clk = 0;
    short *ab = bufs[*bip];
    int G = agrain;
    SceCtrlData cd;
    SceTouchData tb;
    memset(&cd, 0, sizeof cd);
    memset(&tb, 0, sizeof tb);
    sceCtrlPeekBufferPositive(0, &cd, 1);
    sceTouchPeek(SCE_TOUCH_PORT_BACK, &tb, 1);
    unsigned fb = SCE_CTRL_CROSS | SCE_CTRL_CIRCLE | SCE_CTRL_SQUARE | SCE_CTRL_TRIANGLE;
    int down = ((cd.buttons & fb) && !(cd.buttons & SCE_CTRL_SELECT)) || tb.reportNum > 0;
    if (down && !pdown) trig_pending = 1;
    pdown = down;
    if (g_resync) { next_tick = (double)clk; g_resync = 0; }
    double tl = 48000.0 * 60.0 / (double)(bpm * subdiv);
    int ready = g_sy_ready && nsyl > 0;
    if (!ready) { trig_pending = 0; vpos = vend = 0; pleft = 0; }
    for (int i = 0; i < G; i++) {
        int tick = 0, fire = 0;
        double t = (double)(clk + i);
        if (t >= next_tick) {
            tick = 1;
            next_tick += tl;
            if (next_tick <= t) next_tick = t + tl;
            g_tick_t = sceKernelGetProcessTimeWide();
        }
        if (ready) {
            if (auto_run) {
                if (trig_pending) { sy_idx = 0; trig_pending = 0; }   /* тап в авто-режиме = перезапуск с первого слога */
                if (tick) fire = 1;
            } else if (trig_pending && (!quant || tick)) fire = 1;
        }
        if (fire) {
            trig_pending = 0;
            int idx = sy_idx % nsyl;
            if (vpos < vend) {
                /* FULL: прошлый слог доигрывает поверх нового (ритм не сбивается); иначе — короткое затухание */
                if (fullsyl) { ppos = vpos; pleft = vend - vpos; ptot = -1; }
                else { ppos = vpos; ptot = vend - vpos; if (ptot > 96) ptot = 96; pleft = ptot; }
            }
            vpos = sylb[idx]; vend = sylb[idx + 1]; vfi = 0;
            g_sy_last = idx;
            sy_idx = (idx + 1) % nsyl;
        }
        float sm = 0;
        if (vpos < vend) {
            float f = 1.0f;
            if (vfi < 64) { f = vfi / 64.0f; vfi++; }
            int left = vend - vpos;
            if (left < 128) f *= left / 128.0f;
            sm += phr[vpos++] * f;
        }
        if (pleft > 0) {
            float pf2 = ptot > 0 ? (float)pleft / ptot : (pleft < 128 ? pleft / 128.0f : 1.0f);
            sm += phr[ppos++] * pf2; pleft--;
        }
        sm *= gain;
        ab[i] = sm > 32767.0f ? 32767 : (sm < -32768.0f ? -32768 : (short)sm);
    }
    clk += G;
    aout(ab);
    *bip = (*bip + 1) % 3;
}

static int audio_thread(SceSize args, void *argp) {
    int bi = 0, playing = 0;
    for (;;) {
        if (g_reopen) { audio_open(); g_reopen = 0; playing = 0; }
        if (g_flush) { rtail = rhead; playing = 0; g_play = 0; g_flush = 0; continue; }
        if (g_sylmode) { sy_grain(&bi); continue; }
        unsigned avail = rhead - rtail;
        int G = agrain;
        if (!playing) {
            if (avail >= PREBUF || (g_done && avail > 0)) {
                playing = 1;
                if (!g_first_ms && g_req_t) g_first_ms = (int)((sceKernelGetProcessTimeWide() - g_req_t) / 1000) + 1;
            }
            else { sceKernelDelayThread(1000); continue; }
        }
        g_play = 1;
        short *ab = bufs[bi];
        int n = 0;
        if (avail >= (unsigned)G) n = G;
        else if (g_done) { avail = rhead - rtail; n = avail >= (unsigned)G ? G : (int)avail; }
        else {
            /* недогруз: тишина вместо мусора, затем заново набираем предбуфер */
            g_under++;
            memset(ab, 0, G * sizeof(short));
            aout(ab);
            bi = (bi + 1) % 3; playing = 0;
            continue;
        }
        __sync_synchronize();
        for (int i = 0; i < G; i++) ab[i] = i < n ? ring[(rtail + i) & (RING - 1)] : 0;
        rtail += n;
        if (n > 0) aout(ab);
        bi = (bi + 1) % 3;               /* тройная буферизация */
        if (n < G) { playing = 0; g_play = 0; }
    }
    return 0;
}

/* ---------------- ресемплер: оконный sinc, N Гц -> 48000 Гц ---------------- */
#define SK 8
#define SP 128
#define XMAX 4096
static float ker[SP][2 * SK];
static short hist_in[2 * SK];
static double rt;
static int cur_rate = 16000;

static void ker_init(void) {
    const float PI = 3.14159265f;
    for (int p = 0; p < SP; p++) {
        float fr = (float)p / SP, sum = 0;
        for (int j = 0; j < 2 * SK; j++) {
            float d = (float)(j - (SK - 1)) - fr, w;
            if (fabsf(d) < 1e-6f) w = 1.0f; else { float px = PI * d; w = sinf(px) / px; }
            float ad = fabsf(d) / SK;
            float win = ad >= 1.0f ? 0.0f : (0.42f + 0.5f * cosf(PI * ad) + 0.08f * cosf(2 * PI * ad));
            ker[p][j] = w * win;
            sum += ker[p][j];
        }
        for (int j = 0; j < 2 * SK; j++) ker[p][j] /= sum;
    }
}

static void out_begin(void) {
    rt = SK - 1;
    memset(hist_in, 0, sizeof hist_in);
    g_done = 0;
    g_flush = 1;
    for (int i = 0; g_flush && i < 300; i++) sceKernelDelayThread(1000);
}

static int emit(const short *p, int n, int rate) {
    static short tmp[2 * SK + XMAX];
    if (n <= 0) return g_abort;
    cur_rate = rate;
    double step = (double)rate * pitchf / 48000.0;
    unsigned h = rhead;
    while (n > 0) {
        int m = n > XMAX ? XMAX : n;
        memcpy(tmp, hist_in, 2 * SK * sizeof(short));
        memcpy(tmp + 2 * SK, p, m * sizeof(short));
        while (rt < SK + m) {
            if (g_abort) { __sync_synchronize(); rhead = h; return 1; }
            if (!capture && h - rtail >= RING) {
                __sync_synchronize(); rhead = h;
                while (h - rtail >= RING && !g_abort) sceKernelDelayThread(1000);
                continue;
            }
            int i = (int)rt;
            int ph = (int)((rt - i) * SP);
            const float *k = ker[ph];
            const short *x = tmp + i - (SK - 1);
            float acc = 0;
            for (int j = 0; j < 2 * SK; j++) acc += k[j] * x[j];
            if (!capture) acc *= gain;
            short sv = acc > 32767.0f ? 32767 : (acc < -32768.0f ? -32768 : (short)acc);
            if (capture) { if (phn < PHR_MAX) phr[phn++] = sv; }
            else { ring[h & (RING - 1)] = sv; h++; }
            rt += step;
        }
        memcpy(hist_in, tmp + m, 2 * SK * sizeof(short));
        rt -= m; p += m; n -= m;
    }
    __sync_synchronize();
    rhead = h;
    return g_abort;
}

static void out_finish(void) {
    if (!g_abort) { short z[2 * SK] = {0}; emit(z, 2 * SK, cur_rate); }
    __sync_synchronize();
    if (capture) return;
    if (g_abort) g_flush = 1; else g_done = 1;
}

/* ---------------- нарезка на слоги по огибающей громкости ---------------- */
static void sy_analyze(void) {
    const int H = 240;                         /* 5 мс */
    static float env[PHR_MAX / 240 + 8], e2[PHR_MAX / 240 + 8];
    int nf = phn / H;
    nsyl = 0;
    if (nf < 4) return;
    for (int f = 0; f < nf; f++) {
        double sum = 0;
        const short *p = phr + f * H;
        for (int i = 0; i < H; i++) sum += (double)p[i] * p[i];
        env[f] = (float)sqrt(sum / H);
    }
    float M = 0;
    for (int f = 0; f < nf; f++) {
        float a = 0; int c = 0;
        for (int k = -2; k <= 2; k++) { int j = f + k; if (j >= 0 && j < nf) { a += env[j]; c++; } }
        e2[f] = a / c;
        if (e2[f] > M) M = e2[f];
    }
    if (M < 30) return;
    float quiet = 0.04f * M;
    int s0 = 0; while (s0 < nf && e2[s0] < quiet) s0++;
    int e0 = nf - 1; while (e0 > s0 && e2[e0] < quiet) e0--;
    if (e0 <= s0) return;
    /* Режем на slices кусков: границы — самые глубокие провалы громкости (кандидаты — локальные минимумы),
       не ближе MINF кадров друг к другу. Если провалов мало — делим самые длинные куски в их самом тихом месте. */
    int want = slices - 1;
    if (want > MAXSYL - 1) want = MAXSYL - 1;
    int MINF = (e0 - s0) / (want + 1) / 3;      /* куски не короче трети среднего, и не меньше 40 мс */
    if (MINF < 8) MINF = 8;
    static int cf[PHR_MAX / 240 + 8];
    static float cs[PHR_MAX / 240 + 8];
    int nc = 0;
    for (int f = s0 + MINF; f <= e0 - MINF; f++) {
        if (e2[f] > e2[f - 1] || e2[f] > e2[f + 1]) continue;
        float l = 0, r = 0;
        for (int k = f - 30; k <= f; k++) if (k >= s0 && e2[k] > l) l = e2[k];
        for (int k = f; k <= f + 30; k++) if (k <= e0 && e2[k] > r) r = e2[k];
        int g = f;                              /* граница = начало звука, а не начало паузы */
        while (g < e0 - MINF && e2[g] < quiet) g++;
        cf[nc] = g; cs[nc] = (l < r ? l : r) - e2[f]; nc++;
    }
    int b[MAXSYL + 2], nb = 0;
    b[nb++] = s0;
    while (nb - 1 < want) {                     /* жадно: самый глубокий подходящий провал */
        int best = -1;
        for (int i = 0; i < nc; i++) {
            if (cs[i] < 0) continue;
            int ok = 1;
            for (int j = 0; j < nb; j++) if (abs(cf[i] - b[j]) < MINF) { ok = 0; break; }
            if (ok && (best < 0 || cs[i] > cs[best])) best = i;
        }
        if (best < 0) break;
        b[nb++] = cf[best]; cs[best] = -1;
    }
    for (int i = 1; i < nb; i++) for (int j = i; j > 1 && b[j] < b[j - 1]; j--) { int t = b[j]; b[j] = b[j - 1]; b[j - 1] = t; }
    while (nb - 1 < want) {                     /* провалов не хватило: делим самый длинный кусок */
        int bi = -1, bl = 0;
        for (int i = 0; i < nb; i++) { int e = (i + 1 < nb) ? b[i + 1] : e0 + 1; if (e - b[i] > bl) { bl = e - b[i]; bi = i; } }
        if (bi < 0 || bl < 2 * MINF) break;
        int e = (bi + 1 < nb) ? b[bi + 1] : e0 + 1, m = b[bi] + bl / 2;
        for (int f = b[bi] + MINF; f < e - MINF; f++) if (e2[f] < e2[m]) m = f;
        while (m < e - MINF && e2[m] < quiet) m++;
        for (int j = nb; j > bi + 1; j--) b[j] = b[j - 1];
        b[bi + 1] = m; nb++;
    }
    b[nb] = e0 + 1;
    for (int i = 0; i <= nb; i++) sylb[i] = b[i] * H;
    if (sylb[nb] > phn) sylb[nb] = phn;
    nsyl = nb;
}

static void sy_reanalyze(void) {
    if (!g_sy_ready) return;
    g_sy_ready = 0;
    sceKernelDelayThread(30000);
    sy_analyze();
    sy_idx = 0; g_sy_last = -1;
    __sync_synchronize();
    g_sy_ready = 1;
}

/* ---------------- Pico (SVOX): отдельная система на каждый голос ---------------- */
#ifdef HAVE_PICO
#define PICO_MEM (6 * 1024 * 1024)                 /* на компьютере голосу хватает 3 МБ */
static pico_System ps[NPICO];
static pico_Engine pe[NPICO];

static int pico_load1(int v, int mem_sz) {
    static const char *vn[NPICO] = {"VUS", "VGB", "VDE", "VES", "VFR", "VIT"};
    static const char *fl[NPICO][2] = {
        {"app0:/en-US_ta.bin", "app0:/en-US_lh0_sg.bin"},
        {"app0:/en-GB_ta.bin", "app0:/en-GB_kh0_sg.bin"},
        {"app0:/de-DE_ta.bin", "app0:/de-DE_gl0_sg.bin"},
        {"app0:/es-ES_ta.bin", "app0:/es-ES_zl0_sg.bin"},
        {"app0:/fr-FR_ta.bin", "app0:/fr-FR_nk0_sg.bin"},
        {"app0:/it-IT_ta.bin", "app0:/it-IT_cm0_sg.bin"}};
    void *mem = malloc(mem_sz);
    if (!mem) return -1;
    int rc = pico_initialize(mem, mem_sz, &ps[v]);
    if (rc) { free(mem); return rc; }
    rc = pico_createVoiceDefinition(ps[v], (const pico_Char *)vn[v]);
    for (int k = 0; k < 2 && !rc; k++) {
        pico_Resource r;
        pico_Char nm[PICO_MAX_RESOURCE_NAME_SIZE];
        rc = pico_loadResource(ps[v], (const pico_Char *)fl[v][k], &r);
        if (!rc) rc = pico_getResourceName(ps[v], r, (char *)nm);
        if (!rc) rc = pico_addResourceToVoiceDefinition(ps[v], (const pico_Char *)vn[v], nm);
    }
    if (!rc) rc = pico_newEngine(ps[v], (const pico_Char *)vn[v], &pe[v]);
    if (rc) { pico_terminate(&ps[v]); free(mem); }
    return rc;
}

static int pico_load(int v) {
    int rc = pico_load1(v, PICO_MEM);
    if (rc) rc = pico_load1(v, 3 * PICO_MEM);          /* запас, если 6 МБ на Vita не хватит */
    return rc;
}

static void pico_say(int v, const char *txt) {
    static short buf[1024];
    pico_Engine e = pe[v];
    pico_resetEngine(e, 0);
    const pico_Char *in = (const pico_Char *)txt;
    pico_Int16 rem = (pico_Int16)(strlen(txt) + 1), sent = 0, got = 0, dt = 0;
    int guard = 0;
    while (rem > 0 && !g_abort && guard++ < 5000) {
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

/* ---------------- SAM (Commodore 64) ---------------- */
#ifdef HAVE_SAM
/* SAM: классический C64-голос + русский «по правилам» (без словарей) */
static void sam_run(char *in, int es) {
    static short pcm[240000];
    int sp = 72 * 100 / (es > 20 ? es : 20);
    if (sp < 20) sp = 20; if (sp > 255) sp = 255;
    SAM_SetSpeed((unsigned char)sp);
    SetPitch(64);
    SetMouth(128);
    SetThroat(128);
    SetInput(in);
    SAMMain();
    unsigned char *b = (unsigned char *)GetBuffer();
    int cnt = GetBufferLength() / 50;
    if (!b || cnt <= 0 || cnt > 220000) return;
    b[0] = 128;                                          /* первый отсчёт буфера SAM не инициализирован: щелчок */
    for (int i = 0; i < cnt; i++) {
        int v = ((int)b[i] - 128) * 200;     /* постоянный уровень: без «качания» громкости между кусками */
        pcm[i] = (short)(v > 30000 ? 30000 : (v < -30000 ? -30000 : v));
    }
    {   /* мягкий ФНЧ 2-го порядка (22050 Гц): срезает 8-битный «мусор», как выходной каскад C64 */
        const float b0 = 0.3804f, b1 = 0.7608f, b2 = 0.3804f, a1 = 0.3307f, a2 = 0.1910f;   /* Баттерворт 6.5 кГц */
        float x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        for (int i = 0; i < cnt; i++) {
            float x = pcm[i], y = b0 * x + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            pcm[i] = (short)(y > 32767 ? 32767 : (y < -32768 ? -32768 : y));
        }
    }
    for (int i = 0; i < 80 && i < cnt / 2; i++) {          /* короткие плавные края куска: без щелчков на стыках */
        float f = i / 80.0f;
        pcm[i] = (short)(pcm[i] * f);
        pcm[cnt - 1 - i] = (short)(pcm[cnt - 1 - i] * f);
    }
    emit(pcm, cnt, 22050);
}

static void sam_say(const char *txt, int es) {
    static char in[256];
    /* reciter разворачивает текст в фонемы в том же 256-байтовом буфере и переполняет его на длинных кусках,
       поэтому режем на короткие куски */
    const int CH = 70;
    int len = strlen(txt), pos = 0;
    while (pos < len && !g_abort) {
        int e = pos + CH;
        if (e >= len) e = len;
        else { int k = e; while (k > pos + 20 && txt[k] != ' ') k--; if (txt[k] == ' ') e = k; }
        int n = 0, lastsp = 1;
        for (int i = pos; i < e && n < CH + 4; i++) {
            unsigned char c = (unsigned char)txt[i];
            if (c >= 128) continue;
            c = (unsigned char)toupper(c);
            char o;
            if (c == '\'') continue;                         /* don't -> DONT */
            if ((c >= 'A' && c <= 'Z') || c == '.' || c == ',' || c == '?' || c == '!' || c == '-') o = (char)c;
            else if (c >= '0' && c <= '9') o = (char)c;
            else o = ' ';
            if (o == ' ' && lastsp) continue;
            lastsp = (o == ' ');
            in[n++] = o;
        }
        pos = e;
        while (n > 0 && in[n - 1] == ' ') n--;
        if (n == 0) continue;
        in[n++] = '[';
        in[n] = 0;
        if (!TextToPhonemes((unsigned char *)in)) continue;
        sam_run(in, es);
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

/* ---------------- RHVoice (русский): HTS-синтез, данные в app0:rh ---------------- */
#ifdef HAVE_RHVOICE
static RHVoice_tts_engine rh_eng;
static int rh_ok, rh_nv, rh_sr = 24000;
static int rh_set_sr(int sr, void *u) { rh_sr = sr; return 1; }
/* возвращаем 0, чтобы остановить синтез при новой фразе/стопе */
static int rh_play(const short *smp, unsigned n, void *u) { return emit(smp, (int)n, rh_sr) ? 0 : 1; }

static void rh_init(void) {
    static const char *rp[] = {NULL};
    RHVoice_init_params ip;
    memset(&ip, 0, sizeof ip);
    ip.data_path = "app0:rh";
    ip.config_path = "ux0:data";
    ip.resource_paths = rp;
    ip.callbacks.set_sample_rate = rh_set_sr;
    ip.callbacks.play_speech = rh_play;
    ip.options = RHVoice_preload_voices;
    rh_eng = RHVoice_new_tts_engine(&ip);
    if (rh_eng) { rh_nv = (int)RHVoice_get_number_of_voices(rh_eng); rh_ok = rh_nv > 0; }
}

static void rh_say(const char *voice, const char *txt, int es) {
    if (!rh_ok) return;
    RHVoice_synth_params sp;
    memset(&sp, 0, sizeof sp);
    sp.voice_profile = voice;
    double r = es / 100.0;
    sp.relative_rate = r < 0.2 ? 0.2 : (r > 5.0 ? 5.0 : r);
    sp.flags = RHVoice_synth_flag_dont_clip_rate;      /* иначе RHVoice режет скорость до 0.5..3 и питч начинает менять темп */
    sp.relative_pitch = 1.0;
    sp.relative_volume = 1.0;
    RHVoice_message m = RHVoice_new_message(rh_eng, txt, (unsigned)strlen(txt), RHVoice_message_text, &sp, NULL);
    if (!m) return;
    RHVoice_speak(m);
    RHVoice_delete_message(m);
}
#endif

/* ---------------- перевод «чужих» слов (см. xlate.h) ---------------- */
#ifdef HAVE_ESPEAK
static int ph_dirty;                               /* голос eSpeak сменён фонемайзером — перед синтезом выставить заново */
static const char *wk_phon(const char *w, int src) {
    static char out[600];
    if (!es_ok) return NULL;
    espeak_SetVoiceByName(src == XL_RU ? "ru" : "en-us");
    ph_dirty = 1;
    const void *p = w;
    out[0] = 0;
    for (int g = 0; p && g < 20; g++) {
        const char *r = espeak_TextToPhonemes(&p, espeakCHARS_UTF8, 0x02);      /* 0x02 = фонемы в IPA */
        if (r) strncat(out, r, sizeof out - strlen(out) - 1);
    }
    return out;
}
#endif
static void wk_fb(const char *w, int tl, char *o, int om) {
    if (tl == XL_RU) tr_lat2cyr(w, o, om); else tr_cyr2lat(w, o, om);
}

/* ---------------- рабочий поток синтеза ---------------- */
static int worker(SceSize args, void *argp) {
#ifdef HAVE_PICO
    for (int v = 0; v < NPICO; v++) { pico_err[v] = pico_load(v); pico_ok[v] = (pico_err[v] == 0); }
#endif
#ifdef HAVE_ESPEAK
    es_rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, "app0:", espeakINITIALIZE_DONT_EXIT);
    if (es_rate > 0) { espeak_SetSynthCallback(es_cb); es_ok = 1; }
#endif
#ifdef HAVE_RHVOICE
    rh_init();
#endif
    g_ready = 1;
    char last[40] = "";
    for (;;) {
        if (!g_req) { sceKernelDelayThread(2000); continue; }
        g_busy = 1; g_req = 0;
        char t[HL], buf[HL + 100];
        if (rq_tlang >= 0) {
            static char xt[HL * 3];
#ifdef HAVE_ESPEAK
            xl_text(rq_text, rq_tlang, xt, sizeof xt, wk_phon, wk_fb);
            if (ph_dirty) { last[0] = 0; ph_dirty = 0; }
#else
            xl_text(rq_text, rq_tlang, xt, sizeof xt, NULL, wk_fb);
#endif
            snprintf(t, sizeof t, "%s", xt);
        } else snprintf(t, sizeof t, "%s", rq_text);
        capture = rq_cap;
        if (capture) { g_sy_ready = 0; sceKernelDelayThread(30000); phn = 0; }
        double pf = rq_pitch / 100.0;
        pitchf = pf;
        int es = (int)(rq_speed / pf + 0.5);          /* компенсируем скорость, чтобы длительность не менялась */
        out_begin();
        unsigned h0 = rhead;
        uint64_t t0 = sceKernelGetProcessTimeWide();
        if (rq_eng == 0) {
#ifdef HAVE_PICO
            int v = atoi(rq_voice);
            if (v < 0 || v >= NPICO) v = 0;
            if (pico_ok[v]) {
                int lv = es < 20 ? 20 : (es > 500 ? 500 : es);
                snprintf(buf, sizeof buf, "<speed level='%d'>%s</speed>", lv, t);
                pico_say(v, buf);
            }
#endif
        } else if (rq_eng == 3) {
#ifdef HAVE_SAM
            sam_say(t, es);
#endif
        } else if (rq_eng == 5) {
#ifdef HAVE_RHVOICE
            rh_say(rq_voice, t, es);
#endif
        } else {
#ifdef HAVE_ESPEAK
            if (es_ok) {
                if (strcmp(last, rq_voice)) { espeak_SetVoiceByName(rq_voice); snprintf(last, sizeof last, "%s", rq_voice); }
                int wpm = 170 * es / 100; if (wpm < 80) wpm = 80; if (wpm > 450) wpm = 450;
                es_rc = espeak_SetParameter(espeakRATE, wpm, 0);
                espeak_SetParameter(espeakRANGE, 100, 0);
                es_get = espeak_GetParameter(espeakRATE, 1);
                espeak_Synth(t, strlen(t) + 1, 0, POS_CHARACTER, 0, espeakCHARS_UTF8, NULL, NULL);
            }
#endif
        }
        uint64_t t1 = sceKernelGetProcessTimeWide();
        out_finish();
        if (capture && !g_abort) { sy_analyze(); sy_idx = 0; g_sy_last = -1; __sync_synchronize(); g_sy_ready = 1; }
        g_synth_ms = (int)((t1 - t0) / 1000);
        g_audio_ms = (int)((rhead - h0) / 48);
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
/* lang: 0 английский, 1 русский; name — имя голоса RHVoice */
typedef struct { const char *label; int eng; int arg; int lang; const char *name; } Voice;
static Voice ev[16];
static int nev, cur;
static int speed = 100, pitch = 100, volume = 100;
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
    static char b[HL];
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

static void request(int eng, const char *t, const char *vn, int cap, int tlang) {
    g_abort = 1;
    g_flush = 1;
    while (g_busy) sceKernelDelayThread(500);
    snprintf(rq_text, sizeof rq_text, "%s", t);
    snprintf(rq_voice, sizeof rq_voice, "%s", vn);
    rq_eng = eng; rq_speed = speed; rq_pitch = pitch; rq_cap = cap; rq_tlang = tlang;
    g_first_ms = 0; g_req_t = sceKernelGetProcessTimeWide();
    g_abort = 0;
    g_req = 1;
}

/* Транслитерация: «чужой» голос читает текст как свой язык. Русский голос получает латиницу кириллицей
   («хелло»), английский — кириллицу латиницей («privet»). Работает по символам, смешанный текст тоже. */
static void tr_cyr2lat(const char *s, char *o, int om) {
    static const char *L[32] = {"a", "b", "v", "g", "d", "e", "zh", "z", "i", "y", "k", "l", "m", "n", "o", "p",
                                "r", "s", "t", "u", "f", "kh", "ts", "ch", "sh", "shch", "", "y", "", "e", "yu", "ya"};
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p && n < om - 6) {
        const unsigned char *q = p;
        int c = dec(&q);
        const char *r = NULL;
        if (c >= 0x410 && c <= 0x42F) r = L[c - 0x410];
        else if (c >= 0x430 && c <= 0x44F) r = L[c - 0x430];
        else if (c == 0x451 || c == 0x401) r = "yo";
        if (r) { while (*r && n < om - 1) o[n++] = *r++; }
        else { while (p < q && n < om - 1) o[n++] = (char)*p++; }
        p = q;
    }
    o[n] = 0;
}

static void tr_lat2cyr(const char *s, char *o, int om) {
    int n = 0;
#define PUT(u) do { const char *z_ = (u); while (*z_ && n < om - 1) o[n++] = *z_++; } while (0)
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < om - 8) {
        if (*p >= 128 || !isalpha(*p)) { o[n++] = (char)*p++; continue; }
        int c = tolower(*p), d = p[1] ? tolower(p[1]) : 0;
        if (c == 's' && d == 'h') { PUT("ш"); p += 2; continue; }
        if (c == 'c' && d == 'h') { PUT("ч"); p += 2; continue; }
        if (c == 'z' && d == 'h') { PUT("ж"); p += 2; continue; }
        if (c == 'k' && d == 'h') { PUT("х"); p += 2; continue; }
        if (c == 't' && d == 's') { PUT("ц"); p += 2; continue; }
        if (c == 't' && d == 'h') { PUT("з"); p += 2; continue; }
        if (c == 'p' && d == 'h') { PUT("ф"); p += 2; continue; }
        if (c == 'c' && d == 'k') { PUT("к"); p += 2; continue; }
        if (c == 'e' && d == 'e') { PUT("и"); p += 2; continue; }
        if (c == 'o' && d == 'o') { PUT("у"); p += 2; continue; }
        if (c == 'q' && d == 'u') { PUT("кв"); p += 2; continue; }
        if (c == 'y' && d == 'a') { PUT("я"); p += 2; continue; }
        if (c == 'y' && d == 'u') { PUT("ю"); p += 2; continue; }
        if (c == 'y' && d == 'o') { PUT("ё"); p += 2; continue; }
        switch (c) {
        case 'a': PUT("а"); break; case 'b': PUT("б"); break;
        case 'c': PUT((d == 'e' || d == 'i' || d == 'y') ? "с" : "к"); break;
        case 'd': PUT("д"); break; case 'e': PUT("е"); break; case 'f': PUT("ф"); break;
        case 'g': PUT("г"); break; case 'h': PUT("х"); break; case 'i': PUT("и"); break;
        case 'j': PUT("дж"); break; case 'k': PUT("к"); break; case 'l': PUT("л"); break;
        case 'm': PUT("м"); break; case 'n': PUT("н"); break; case 'o': PUT("о"); break;
        case 'p': PUT("п"); break; case 'q': PUT("к"); break; case 'r': PUT("р"); break;
        case 's': PUT("с"); break; case 't': PUT("т"); break; case 'u': PUT("у"); break;
        case 'v': PUT("в"); break; case 'w': PUT("в"); break; case 'x': PUT("кс"); break;
        case 'y': PUT("и"); break; default: PUT("з"); break;
        }
        p++;
    }
    o[n] = 0;
#undef PUT
}

/* Любой текст любым голосом (выбран один голос): «чужие» слова переводятся в рабочем потоке через фонемы eSpeak.
   Японский всегда через eSpeak — другие движки его не читают. */
static void speak_ex(const char *t, int cap) {
    if (!g_ready || !t[0] || !nev) return;
    const Voice *v = &ev[cur];
    char vn[40];
    if (detect(t) == 2) { snprintf(vn, sizeof vn, "ja"); request(1, t, vn, cap, -1); return; }
    if (v->eng == 0 || v->eng == 3) snprintf(vn, sizeof vn, "%d", v->arg);
    else if (v->eng == 5) snprintf(vn, sizeof vn, "%s", v->name);
    else snprintf(vn, sizeof vn, "%s", v->arg ? "ru" : "en-us");
    request(v->eng, t, vn, cap, v->lang);
}

static void speak(const char *t) { speak_ex(t, 0); }

static void sy_render(void) { if (hn) speak_ex(hist[sel], 1); }

static void set_sylmode(int on) {
    if (on == g_sylmode) return;
    g_abort = 1; g_flush = 1;
    trig_pending = 0; auto_run = 0;
    if (on) {
        agrain = 256; g_reopen = 1;       /* меньше буфер = меньше задержка отклика */
        g_sy_ready = 0; g_sylmode = 1;
        sy_render();
    } else {
        g_sylmode = 0; g_sy_ready = 0;
        agrain = 512; g_reopen = 1;
    }
}

/* ---------------- интерфейс ---------------- */
/* Экран «+VITA»: фон и спрайты — PNG из tools/gen_assets.py (assets, ui_atlas.h), текст рисуется поверх шрифтами TTF.
 * Левая половина зависит от режима (обычный / слоговый), правая (голоса, история) одна и та же. */
#include "ui_atlas.h"
#ifndef ASSET_PREFIX
#define ASSET_PREFIX "app0:assets/"
#endif
#ifndef FONT_PREFIX
#define FONT_PREFIX "app0:fonts/"
#endif

static vita2d_pvf *font;                      /* системный шрифт: запасной для японской каны */
static vita2d_font *f_ui, *f_lcd;             /* DejaVu Condensed Bold (кириллица) и DejaVu Mono Bold */
static vita2d_texture *g_atlas, *g_body[2];

#define COL_BG   RGBA8(235, 227, 205, 255)
#define COL_INK  RGBA8(58, 46, 32, 255)
#define COL_DIM  RGBA8(120, 108, 88, 255)
#define COL_LCD  RGBA8(54, 38, 10, 255)
#define COL_RED  RGBA8(200, 50, 40, 255)

static void ui_load(void) {
    g_atlas = vita2d_load_PNG_file(ASSET_PREFIX "atlas.png");
    g_body[0] = vita2d_load_PNG_file(ASSET_PREFIX "body_main.png");
    g_body[1] = vita2d_load_PNG_file(ASSET_PREFIX "body_syl.png");
    f_ui = vita2d_load_font_file(FONT_PREFIX "cond.ttf");
    f_lcd = vita2d_load_font_file(FONT_PREFIX "mono.ttf");
}

static void spr(int id, float x, float y) {
    const SprRect *r = &SPR_TAB[id];
    if (g_atlas) vita2d_draw_texture_part(g_atlas, x - r->pad, y - r->pad, r->x, r->y, r->w, r->h);
}

/* текст: TTF, а для японского (нет глифов в DejaVu) — системный PVF */
static int is_jp(const char *s) { return detect(s) == 2; }
static void ftx(vita2d_font *f, int x, int y, unsigned c, int size, const char *s) {
    if (f && !is_jp(s)) vita2d_font_draw_text(f, x, y, c, size, s);
    else if (font) vita2d_pvf_draw_text(font, x, y, c, size / 16.0f, s);
}
static int fw(vita2d_font *f, int size, const char *s) {
    if (f && !is_jp(s)) return vita2d_font_text_width(f, size, s);
    return (int)(strlen(s) * size * 0.55f);
}
static void ftc(vita2d_font *f, int cx, int y, unsigned c, int size, const char *s) { ftx(f, cx - fw(f, size, s) / 2, y, c, size, s); }
static void ftr(vita2d_font *f, int rx, int y, unsigned c, int size, const char *s) { ftx(f, rx - fw(f, size, s), y, c, size, s); }
static void fit(const char *s, char *d, int dsz, vita2d_font *f, int size, int maxw) {
    snprintf(d, dsz, "%s", s);
    if (fw(f, size, d) <= maxw) return;
    int n = strlen(d);
    while (n > 1) {
        n--;
        while (n > 0 && (((unsigned char)d[n]) & 0xC0) == 0x80) n--;
        d[n] = 0;
        char t[HL + 8];
        snprintf(t, sizeof t, "%s...", d);
        if (fw(f, size, t) <= maxw) { snprintf(d, dsz, "%s", t); return; }
    }
}
static int ncp(const char *s) {
    int n = 0;
    for (; *s; s++) if ((((unsigned char)*s) & 0xC0) != 0x80) n++;
    return n;
}

/* разбиение на строки для моноширинного ЖК: колонки (кана/иероглифы — 2 колонки) */
static int lcd_wrap(const char *s, int cap, int maxl, int *ls, int *ll) {
    int n = 0, i = 0, len = strlen(s);
    while (i < len && n < maxl + 1) {
        int st = i, col = 0, lastsp = -1, j = i;
        while (j < len && s[j] != '\n') {
            const unsigned char *p = (const unsigned char *)s + j, *q = p;
            int cp = dec(&q), cw = (cp >= 0x2E80) ? 2 : 1, bl = (int)(q - p);
            if (bl < 1) bl = 1;
            if (col + cw > cap) break;
            if (cp == ' ') lastsp = j + bl;
            col += cw; j += bl;
        }
        int e = j;
        if (j < len && s[j] != '\n' && lastsp > st) e = lastsp;
        int l = e - st;
        while (l > 0 && s[st + l - 1] == ' ') l--;
        ls[n] = st; ll[n] = l; n++;
        i = e;
        if (i < len && s[i] == '\n') i++;
        while (i < len && s[i] == ' ') i++;
    }
    return n;
}

/* ---------- геометрия (должна совпадать с tools/gen_assets.py) ---------- */
#define LCD_X 26
#define LCD_Y 74
#define LCD_W 560
#define LCD_H_MAIN 180
#define LCD_H_SYL 130
#define APP_VER "v2.30"
#define PILL_X 456
#define PILL_Y 16
#define ROW_Y 224
#define BIG_X 16
#define BIG_Y 424
#define VOI_Y 94
#define VOI_X(i) (624 + ((i) / 6) * 156)
#define VOI_YI(i) (VOI_Y + ((i) % 6) * 35)
#define HIS_Y 350

enum { SL_SPEED, SL_PITCH, SL_VOL, SL_BPM, SL_SENS, SL_N };
static const int SL_MIN[SL_N] = {50, 50, 20, 40, 2}, SL_MAX[SL_N] = {250, 200, 250, 240, 32}, SL_STEP[SL_N] = {5, 5, 5, 1, 1};

static int sl_geo(int id, int *x, int *cy, int *w) {
    int sy = g_sylmode;
    *x = 108; *w = sy ? 190 : 400;
    switch (id) {
    case SL_SPEED: if (sy) return 0; *cy = 318; return 1;
    case SL_PITCH: *cy = sy ? 342 : 366; return 1;
    case SL_BPM: if (!sy) return 0; *cy = 304; return 1;
    case SL_SENS: if (!sy) return 0; *cy = 380; return 1;
    default: *x = 410; *w = 128; *cy = 446; return 1;
    }
}
static int sl_get(int id) {
    switch (id) { case SL_SPEED: return speed; case SL_PITCH: return pitch; case SL_VOL: return volume; case SL_BPM: return bpm; default: return slices; }
}
static void sl_set(int id, int v) {
    if (v < SL_MIN[id]) v = SL_MIN[id];
    if (v > SL_MAX[id]) v = SL_MAX[id];
    switch (id) {
    case SL_SPEED: speed = v; break;
    case SL_PITCH: pitch = v; break;
    case SL_VOL: volume = v; gain = volume / 100.0f; break;
    case SL_BPM: bpm = v; break;
    default: slices = v; break;
    }
}

/* ---------- касания ---------- */
static int ui_force = 1;
static int u_down, u_press, u_rel, u_tx, u_ty, u_drag = -1, u_dirty;
static int inr(int x, int y, int w, int h) { return u_down && u_tx >= x && u_tx < x + w && u_ty >= y && u_ty < y + h; }
static int tapr(int x, int y, int w, int h) { return u_press && u_tx >= x && u_tx < x + w && u_ty >= y && u_ty < y + h; }

static void select_voice(int i) { cur = i; }

/* тап-темпо: средний интервал последних касаний -> BPM */
static void tap_tempo(void) {
    static unsigned long long last, iv[4];
    static int n;
    unsigned long long now = sceKernelGetProcessTimeWide();
    if (last && now - last < 2000000ULL) {
        for (int i = 3; i > 0; i--) iv[i] = iv[i - 1];
        iv[0] = now - last;
        if (n < 4) n++;
        unsigned long long sum = 0;
        for (int i = 0; i < n; i++) sum += iv[i];
        int b = (int)(60000000.0 * n / (double)sum + 0.5);
        sl_set(SL_BPM, b);
    } else n = 0;
    last = now;
}

static void ui_events(int *do_new, int *do_edit) {
    int sy = g_sylmode;
    if (u_press) {
        u_drag = -1; u_dirty = 0;
        for (int id = 0; id < SL_N; id++) {
            int x, cy, w;
            if (!sl_geo(id, &x, &cy, &w)) continue;
            if (u_tx >= x - 16 && u_tx <= x + w + 16 && abs(u_ty - cy) <= 20) { u_drag = id; break; }
        }
        if (u_drag < 0) {
            if (tapr(PILL_X, PILL_Y, 140, 36)) set_sylmode(!sy);
            else if (tapr(LCD_X, LCD_Y, LCD_W, sy ? 130 : 180)) { if (sy || !hn) *do_new = 1; else *do_edit = 1; }
            else if (tapr(BIG_X, BIG_Y, 330, 100)) {
                if (sy) { auto_run = !auto_run; trig_pending = 0; if (auto_run) g_resync = 1; }
                else if (g_busy || g_play) { g_abort = 1; g_flush = 1; }          /* во время речи кнопка = стоп */
                else if (hn) speak(hist[sel]);
                else *do_new = 1;
            }
            if (sy) {
                if (tapr(16, ROW_Y, 284, 44)) { sy_idx = 0; trig_pending = 0; }          /* REWIND */
                if (tapr(312, ROW_Y, 284, 44)) trig_pending = 1;                         /* NEXT */
                for (int i = 0; i < 6; i++) {
                    if (!tapr(380 + (i % 2) * 110, 300 + (i / 2) * 36, 102, 30)) continue;
                    if (i == 0) fullsyl = !fullsyl;
                    else if (i == 1) tap_tempo();
                    else subdiv = i - 1;
                }
            }
            for (int i = 0; i < nev && i < 12; i++) {
                if (!tapr(VOI_X(i), VOI_YI(i), 148, 32)) continue;
                select_voice(i);
                if (sy && hn) sy_render();                  /* слоги зависят от голоса: пересобрать фразу */
            }
            for (int i = 0; i < hn; i++) {
                if (!tapr(624, HIS_Y + i * 32, 308, 30)) continue;
                sel = i;
                if (sy) sy_render(); else speak(hist[sel]);
            }
        }
    }
    if (u_drag >= 0 && u_down) {
        int x, cy, w;
        if (sl_geo(u_drag, &x, &cy, &w)) {
            float t = (u_tx - x) / (float)w;
            if (t < 0) t = 0;
            if (t > 1) t = 1;
            int mn = SL_MIN[u_drag], st = SL_STEP[u_drag];
            int v = mn + (int)(t * (SL_MAX[u_drag] - mn) + 0.5f);
            v = mn + ((v - mn + st / 2) / st) * st;
            if (v != sl_get(u_drag)) { sl_set(u_drag, v); u_dirty = 1; }
        }
    }
    if (u_rel && u_drag >= 0) {
        if (u_dirty) {
            if (sy && u_drag == SL_PITCH) sy_render();
            else if (u_drag == SL_SENS) sy_reanalyze();
        }
        u_drag = -1; u_dirty = 0;
    }
}

/* ---------- рисование ---------- */
static void draw_btnw(int i, int style, int icon, const char *label, const char *hint) {
    int x = 16 + i * 296, y = ROW_Y, p = inr(x, y, 284, 44);
    static const int ids[2][2] = {{S_BTNW_CREAM, S_BTNW_CREAM_P}, {S_BTNW_YELLOW, S_BTNW_YELLOW_P}};
    spr(ids[style][p], x, y);
    spr(icon, x + 20, y + 8 + p);
    ftx(f_ui, x + 58, y + 29 + p, COL_INK, 17, label);
    ftr(f_ui, x + 268, y + 28 + p, COL_DIM, 11, hint);
}

static void draw_lcd(void) {
    char t[64];
    int sy = g_sylmode;
    int lh = sy ? LCD_H_SYL : LCD_H_MAIN;
    if (!sy) {
        if (hn) { snprintf(t, sizeof t, "%d/%d", ncp(hist[sel]), IMEMAX); ftr(f_lcd, LCD_X + LCD_W - 12, LCD_Y + 18, COL_LCD, 12, t); }
        if (!hn) { ftc(f_ui, LCD_X + LCD_W / 2, LCD_Y + lh / 2 + 6, RGBA8(54, 38, 10, 150), 22, "TAP HERE TO TYPE A PHRASE"); return; }
        static const int SIZES[] = {30, 26, 22, 18, 15, 12};
        int ls[10], ll[10], k, n = 0, maxl = 3, cap, sz = 12;
        for (k = 0; k < 6; k++) {
            sz = SIZES[k];
            maxl = (int)((lh - 40.0f) / (sz * 1.22f));
            if (maxl > 9) maxl = 9;
            cap = (int)((LCD_W - 28) / (sz * 0.602f));
            n = lcd_wrap(hist[sel], cap, maxl, ls, ll);
            if (n <= maxl) break;
        }
        cap = (int)((LCD_W - 28) / (sz * 0.602f));
        for (int i = 0; i < n && i < maxl; i++) {
            char b[HL];
            int l = ll[i];
            memcpy(b, hist[sel] + ls[i], l); b[l] = 0;
            if (i == maxl - 1 && n > maxl) { char c2[HL]; clip(b, c2, cap > 4 ? cap - 3 : 1); snprintf(b, sizeof b, "%s", c2); }
            ftx(f_lcd, LCD_X + 14, LCD_Y + 40 + (int)(i * sz * 1.22f) + (int)(sz * 0.55f), COL_LCD, sz, b);
        }
        return;
    }
    /* слоговый режим: фраза мелко и полоса слогов */
    if (hn) {
        int ls[3], ll[3];
        int n = lcd_wrap(hist[sel], 63, 2, ls, ll);
        for (int i = 0; i < n && i < 2; i++) {
            char b[HL];
            memcpy(b, hist[sel] + ls[i], ll[i]); b[ll[i]] = 0;
            ftx(f_lcd, LCD_X + 14, LCD_Y + 42 + i * 17, COL_LCD, 14, b);
        }
    }
    int sx = LCD_X + 14, sw = LCD_W - 28, y = LCD_Y + 78, h = 40;
    int nn = nsyl;
    if (!g_sy_ready || nn <= 0) {
        ftc(f_ui, LCD_X + LCD_W / 2, y + 27, RGBA8(54, 38, 10, 170), 17, g_sy_ready ? "NO SPEECH FOUND" : (hn ? "RENDERING PHRASE..." : "TAP HERE TO TYPE A PHRASE"));
        return;
    }
    float w = (sw - 2.0f * (nn - 1)) / nn;
    for (int i = 0; i < nn; i++) {
        float x = sx + i * (w + 2.0f);
        if (i == g_sy_last) vita2d_draw_rectangle(x, y, w, h, COL_LCD);
        else if (i == sy_idx) {
            vita2d_draw_rectangle(x, y, w, h, COL_LCD);
            vita2d_draw_rectangle(x + 3, y + 3, w - 6 > 1 ? w - 6 : 1, h - 6, RGBA8(236, 196, 40, 255));
            vita2d_draw_rectangle(x + 3, y + h - 12, w - 6 > 1 ? w - 6 : 1, 9, RGBA8(54, 38, 10, 120));
        } else vita2d_draw_rectangle(x, y, w, h, RGBA8(200, 150, 22, 255));
    }
}

/* «отпечаток» всего, что видно на экране: если не изменился, кадр не перерисовываем (меньше нагрузка на GPU) */
static unsigned ui_sig(void) {
    unsigned long long now = sceKernelGetProcessTimeWide();
    int v[] = {g_ready, g_busy, g_play, g_sylmode, sel, hn, cur, speed, pitch, volume, bpm, slices, subdiv, auto_run, fullsyl,
               g_sy_ready, nsyl, sy_idx, g_sy_last, g_audio_ms, g_first_ms, g_synth_ms, u_down, u_tx, u_ty, u_drag,
               (int)(auto_run && now - g_tick_t < 90000ULL), hn ? (int)strlen(hist[sel]) : 0, hn ? (int)strlen(hist[0]) : 0};
    unsigned h = 2166136261u;
    for (unsigned i = 0; i < sizeof v / sizeof v[0]; i++) { h ^= (unsigned)v[i]; h *= 16777619u; }
    return h;
}

static void ui_draw(void) {
    char t[160], t2[160];
    int sy = g_sylmode;
    vita2d_texture *bt = g_body[sy ? 1 : 0];
    if (bt) vita2d_draw_texture(bt, 0, 0);
    else vita2d_draw_rectangle(0, 0, 960, 544, COL_BG);

    /* верхняя полоса: состояние, кнопка слогового режима */
    int speaking = g_busy || g_play;
    spr(!g_ready ? S_LED_RED : (speaking ? S_LED_ORANGE : S_LED_GREEN), 632, 26);
    ftx(f_ui, 658, 40, COL_INK, 17, !g_ready ? "LOADING" : (speaking ? "SPEAKING" : "READY"));
    ftr(f_ui, 936, 40, COL_DIM, 12, APP_VER);
    if (sy && auto_run) {
        unsigned long long now = sceKernelGetProcessTimeWide();
        spr(now - g_tick_t < 90000ULL ? S_LED_ORANGE : S_LED_OFF, 840, 26);
    }
    {
        int p = inr(PILL_X, PILL_Y, 140, 36);
        spr(sy ? (p ? S_PILL_ORANGE_P : S_PILL_ORANGE) : (p ? S_PILL_CREAM_P : S_PILL_CREAM), PILL_X, PILL_Y);
        spr(S_IC_SYL, PILL_X + 10, PILL_Y + 4 + p);
        ftx(f_ui, PILL_X + 44, PILL_Y + 24 + p, COL_INK, 14, "SYLLABLE");
    }

    draw_lcd();

    /* слоговый режим: два длинных ряда кнопок */
    if (sy) {
        draw_btnw(0, 0, S_IC_REW, "REWIND", "L");
        draw_btnw(1, 1, S_IC_PLAY, "NEXT SYLLABLE", "X O TRI SQ");
    }

    /* ползунки */
    static const char *vfmt[SL_N] = {"%d%%", "%d%%", "%d%%", "%d", "%d"};
    for (int id = 0; id < SL_N; id++) {
        int x, cy, w;
        if (!sl_geo(id, &x, &cy, &w)) continue;
        float tt = (sl_get(id) - SL_MIN[id]) / (float)(SL_MAX[id] - SL_MIN[id]);
        if (tt > 0) vita2d_draw_rectangle(x, cy - 3, tt * w, 6, RGBA8(244, 126, 44, 255));
        spr(u_drag == id ? S_THUMB_P : S_THUMB, x + tt * w - 15, cy - 15);
        snprintf(t, sizeof t, vfmt[id], sl_get(id));
        if (id == SL_VOL) ftx(f_ui, x + w + 12, cy + 5, COL_INK, 14, t);
        else ftx(f_ui, x + w + 16, cy + 5, COL_INK, 15, t);
    }

    /* чипы слогового режима: FULL, TAP и сетка */
    if (sy) {
        static const char *syn[6] = {"FULL", "TAP TEMPO", "1/4", "1/8", "1/8T", "1/16"};
        static const int CH_OFF[6] = {S_CHIP0_OFF, S_CHIP1_OFF, S_CHIP2_OFF, S_CHIP3_OFF, S_CHIP4_OFF, S_CHIP5_OFF};
        static const int CH_ON[6] = {S_CHIP0_ON, S_CHIP1_ON, S_CHIP2_ON, S_CHIP3_ON, S_CHIP4_ON, S_CHIP5_ON};
        for (int i = 0; i < 6; i++) {
            int x = 380 + (i % 2) * 110, y = 300 + (i / 2) * 36;
            int on = (i == 0) ? fullsyl : (i == 1 ? inr(x, y, 102, 30) : subdiv == i - 1);
            spr(on ? CH_ON[i] : CH_OFF[i], x, y);
            ftc(f_ui, x + 51, y + 20 + (on ? 1 : 0), COL_INK, 14, syn[i]);
        }
    }

    /* главная кнопка */
    {
        int p = inr(BIG_X, BIG_Y, 330, 100);
        int orange = sy ? auto_run : speaking;
        spr(orange ? S_BIG_ORANGE : (p ? S_BIG_YELLOW_P : S_BIG_YELLOW), BIG_X, BIG_Y);
        spr(speaking && !sy ? S_IC_STOP_BIG : S_IC_PLAY_BIG, BIG_X + 30, BIG_Y + 22 + p);
        const char *bl = sy ? (auto_run ? "STOP AUTO" : "AUTOPLAY") : (speaking ? "STOP" : (hn ? "SPEAK" : "TYPE TEXT"));
        ftx(f_ui, BIG_X + 100, BIG_Y + 62 + p, COL_INK, 34, bl);
    }

    /* нижняя панель: громкость и состояние */
    {
        ftx(f_ui, 370, 488, COL_INK, 14, (hn && detect(hist[sel]) == 2) ? "eSpeak JA" : (nev ? ev[cur].label : "-"));
        if (sy) snprintf(t, sizeof t, "syllable %d / %d    %s", g_sy_last + 1, nsyl, g_sy_ready ? "" : "rendering...");
#ifdef HAVE_RHVOICE
        else if (g_ready && !rh_ok && ev[cur].eng == 5) snprintf(t, sizeof t, "RHVoice: not loaded");
#endif
#ifdef HAVE_PICO
        else if (g_ready && ev[cur].eng == 0 && !pico_ok[ev[cur].arg]) snprintf(t, sizeof t, "Pico: error %d", pico_err[ev[cur].arg]);
#endif
        else if (g_audio_ms > 0) snprintf(t, sizeof t, "start %d ms, synth %d/%d ms", g_first_ms, g_synth_ms, g_audio_ms);
        else snprintf(t, sizeof t, "tap the screen to type");
        ftx(f_ui, 370, 510, COL_DIM, 12, t);
    }

    /* голоса: две колонки по 6 */
    for (int i = 0; i < nev && i < 12; i++) {
        int x = VOI_X(i), y = VOI_YI(i);
        if (cur == i) spr(S_ROWSEL, x, y);
        int e = ev[i].eng, d = e == 0 ? 0 : (e == 1 ? 1 : (e == 3 ? 2 : 3));
        static const int DOT[4] = {S_DOT0, S_DOT1, S_DOT2, S_DOT3};
        spr(DOT[d], x + 10, y + 10);
        ftx(f_ui, x + 30, y + 22, COL_INK, 15, ev[i].label);
    }

    /* история (обрезанный текст кэшируется: ширину считать каждый кадр дорого) */
    static char hc_src[HN][100], hc_out[HN][100];
    static const int HC[5] = {S_HC0, S_HC1, S_HC2, S_HC3, S_HC4}, HCS[5] = {S_HC0_SEL, S_HC1_SEL, S_HC2_SEL, S_HC3_SEL, S_HC4_SEL};
    for (int i = 0; i < hn; i++) {
        int y = HIS_Y + i * 32;
        spr(i == sel ? HCS[i] : HC[i], 624, y);
        snprintf(t2, sizeof t2, "%d", i + 1);
        ftx(f_ui, 637, y + 20, COL_DIM, 13, t2);
        char src[100];
        clip(hist[i], src, 90);
        if (strcmp(src, hc_src[i])) { snprintf(hc_src[i], sizeof hc_src[i], "%s", src); fit(src, hc_out[i], sizeof hc_out[i], f_ui, 15, 262); }
        ftx(f_ui, 658, y + 20, COL_INK, 15, hc_out[i]);
    }
}

static int ime(const char *init, char *out, int outsz, int multi) {
    static uint16_t title[] = {'T', 'e', 'x', 't', 0};
    static uint16_t ini[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1], buf[SCE_IME_DIALOG_MAX_TEXT_LENGTH + 1];
    u8to16(init, ini, IMEMAX + 5);
    memset(buf, 0, sizeof buf);
    SceImeDialogParam p;
    sceImeDialogParamInit(&p);
    p.supportedLanguages = 0;
    p.languagesForced = 0;
    p.type = SCE_IME_TYPE_DEFAULT;
    p.option = multi ? 1 : 0;         /* 1 = многострочное окно */
    p.title = title;
    p.maxTextLength = IMEMAX;
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
    /* в многострочном режиме Enter = перенос строки, поэтому закрытие окна (кружок) тоже считаем подтверждением */
    int ok = multi ? (r.button != 0) : (r.button == SCE_IME_DIALOG_BUTTON_ENTER);
    sceImeDialogTerm();
    if (!ok) return 0;
    u16to8(buf, out, outsz);
    clean(out);
    return out[0] != 0;
}

#ifndef TTS_NO_MAIN
int main(void) {
    ker_init();
    vita2d_init();
    vita2d_set_clear_color(COL_BG);
    font = vita2d_load_custom_pvf("sa0:data/font/pvf/jpn0.pvf");
    if (!font) font = vita2d_load_default_pvf();
    ui_load();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

#ifdef HAVE_PICO
    ev[nev++] = (Voice){"Pico US", 0, 0, XL_EN, NULL};
    ev[nev++] = (Voice){"Pico UK", 0, 1, XL_EN, NULL};
    ev[nev++] = (Voice){"Pico DE", 0, 2, XL_DE, NULL};
    ev[nev++] = (Voice){"Pico ES", 0, 3, XL_ES, NULL};
    ev[nev++] = (Voice){"Pico FR", 0, 4, XL_FR, NULL};
    ev[nev++] = (Voice){"Pico IT", 0, 5, XL_IT, NULL};
#endif
#ifdef HAVE_SAM
    ev[nev++] = (Voice){"SAM C64", 3, 0, XL_EN, NULL};
#endif
#ifdef HAVE_ESPEAK
    ev[nev++] = (Voice){"eSpeak EN", 1, 0, XL_EN, NULL};
#endif
#ifdef HAVE_RHVOICE
    ev[nev++] = (Voice){"RH Aleksandr", 5, 0, XL_RU, "Aleksandr-hq"};
    ev[nev++] = (Voice){"RH Anna", 5, 1, XL_RU, "Anna"};
#endif
#ifdef HAVE_ESPEAK
    ev[nev++] = (Voice){"eSpeak RU", 1, 1, XL_RU, NULL};
#endif
    cur = 0;
    hist_load();

    SceUID ath = sceKernelCreateThread("aud", audio_thread, 96, 0x10000, 0, 0x40000, NULL);
    if (ath < 0) ath = sceKernelCreateThread("aud", audio_thread, 96, 0x10000, 0, 0, NULL);
    if (ath < 0) ath = sceKernelCreateThread("aud", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(ath, 0, NULL);

    SceUID th = sceKernelCreateThread("tts", worker, 0x10000100, 0x400000, 0, 0x20000, NULL);
    if (th < 0) th = sceKernelCreateThread("tts", worker, 0x10000100, 0x400000, 0, 0, NULL);
    if (th < 0) th = sceKernelCreateThread("tts", worker, 0x10000100, 0x100000, 0, 0, NULL);
    sceKernelStartThread(th, 0, NULL);

    SceCtrlData pad;
    unsigned old = 0;
    int tprev = 0;
    static char buf[HL];

    for (;;) {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        unsigned pr = pad.buttons & ~old;
        old = pad.buttons;
        int selh = (pad.buttons & SCE_CTRL_SELECT) != 0;

        SceTouchData td;
        sceTouchPeek(SCE_TOUCH_PORT_FRONT, &td, 1);
        int tnow = td.reportNum > 0;
        u_press = tnow && !tprev;
        u_rel = !tnow && tprev;
        u_down = tnow;
        if (tnow) { u_tx = td.report[0].x / 2; u_ty = td.report[0].y / 2; }
        tprev = tnow;

        int do_new = 0, do_edit = 0, single = 0;
        if ((pr & SCE_CTRL_CIRCLE) && selh) {
            set_sylmode(!g_sylmode);                         /* Select+O: слоговый режим вкл/выкл */
        } else if (g_sylmode) {
            /* ---- слоговый режим ---- */
            if (pr & SCE_CTRL_START) {
                if (selh) break;
                auto_run = !auto_run; trig_pending = 0;
                if (auto_run) g_resync = 1;
            }
            if (pr & SCE_CTRL_LTRIGGER) {
                if (selh) { if (slices > 2) slices--; sy_reanalyze(); }
                else { sy_idx = 0; trig_pending = 0; }
            }
            if (pr & SCE_CTRL_RTRIGGER) {
                if (selh) { if (slices < 32) slices++; sy_reanalyze(); }
                else do_new = 1;
            }
            if (pr & (SCE_CTRL_UP | SCE_CTRL_DOWN)) {
                if (selh) {
                    volume += (pr & SCE_CTRL_UP) ? 10 : -10;
                    if (volume < 20) volume = 20; if (volume > 250) volume = 250;
                    gain = volume / 100.0f;
                } else {
                    int o = sel;
                    if (pr & SCE_CTRL_UP) { if (sel > 0) sel--; } else { if (sel < hn - 1) sel++; }
                    if (sel != o) sy_render();
                }
            }
            if (pr & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
                int d = (pr & SCE_CTRL_RIGHT) ? 1 : -1;
                if (selh) { pitch += d * 10; if (pitch < 50) pitch = 50; if (pitch > 200) pitch = 200; sy_render(); }
                else { int nb = bpm + d; if (nb < 40) nb = 40; if (nb > 240) nb = 240; bpm = nb; }
            }
            if (selh && (pr & SCE_CTRL_SQUARE)) fullsyl = !fullsyl;       /* Select+Квадрат: слоги до конца */
            if (selh && (pr & SCE_CTRL_TRIANGLE)) subdiv = subdiv >= 4 ? 1 : subdiv + 1;
            if (selh && (pr & SCE_CTRL_CROSS)) { do_new = 1; single = 1; }
        } else {
            /* ---- обычный режим ---- */
            if (pr & SCE_CTRL_START) break;
            if (pr & SCE_CTRL_LTRIGGER) {
                if (!selh && nev) cur = (cur + nev - 1) % nev;
            }
            if (pr & SCE_CTRL_RTRIGGER) {
                if (!selh && nev) cur = (cur + 1) % nev;
            }
            if (pr & (SCE_CTRL_UP | SCE_CTRL_DOWN)) {
                if (selh) {
                    volume += (pr & SCE_CTRL_UP) ? 10 : -10;
                    if (volume < 20) volume = 20; if (volume > 250) volume = 250;
                    gain = volume / 100.0f;
                } else if (pr & SCE_CTRL_UP) { if (sel > 0) sel--; }
                else { if (sel < hn - 1) sel++; }
            }
            if (pr & (SCE_CTRL_LEFT | SCE_CTRL_RIGHT)) {
                int d = (pr & SCE_CTRL_RIGHT) ? 10 : -10;
                if (selh) { pitch += d; if (pitch < 50) pitch = 50; if (pitch > 200) pitch = 200; }
                else { speed += d; if (speed < 50) speed = 50; if (speed > 250) speed = 250; }
            }
            if (pr & SCE_CTRL_SQUARE) { g_abort = 1; g_flush = 1; }
            if ((pr & SCE_CTRL_CIRCLE) && hn) speak(hist[sel]);
            if (pr & SCE_CTRL_CROSS) { if (selh) { do_new = 1; single = 1; } else do_new = 1; }
            if ((pr & SCE_CTRL_TRIANGLE) && hn) do_edit = 1;
        }

        ui_events(&do_new, &do_edit);

        if (do_new || do_edit) {
            if (ime(do_edit ? hist[sel] : "", buf, sizeof buf, !single)) {
                hist_add(buf);
                if (g_sylmode) speak_ex(buf, 1); else speak(buf);
            }
            g_reopen = 1;                                   /* системный диалог мог забрать звук */
            ui_force = 1;
            sceCtrlPeekBufferPositive(0, &pad, 1);
            old = pad.buttons;
            tprev = 1; u_drag = -1; u_down = 0; u_press = 0;
            continue;
        }

        /* --- рисование: только если что-то изменилось (и раз в полсекунды на всякий случай) --- */
        {
            static unsigned last_sig;
            static int idle;
            unsigned sg = ui_sig();
            if (sg == last_sig && idle < 30 && !ui_force) { idle++; sceKernelDelayThread(16000); continue; }
            last_sig = sg; idle = 0; ui_force = 0;
        }
        vita2d_start_drawing();
        vita2d_clear_screen();
        ui_draw();
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    g_abort = 1;
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}

#endif
