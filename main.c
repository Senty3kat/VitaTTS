// VitaTTS v2 - быстрый TTS для PS Vita (Pico/SVOX для английского, eSpeak-NG для RU/JA/EN-retro)
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

#ifdef HAVE_PICO
#include "picoapi.h"
#include "picodefs.h"
#endif
#ifdef HAVE_ESPEAK
#include <espeak-ng/speak_lib.h>
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

int _newlib_heap_size_user = 100 * 1024 * 1024;

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
static int pico_ok[2], pico_err[2], es_ok;
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
static volatile int bpm = 120, subdiv = 1, sens = 16;
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
            if (vpos < vend) { ppos = vpos; ptot = vend - vpos; if (ptot > 96) ptot = 96; pleft = ptot; }
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
        if (pleft > 0) { sm += phr[ppos++] * ((float)pleft / ptot); pleft--; }
        sm *= gain;
        ab[i] = sm > 32767.0f ? 32767 : (sm < -32768.0f ? -32768 : (short)sm);
    }
    clk += G;
    if (aport >= 0) sceAudioOutOutput(aport, ab); else sceKernelDelayThread(2000);
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
            if (avail >= PREBUF || (g_done && avail > 0)) playing = 1;
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
            if (aport >= 0) sceAudioOutOutput(aport, ab);
            bi = (bi + 1) % 3; playing = 0;
            continue;
        }
        __sync_synchronize();
        for (int i = 0; i < G; i++) ab[i] = i < n ? ring[(rtail + i) & (RING - 1)] : 0;
        rtail += n;
        if (n > 0 && aport >= 0) sceAudioOutOutput(aport, ab);
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
    float D = sens / 100.0f * M;
    int b[MAXSYL + 2], nb = 0;
    b[nb++] = s0;
    int st = 0, exti = s0, lastb = s0;
    float ext = e2[s0];
    for (int f = s0 + 1; f <= e0; f++) {
        float v = e2[f];
        if (st == 0) {                          /* ищем пик */
            if (v > ext) { ext = v; exti = f; }
            else if (ext - v > D) { st = 1; ext = v; exti = f; }
        } else {                                /* ищем провал */
            if (v < ext) { ext = v; exti = f; }
            else if (v - ext > D) {
                if (exti - lastb >= 10 && nb < MAXSYL) { b[nb++] = exti; lastb = exti; }
                st = 0; ext = v; exti = f;
            }
        }
    }
    b[nb] = e0 + 1;
    for (int i = 1; i < nb; i++) {              /* граница = начало звука, а не начало паузы */
        int k = b[i];
        while (k < b[i + 1] - 1 && e2[k] < quiet) k++;
        b[i] = k;
    }
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
#define PICO_MEM (20 * 1024 * 1024)
static pico_System ps[2];
static pico_Engine pe[2];

