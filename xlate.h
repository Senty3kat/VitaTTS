/* Перевод фонем eSpeak (IPA) в «понятное чужому голосу» написание.
 * Нужен, чтобы любой голос читал любой текст: русский голос получает английские слова кириллицей «как слышится»
 * (hello -> хэлоу), английский — русские слова латиницей «как слышится» (привет -> preevyet).
 * Только чистые функции над строками: проверяются на компьютере (host/xlate_test.c). */
#pragma once
#include <string.h>
#include <stdio.h>

/* целевой язык голоса */
enum { XL_EN = 0, XL_RU = 1, XL_DE = 2, XL_ES = 3, XL_FR = 4, XL_IT = 5 };

typedef struct { const char *ipa, *out; } XlTok;

static int xl_isv(const char *p) {            /* гласная IPA в начале строки? */
    static const char *V[] = {"i", "ɪ", "ᵻ", "e", "ɛ", "æ", "a", "ɑ", "ɒ", "ɔ", "o", "ʊ", "u", "ʌ", "ə", "ɐ", "ɨ", "y", "ʉ", "ɜ", "ɚ", "ɝ", "ɵ", 0};
    for (int i = 0; V[i]; i++) if (!strncmp(p, V[i], strlen(V[i]))) return 1;
    return 0;
}

/* пропускаемые знаки: ударения, долгота, связки, диакритика */
static int xl_skip(const unsigned char *p) {
    if (p[0] == 0xCB && (p[1] == 0x88 || p[1] == 0x8C || p[1] == 0x90 || p[1] == 0x91)) return 2;   /* ˈ ˌ ː ˑ */
    if (p[0] == 0xCC || (p[0] == 0xCD && p[1] < 0xB0)) return 2;                                   /* U+0300..036F */
    if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0xBF) return 3;                                      /* ‿ */
    if (p[0] == '-' || p[0] == '_' || p[0] == '\'') return 1;
    return 0;
}

static int xl_cpl(const unsigned char *p) {   /* длина UTF-8 символа */
    if (*p < 0x80) return 1;
    if ((*p & 0xE0) == 0xC0) return 2;
    if ((*p & 0xF0) == 0xE0) return 3;
    return 4;
}

#define XL_PUT(s) do { const char *z_ = (s); while (*z_ && n < om - 1) o[n++] = *z_++; } while (0)

/* английские фонемы -> кириллица для русского голоса */
static void xl_ipa2cyr(const char *ipa, char *o, int om) {
    static const XlTok T[] = {
        {"tʃ", "ч"}, {"dʒ", "дж"}, {"aɪ", "ай"}, {"eɪ", "эй"}, {"ɔɪ", "ой"}, {"aʊ", "ау"}, {"oʊ", "оу"}, {"əʊ", "оу"},
        {"ɪə", "иэ"}, {"eə", "эа"}, {"ʊə", "уа"}, {"ɚ", "эр"}, {"ɝ", "ёр"}, {"ɜ", "ё"},
        {"i", "и"}, {"ɪ", "и"}, {"ᵻ", "и"}, {"e", "э"}, {"ɛ", "э"}, {"æ", "э"}, {"a", "а"}, {"ɑ", "а"}, {"ɒ", "о"},
        {"ɔ", "о"}, {"ɵ", "о"}, {"o", "о"}, {"ʊ", "у"}, {"u", "у"}, {"ʌ", "а"}, {"ɐ", "а"}, {"ɨ", "ы"},
        {"p", "п"}, {"b", "б"}, {"t", "т"}, {"d", "д"}, {"ɾ", "т"}, {"k", "к"}, {"ɡ", "г"}, {"g", "г"}, {"f", "ф"},
        {"v", "в"}, {"θ", "с"}, {"ð", "з"}, {"s", "с"}, {"z", "з"}, {"ʃ", "ш"}, {"ʒ", "ж"}, {"h", "х"}, {"m", "м"},
        {"n", "н"}, {"ŋ", "нг"}, {"l", "л"}, {"ɫ", "л"}, {"ɹ", "р"}, {"r", "р"}, {"w", "у"}, {"x", "х"}, {"ʔ", ""},
        {" ", " "}, {0, 0}};
    int n = 0;
    const unsigned char *p = (const unsigned char *)ipa;
    while (*p && n < om - 8) {
        int sk = xl_skip(p);
        if (sk) { p += sk; continue; }
        if (*p == 'j') {                           /* й + гласная = я/ю/е/ё */
            const unsigned char *q = p + 1;
            while (xl_skip(q)) q += xl_skip(q);
            const char *r = NULL;
            if (!strncmp((const char *)q, "u", 1) || !strncmp((const char *)q, "ʊ", 2)) r = "ю";
            else if (!strncmp((const char *)q, "a", 1) || !strncmp((const char *)q, "ɑ", 2) || !strncmp((const char *)q, "æ", 2) || !strncmp((const char *)q, "ʌ", 2)) r = "я";
            else if (!strncmp((const char *)q, "e", 1) || !strncmp((const char *)q, "ɛ", 2)) r = "е";
            else if (!strncmp((const char *)q, "o", 1) || !strncmp((const char *)q, "ɔ", 2)) r = "ё";
            if (r) { XL_PUT(r); p = q + xl_cpl(q); continue; }
            XL_PUT("й"); p++; continue;
        }
        if (!strncmp((const char *)p, "ə", 2)) {      /* шва в конце слова — «а» (vita -> вита), иначе «э» */
            const unsigned char *q = p + 2;
            while (xl_skip(q)) q += xl_skip(q);
            XL_PUT((*q == 0 || *q == ' ') ? "а" : "э");
            p += 2; continue;
        }
        int hit = 0;
        for (int i = 0; T[i].ipa; i++) {
            int l = strlen(T[i].ipa);
            if (!strncmp((const char *)p, T[i].ipa, l)) { XL_PUT(T[i].out); p += l; hit = 1; break; }
        }
        if (!hit) p += xl_cpl(p);                  /* незнакомый знак пропускаем */
    }
    o[n] = 0;
}

