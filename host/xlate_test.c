/* Проверка перевода «чужого» текста на компьютере с настоящим eSpeak-NG (нужна собранная libespeak-ng):
 *   gcc host/xlate_test.c -I<espeak>/include -L<espeak>/lib -lespeak-ng -o /tmp/xt && /tmp/xt */
#include <espeak-ng/speak_lib.h>
#include "../xlate.h"
static const char *cur = "";
static const char *phon(const char *w, int src) {
    static char out[512];
    const char *v = src == XL_RU ? "ru" : "en-us";
    if (strcmp(cur, v)) { espeak_SetVoiceByName(v); cur = v; }
    const void *p = w; out[0] = 0;
    while (p) { const char *r = espeak_TextToPhonemes(&p, espeakCHARS_UTF8, 0x02); if (r) strncat(out, r, sizeof out - strlen(out) - 1); }
    return out;
}
int main(void) {
    espeak_Initialize(AUDIO_OUTPUT_SYNCHRONOUS, 0, NULL, 0);
    static char o[4096];
    const char *en = "Hello, this is VITA. We are the robots! Computer love, one two three four. Kraftwerk 1981.";
    const char *ru = "Привет, это Вита. Экспериментальная речь! Щука, жёлтый чай, мы едем домой. Ёлка 2024.";
    xl_text(en, XL_RU, o, sizeof o, phon, NULL); printf("RU голос <- EN: %s\n", o);
    xl_text(ru, XL_RU, o, sizeof o, phon, NULL); printf("RU голос <- RU: %s\n", o);
    const char *ln[] = {"EN", "RU", "DE", "ES", "FR", "IT"};
    for (int l = 0; l < 6; l++) if (l != XL_RU) { xl_text(ru, l, o, sizeof o, phon, NULL); printf("%s голос <- RU: %s\n", ln[l], o); }
    xl_text("Привет, VITA! мир-world", XL_EN, o, sizeof o, phon, NULL); printf("смесь EN: %s\n", o);
    return 0;
}