static int pico_load(int v) {
    static const char *vn[2] = {"VUS", "VGB"};
    static const char *fl[2][2] = {
        {"app0:/en-US_ta.bin", "app0:/en-US_lh0_sg.bin"},
        {"app0:/en-GB_ta.bin", "app0:/en-GB_kh0_sg.bin"}};
    void *mem = malloc(PICO_MEM);
    if (!mem) return -1;
    int rc = pico_initialize(mem, PICO_MEM, &ps[v]);
    if (rc) return rc;
    rc = pico_createVoiceDefinition(ps[v], (const pico_Char *)vn[v]);
    if (rc) return rc;
    for (int k = 0; k < 2; k++) {
        pico_Resource r;
        pico_Char nm[PICO_MAX_RESOURCE_NAME_SIZE];
        rc = pico_loadResource(ps[v], (const pico_Char *)fl[v][k], &r);
        if (rc) return rc;
        rc = pico_getResourceName(ps[v], r, (char *)nm);
        if (rc) return rc;
        rc = pico_addResourceToVoiceDefinition(ps[v], (const pico_Char *)vn[v], nm);
        if (rc) return rc;
    }
    return pico_newEngine(ps[v], (const pico_Char *)vn[v], &pe[v]);
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
    for (int i = 0; i < cnt; i++) {
        int v = ((int)b[i] - 128) * 200;     /* постоянный уровень: без «качания» громкости между кусками */
        pcm[i] = (short)(v > 30000 ? 30000 : (v < -30000 ? -30000 : v));
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

/* ---- русский -> фонемы SAM ----
   Правила: оглушение/озвончение, смягчение (ь, я е ё ю и), шипящие всегда твёрдые, ц = Т+С.
   Ударение словарём НЕ определяется (без него звук монотонный, «роботный»):
   чтобы выделить гласную, напиши её ЗАГЛАВНОЙ (приВет), ё всегда ударная. */
typedef struct { int cp; const char *v, *u; int vo, trig; } RuOb;
static const RuOb ruob[] = {
    {0x431, "B", "P", 1, 1}, {0x432, "V", "F", 1, 0}, {0x433, "G", "K", 1, 1}, {0x434, "D", "T", 1, 1},
    {0x436, "ZH", "SH", 1, 1}, {0x437, "Z", "S", 1, 1},
    {0x43A, "G", "K", 0, 1}, {0x43F, "B", "P", 0, 1}, {0x442, "D", "T", 0, 1}, {0x444, "V", "F", 0, 1},
    {0x441, "Z", "S", 0, 1}, {0x448, "ZH", "SH", 0, 1}, {0x445, "/H", "/H", 0, 1},
    {0x446, "TS", "TS", 0, 1}, {0x447, "CH", "CH", 0, 1}, {0x449, "SHSH", "SHSH", 0, 1},
    {0, 0, 0, 0, 0}};

typedef struct { const char *fix; const RuOb *ob; int voiced, hard, stress; char vowel; } RuTok;

static const RuOb *ru_findob(int cp) { for (const RuOb *o = ruob; o->cp; o++) if (o->cp == cp) return o; return 0; }
static int ru_lower(int c) { if (c >= 0x410 && c <= 0x42F) return c + 0x20; if (c == 0x401) return 0x451; return c; }
static int ru_isletter(int c) { c = ru_lower(c); return (c >= 0x430 && c <= 0x44F) || c == 0x451; }
static int ru_isupper(int c) { return (c >= 0x410 && c <= 0x42F) || c == 0x401; }

/* одно слово (кодпоинты) -> строка фонем */
static void ru_word(const int *cp, int n, char *out, int outsz) {
    RuTok t[96]; int nt = 0;
    int nlow = 0; for (int i = 0; i < n; i++) if (ru_isletter(cp[i]) && !ru_isupper(cp[i])) nlow++;
    int allcaps = (nlow == 0 && n > 1);
    for (int i = 0; i < n && nt < 90; i++) {
        int c0 = cp[i];
        if (c0 == 0x301) { if (nt && t[nt - 1].vowel) t[nt - 1].stress = 1; continue; }   /* знак ударения после гласной */
        int c = ru_lower(c0);
        int up = ru_isupper(c0) && !allcaps;
        const char *vf = 0; int iot = 0, isi = 0;
        switch (c) {
            case 0x430: vf = "AA"; break;                 /* а */
            case 0x43E: vf = "AO"; break;                 /* о */
            case 0x443: vf = "UX"; break;                 /* у */
            case 0x44D: vf = "EH"; break;                 /* э */
            case 0x44B: vf = "IX"; break;                 /* ы */
            case 0x438: vf = "IY"; isi = 1; break;        /* и */
            case 0x44F: vf = "AA"; iot = 1; break;        /* я */
            case 0x44E: vf = "UX"; iot = 1; break;        /* ю */
            case 0x435: vf = "EH"; iot = 1; break;        /* е */
            case 0x451: vf = "AO"; iot = 1; up = 1; break;/* ё */
        }
        if (vf) {
            RuTok *p = nt ? &t[nt - 1] : 0;
            int prevhard = p && p->ob && p->hard;          /* ж ш ц */
            int prevcons = p && (p->ob || (p->fix && !p->vowel && strcmp(p->fix, "Y") != 0));
            if (iot && !(prevhard && c != 0x44E && c != 0x44F ? 0 : 0)) {
                /* смягчение/йотация: после согласной (кроме ж ш ц) и после гласной/начала добавляем Y */
                if (!prevhard) { t[nt].fix = "Y"; t[nt].ob = 0; t[nt].hard = 0; t[nt].vowel = 0; t[nt].stress = 0; nt++; }
            }
            if (isi && prevhard) vf = "IX";                /* жи ши цы читаются как «ы» */
            (void)prevcons;
            t[nt].fix = vf; t[nt].ob = 0; t[nt].hard = 0; t[nt].vowel = 1; t[nt].stress = up; nt++;
            continue;
        }
        if (c == 0x44C) {                                  /* ь */
            if (nt && !t[nt - 1].vowel && !(t[nt - 1].ob && (t[nt - 1].ob->cp == 0x436 || t[nt - 1].ob->cp == 0x448 || t[nt - 1].ob->cp == 0x447 || t[nt - 1].ob->cp == 0x449))) {
                t[nt].fix = "Y"; t[nt].ob = 0; t[nt].hard = 0; t[nt].vowel = 0; t[nt].stress = 0; nt++;
            }
            continue;
        }
        if (c == 0x44A) continue;                          /* ъ */
        const char *fx = 0;
        switch (c) { case 0x43B: fx = "L"; break; case 0x43C: fx = "M"; break; case 0x43D: fx = "N"; break;
                     case 0x440: fx = "R"; break; case 0x439: fx = "Y"; break; }
        if (fx) { t[nt].fix = fx; t[nt].ob = 0; t[nt].hard = 0; t[nt].vowel = 0; t[nt].stress = 0; nt++; continue; }
        const RuOb *o = ru_findob(c);
        if (o) {
            t[nt].fix = 0; t[nt].ob = o; t[nt].voiced = o->vo;
            t[nt].hard = (c == 0x436 || c == 0x448 || c == 0x446);
            t[nt].vowel = 0; t[nt].stress = 0; nt++;
        }
    }
    /* оглушение / озвончение справа налево */
    for (int i = nt - 1; i >= 0; i--) {
        if (!t[i].ob) continue;
        if (i == nt - 1) { t[i].voiced = 0; continue; }
        const RuTok *nx = &t[i + 1];
        if (nx->ob && nx->ob->trig) t[i].voiced = nx->voiced;
    }
    int o = 0;
    for (int i = 0; i < nt; i++) {
        const char *s = t[i].ob ? (t[i].voiced ? t[i].ob->v : t[i].ob->u) : t[i].fix;
        int l = strlen(s);
        if (o + l + 2 >= outsz) break;
        memcpy(out + o, s, l); o += l;
        if (t[i].vowel && t[i].stress) out[o++] = '5';
    }
    out[o] = 0;
}

static void sam_say_ru(const char *txt, int es) {
    static char in[256], w[96];
    static int cp[96];
    const unsigned char *p = (const unsigned char *)txt;
    int n = 0, nin = 0;
    while (*p && !g_abort) {
        int c = dec(&p);
        int letter = ru_isletter(c) || c == 0x301;
        if (letter) { if (n < 90) cp[n++] = c; }
        if (!letter || !*p) {
            if (n) {
                ru_word(cp, n, w, sizeof w);
                int wl = strlen(w);
                if (nin + wl + 2 > 225) { in[nin++] = (char)0x9b; in[nin] = 0; sam_run(in, es); nin = 0; }
                memcpy(in + nin, w, wl); nin += wl;
                in[nin++] = ' ';
                n = 0;
            }
            if (!letter && nin < 225 && (c == ',' || c == '.' || c == '?' || c == '!' || c == '-')) in[nin++] = (char)c;
        }
    }
    if (nin > 0 && !g_abort) { in[nin++] = (char)0x9b; in[nin] = 0; sam_run(in, es); }
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

/* ---------------- рабочий поток синтеза ---------------- */
static int worker(SceSize args, void *argp) {
#ifdef HAVE_PICO
    for (int v = 0; v < 2; v++) { pico_err[v] = pico_load(v); pico_ok[v] = (pico_err[v] == 0); }
#endif
#ifdef HAVE_ESPEAK
    es_rate = espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, "app0:", espeakINITIALIZE_DONT_EXIT);
    if (es_rate > 0) { espeak_SetSynthCallback(es_cb); es_ok = 1; }
#endif
    g_ready = 1;
    char last[40] = "";
    for (;;) {
        if (!g_req) { sceKernelDelayThread(2000); continue; }
        g_busy = 1; g_req = 0;
        char t[HL], buf[HL + 100];
        snprintf(t, sizeof t, "%s", rq_text);
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
            int v = atoi(rq_voice) & 1;
            if (pico_ok[v]) {
                int lv = es < 20 ? 20 : (es > 500 ? 500 : es);
                snprintf(buf, sizeof buf, "<speed level='%d'>%s</speed>", lv, t);
                pico_say(v, buf);
            }
#endif
        } else if (rq_eng == 3 || rq_eng == 4) {
#ifdef HAVE_SAM
            if (rq_eng == 4) sam_say_ru(t, es); else sam_say(t, es);
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
typedef struct { const char *label; int eng; int arg; } Voice;
static Voice ev[12];
static int nev, cur;
static int speed = 100, pitch = 100, volume = 100, style;
static const char *stylev[] = {"", "+m3", "+m6", "+f2", "+f4", "+croak"};
#define NSTYLE 6
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

static void request(int eng, const char *t, const char *vn, int cap) {
    g_abort = 1;
    g_flush = 1;
    while (g_busy) sceKernelDelayThread(500);
    snprintf(rq_text, sizeof rq_text, "%s", t);
    snprintf(rq_voice, sizeof rq_voice, "%s", vn);
    rq_eng = eng; rq_speed = speed; rq_pitch = pitch; rq_cap = cap;
    g_abort = 0;
    g_req = 1;
}

static void speak_ex(const char *t, int cap) {
    if (!g_ready || !t[0]) return;
    int lang = detect(t), eng;
    char vn[40];
#ifdef HAVE_SAM
    if (lang == 1 && ev[cur].eng == 3) { eng = 4; snprintf(vn, sizeof vn, "0"); }   /* голос SAM + русский текст = робо-русский */
    else
#endif
    if (lang == 1) { eng = 1; snprintf(vn, sizeof vn, "ru%s", stylev[style]); }
    else if (lang == 2) { eng = 1; snprintf(vn, sizeof vn, "ja%s", stylev[style]); }
    else {
        eng = ev[cur].eng;
        if (eng == 0 || eng == 3) snprintf(vn, sizeof vn, "%d", ev[cur].arg);
        else snprintf(vn, sizeof vn, "en-us%s", stylev[style]);
    }
    request(eng, t, vn, cap);
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
static vita2d_pvf *font;

static void T(int x, int y, unsigned c, float s, const char *fmt, ...) {
    char b[700];
    va_list a; va_start(a, fmt); vsnprintf(b, sizeof b, fmt, a); va_end(a);
    vita2d_pvf_draw_text(font, x, y, c, s, b);
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

#define COL_BG   RGBA8(18, 18, 28, 255)
#define COL_TXT  RGBA8(235, 235, 235, 255)
#define COL_DIM  RGBA8(130, 130, 150, 255)
#define COL_ACC  RGBA8(0, 230, 170, 255)
#define COL_BOX  RGBA8(40, 40, 60, 255)
#define COL_SEL  RGBA8(0, 90, 80, 255)

static const char *pst(int v, char *b) {
    if (pico_ok[v]) return "ok";
    sprintf(b, "err%d", pico_err[v]);
    return b;
}

int main(void) {
    ker_init();
    vita2d_init();
    vita2d_set_clear_color(COL_BG);
    font = vita2d_load_custom_pvf("sa0:data/font/pvf/jpn0.pvf");
    if (!font) font = vita2d_load_default_pvf();

    sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT, SCE_TOUCH_SAMPLING_STATE_START);
    sceTouchSetSamplingState(SCE_TOUCH_PORT_BACK, SCE_TOUCH_SAMPLING_STATE_START);

#ifdef HAVE_PICO
    ev[nev++] = (Voice){"Pico US (SVOX)", 0, 0};
    ev[nev++] = (Voice){"Pico UK (SVOX)", 0, 1};
#endif
#ifdef HAVE_ESPEAK
    ev[nev++] = (Voice){"eSpeak EN (retro)", 1, 0};
#endif
#ifdef HAVE_SAM
    ev[nev++] = (Voice){"SAM C64", 3, 0};
#endif
    hist_load();

    SceUID ath = sceKernelCreateThread("aud", audio_thread, 96, 0x10000, 0, 0x40000, NULL);
    if (ath < 0) ath = sceKernelCreateThread("aud", audio_thread, 96, 0x10000, 0, 0, NULL);
    if (ath < 0) ath = sceKernelCreateThread("aud", audio_thread, 0x10000100, 0x10000, 0, 0, NULL);
    sceKernelStartThread(ath, 0, NULL);

    SceUID th = sceKernelCreateThread("tts", worker, 0x10000100, 0x100000, 0, 0x20000, NULL);
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
        int tap = tnow && !tprev, ty = 0;
        if (tap) ty = td.report[0].y / 2;
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
                if (selh) { sens -= 3; if (sens < 5) sens = 5; sy_reanalyze(); }
                else { sy_idx = 0; trig_pending = 0; }
            }
            if (pr & SCE_CTRL_RTRIGGER) {
                if (selh) { sens += 3; if (sens > 45) sens = 45; sy_reanalyze(); }
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
            if (selh && (pr & SCE_CTRL_SQUARE)) quant = !quant;
            if (selh && (pr & SCE_CTRL_TRIANGLE)) subdiv = subdiv >= 4 ? 1 : subdiv + 1;
            if (selh && (pr & SCE_CTRL_CROSS)) { do_new = 1; single = 1; }
            if (tap) {
                if (ty < 160) do_new = 1;
                else if (ty >= 170 && (ty - 170) / 58 < hn) { sel = (ty - 170) / 58; sy_render(); }
            }
        } else {
            /* ---- обычный режим ---- */
            if (pr & SCE_CTRL_START) break;
            if (pr & SCE_CTRL_LTRIGGER) {
                if (selh) style = (style + NSTYLE - 1) % NSTYLE; else cur = (cur + nev - 1) % nev;
            }
            if (pr & SCE_CTRL_RTRIGGER) {
                if (selh) style = (style + 1) % NSTYLE; else cur = (cur + 1) % nev;
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
            if (tap) {
                if (ty < 160) do_new = 1;
                else if (ty >= 170 && (ty - 170) / 58 < hn) { sel = (ty - 170) / 58; speak(hist[sel]); }
            }
        }

        if (do_new || do_edit) {
            if (ime(do_edit ? hist[sel] : "", buf, sizeof buf, !single)) {
                hist_add(buf);
                if (g_sylmode) speak_ex(buf, 1); else speak(buf);
            }
            sceCtrlPeekBufferPositive(0, &pad, 1);
            old = pad.buttons;
            tprev = 1;
            continue;
        }

        /* --- рисование --- */
        vita2d_start_drawing();
        vita2d_clear_screen();
        T(20, 34, COL_ACC, 1.3f, "VitaTTS");
        if (g_sylmode) {
            static const char *gn[5] = {"", "1/4", "1/8", "1/8T", "1/16"};
            T(180, 34, COL_ACC, 0.95f, "SYLLABLE MODE   (Select+O: back)");
            T(20, 66, COL_TXT, 0.95f, "BPM %d   grid %s   quant %s   auto %s   syllable %d/%d",
              bpm, gn[subdiv], quant ? "ON" : "off", auto_run ? "RUN" : "off", g_sy_last + 1, nsyl);
            T(20, 92, COL_TXT, 0.95f, "Pitch %d%%   Volume %d%%   Sens %d   %s", pitch, volume, sens,
              g_sy_ready ? "" : "rendering...");
            vita2d_draw_rectangle(20, 110, 920, 46, COL_BOX);
            int n = nsyl;
            if (g_sy_ready && n > 0) {
                float w = (916.0f - 2.0f * (n - 1)) / n;
                for (int i = 0; i < n; i++) {
                    unsigned c = (i == g_sy_last) ? COL_ACC : (i == sy_idx ? RGBA8(210, 200, 70, 255) : RGBA8(70, 70, 100, 255));
                    vita2d_draw_rectangle(22 + i * (w + 2.0f), 114, w, 38, c);
                }
            } else T(34, 142, COL_DIM, 1.0f, "%s", g_sy_ready ? "no syllables found (lower Sens: Select+L)" : "rendering phrase...");
            if (quant || auto_run) {
                unsigned long long now = sceKernelGetProcessTimeWide();
                if (now - g_tick_t < 90000ULL) vita2d_draw_rectangle(905, 14, 26, 26, COL_ACC);
                else vita2d_draw_rectangle(905, 14, 26, 26, COL_BOX);
            }
        } else {
            T(180, 34, COL_DIM, 0.9f, "%s", g_ready ? ((g_busy || g_play) ? "SPEAKING" : "ready") : "loading...");
            T(20, 66, COL_TXT, 0.95f, "Voice (L/R): %s    eSpeak style (Sel+L/R): %s", ev[cur].label, stylev[style][0] ? stylev[style] : "default");
            T(20, 92, COL_TXT, 0.95f, "Speed %d%%   Pitch %d%%   Volume %d%%", speed, pitch, volume);
            vita2d_draw_rectangle(20, 110, 920, 46, COL_BOX);
            T(34, 142, COL_ACC, 1.1f, "[X] / tap here: type a phrase and speak");
        }
        for (int i = 0; i < HN; i++) {
            int y = 170 + i * 58;
            vita2d_draw_rectangle(20, y, 920, 52, (i == sel && hn) ? COL_SEL : COL_BOX);
            if (i < hn) {
                char c[100];
                clip(hist[i], c, 70);
                T(32, y + 34, COL_TXT, 1.1f, "%d  %s", i + 1, c);
            } else T(32, y + 34, COL_DIM, 1.0f, "%d  -", i + 1);
        }
        if (g_sylmode) {
            T(20, 504, COL_DIM, 0.72f, "X/O/Sq/Tri or REAR TOUCH: next syllable   L rewind  R new  Up/Down phrase  Left/Right BPM  START auto-run");
            T(20, 526, COL_DIM, 0.72f, "Select+: Left/Right pitch  Up/Down vol  L/R sens  Square quant  Triangle grid  X one-line  Start exit");
        } else {
            T(20, 504, COL_DIM, 0.72f, "X new (finish with O)  O repeat  Triangle edit  Square stop  Up/Down history  Left/Right speed  L/R voice");
            T(20, 526, COL_DIM, 0.72f, "Select+: Left/Right pitch  Up/Down volume  L/R eSpeak style  X one-line  O syllable mode      START exit");
        }
        vita2d_end_drawing();
        vita2d_swap_buffers();
    }

    g_abort = 1;
    vita2d_fini();
    sceKernelExitProcess(0);
    return 0;
}