/* русские фонемы -> латиница для голоса языка lang (EN, DE, ES, FR, IT) */
static void xl_ipa2lat(const char *ipa, int lang, char *o, int om) {
    /* по столбцам: EN, DE, ES, FR, IT */
    static const struct { const char *ipa, *out[5]; } T[] = {
        {"tʃ", {"ch", "tsch", "ch", "tch", "c"}},
        {"ts", {"ts", "z", "ts", "ts", "z"}},
        {"ɕ", {"sh", "sch", "sh", "ch", "sc"}},
        {"ʂ", {"sh", "sch", "sh", "ch", "sc"}},
        {"ʃ", {"sh", "sch", "sh", "ch", "sc"}},
        {"ʐ", {"zh", "sch", "y", "j", "sg"}},
        {"ʒ", {"zh", "sch", "y", "j", "sg"}},
        {"x", {"h", "ch", "j", "kh", "h"}},
        {"j", {"y", "j", "y", "y", "i"}},
        {"i", {"ee", "i", "i", "i", "i"}},
        {"ɪ", {"i", "i", "i", "i", "i"}},
        {"ɨ", {"i", "ü", "i", "u", "i"}},
        {"y", {"i", "ü", "i", "u", "i"}},
        {"e", {"e", "e", "e", "é", "e"}},
        {"ɛ", {"e", "e", "e", "è", "e"}},
        {"a", {"a", "a", "a", "a", "a"}},
        {"ɑ", {"a", "a", "a", "a", "a"}},
        {"ʌ", {"a", "a", "a", "a", "a"}},
        {"ɐ", {"a", "a", "a", "a", "a"}},
        {"ə", {"a", "e", "a", "e", "a"}},
        {"o", {"o", "o", "o", "o", "o"}},
        {"ɵ", {"o", "o", "o", "o", "o"}},
        {"ɔ", {"o", "o", "o", "o", "o"}},
        {"u", {"oo", "u", "u", "ou", "u"}},
        {"ʊ", {"oo", "u", "u", "ou", "u"}},
        {"ʉ", {"oo", "ü", "u", "u", "u"}},
        {"ɭ", {"l", "l", "l", "l", "l"}},
        {"ɫ", {"l", "l", "l", "l", "l"}},
        {"ɾ", {"r", "r", "r", "r", "r"}},
        {"ɡ", {"g", "g", "g", "g", "g"}},
        {"ɣ", {"g", "g", "g", "g", "g"}},
        {"ŋ", {"n", "n", "n", "n", "n"}},
        {"ç", {"h", "ch", "j", "h", "h"}},
        {"w", {"v", "w", "v", "v", "v"}},
        {0, {0, 0, 0, 0, 0}}};
    int L = lang == XL_EN ? 0 : lang - 1;
    if (L < 0 || L > 4) L = 0;
    int n = 0, soft_ok = 1;                        /* после ч/щ/й мягкость не пишем */
    const unsigned char *p = (const unsigned char *)ipa;
    while (*p && n < om - 8) {
        int sk = xl_skip(p);
        if (sk) { p += sk; continue; }
        if (p[0] == 0xCA && p[1] == 0xB2) {        /* ʲ: перед гласной даёт «y»/«j»/«i» (vye, vie) */
            const unsigned char *q = p + 2;
            while (xl_skip(q)) q += xl_skip(q);
            if (soft_ok && xl_isv((const char *)q) && strncmp((const char *)q, "i", 1) && strncmp((const char *)q, "ɪ", 2)) {
                static const char *Y[5] = {"y", "j", "i", "i", "i"};
                XL_PUT(Y[L]);
            }
            p += 2; continue;
        }
        if (L == 0 && *p == 'e') {                 /* английское: «е» в конце слова читается -> eh */
            const unsigned char *q = p + 1;
            while (xl_skip(q)) q += xl_skip(q);
            if (*q == 0 || *q == ' ') { XL_PUT("eh"); p++; soft_ok = 1; continue; }
        }
        int hit = 0;
        for (int i = 0; T[i].ipa; i++) {
            int l = strlen(T[i].ipa);
            if (!strncmp((const char *)p, T[i].ipa, l)) {
                XL_PUT(T[i].out[L]);
                soft_ok = !(!strcmp(T[i].ipa, "tʃ") || !strcmp(T[i].ipa, "ɕ") || !strcmp(T[i].ipa, "j"));
                p += l; hit = 1; break;
            }
        }
        if (hit) continue;
        if (*p < 0x80) { if (n < om - 1) o[n++] = (char)*p; soft_ok = 1; p++; }  /* обычные латинские согласные и пробел */
        else p += xl_cpl(p);
    }
    o[n] = 0;
}
#undef XL_PUT

/* Разбор текста по словам: слова «чужого» алфавита переводятся через фонемы (phon), остальное как есть.
 * phon(слово_в_нижнем_регистре, язык_слова) -> строка IPA или NULL; fb — запасной побуквенный перевод слова. */
typedef const char *(*XlPhon)(const char *word, int srclang);
typedef void (*XlFallback)(const char *word, int tlang, char *o, int om);

static int xl_cyr(const unsigned char *p) { return (p[0] == 0xD0 || p[0] == 0xD1) && p[1] >= 0x80; }
static int xl_lat(const unsigned char *p) { return (*p >= 'A' && *p <= 'Z') || (*p >= 'a' && *p <= 'z'); }

static void xl_lower(const char *s, char *d, int dm) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)s;
    while (*p && n < dm - 3) {
        if (*p >= 'A' && *p <= 'Z') { d[n++] = (char)(*p + 32); p++; }
        else if (p[0] == 0xD0 && p[1] >= 0x90 && p[1] <= 0x9F) { d[n++] = (char)0xD0; d[n++] = (char)(p[1] + 0x20); p += 2; }
        else if (p[0] == 0xD0 && p[1] >= 0xA0 && p[1] <= 0xAF) { d[n++] = (char)0xD1; d[n++] = (char)(p[1] - 0x20); p += 2; }
        else if (p[0] == 0xD0 && p[1] == 0x81) { d[n++] = (char)0xD1; d[n++] = (char)0x91; p += 2; }
        else d[n++] = (char)*p++;
    }
    d[n] = 0;
}

static void xl_text(const char *in, int tlang, char *o, int om, XlPhon phon, XlFallback fb) {
    int n = 0;
    const unsigned char *p = (const unsigned char *)in;
    char word[256], low[256], tmp[1024];
    while (*p && n < om - 2) {
        if (*p == ' ' || *p == '\t' || *p == '\n') { o[n++] = (char)*p++; continue; }
        int wl = 0;
        while (p[wl] && p[wl] != ' ' && p[wl] != '\t' && p[wl] != '\n' && wl < 250) wl++;
        /* слово: ведущая пунктуация, ядро из букв, хвост */
        int a = 0, b = wl;
        while (a < b && !xl_lat(p + a) && !xl_cyr(p + a)) a++;
        while (b > a && !xl_lat(p + b - 1) && !(b >= 2 && xl_cyr(p + b - 2))) b--;
        int nf = 0, nn = 0;
        for (int i = a; i < b; i++) {
            if (xl_lat(p + i)) { if (tlang == XL_RU) nf++; else nn++; }
            else if (xl_cyr(p + i)) { if (tlang == XL_RU) nn++; else nf++; i++; }
        }
        for (int i = 0; i < a && n < om - 1; i++) o[n++] = (char)p[i];
        if (nf > 0 && b > a) {
            memcpy(word, p + a, b - a); word[b - a] = 0;
            tmp[0] = 0;
            const char *ipa = NULL;
            if (nn == 0 && phon) { xl_lower(word, low, sizeof low); ipa = phon(low, tlang == XL_RU ? XL_EN : XL_RU); }
            if (ipa && *ipa) {
                if (tlang == XL_RU) xl_ipa2cyr(ipa, tmp, sizeof tmp); else xl_ipa2lat(ipa, tlang, tmp, sizeof tmp);
            } else if (fb) fb(word, tlang, tmp, sizeof tmp);
            else snprintf(tmp, sizeof tmp, "%s", word);
            for (const char *z = tmp; *z && n < om - 1; z++) o[n++] = *z;
        } else for (int i = a; i < b && n < om - 1; i++) o[n++] = (char)p[i];
        for (int i = b; i < wl && n < om - 1; i++) o[n++] = (char)p[i];
        p += wl;
    }
    o[n] = 0;
}
