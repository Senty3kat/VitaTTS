/* Хост-превью: настоящий ui_draw() из main.c рисуется программно (PNG-текстуры через libpng, шрифты через FreeType).
 * Сборка и запуск: make -C host preview  ->  /tmp/shots/*.png (смотреть глазами!) */
#define TTS_NO_MAIN
#define ASSET_PREFIX "../assets/"
#define FONT_PREFIX "../fonts/"
#include "../main.c"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <png.h>

#define SCR_W 960
#define SCR_H 544
struct vita2d_texture { int w, h; uint32_t *px; };
struct vita2d_font { FT_Face face; };
static uint32_t *g_fb;
static FT_Library g_ft;

vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h) {
    vita2d_texture *t = malloc(sizeof(*t)); t->w = (int)w; t->h = (int)h; t->px = calloc((size_t)w * h, 4); return t;
}
void vita2d_free_texture(vita2d_texture *t) { if (t) { free(t->px); free(t); } }
vita2d_texture *vita2d_load_PNG_file(const char *fn) {
    FILE *f = fopen(fn, "rb");
    if (!f) { fprintf(stderr, "нет файла %s\n", fn); return NULL; }
    png_structp p = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
    png_infop i = png_create_info_struct(p);
    if (setjmp(png_jmpbuf(p))) return NULL;
    png_init_io(p, f);
    png_read_info(p, i);
    int w = png_get_image_width(p, i), h = png_get_image_height(p, i);
    png_set_expand(p); png_set_strip_16(p); png_set_gray_to_rgb(p);
    png_set_filler(p, 0xFF, PNG_FILLER_AFTER);
    png_read_update_info(p, i);
    vita2d_texture *t = vita2d_create_empty_texture(w, h);
    png_bytep *rows = malloc(sizeof(png_bytep) * h);
    for (int y = 0; y < h; y++) rows[y] = (png_bytep)(t->px + (size_t)y * w);
    png_read_image(p, rows);
    free(rows); fclose(f);
    if (png_get_color_type(p, i) == PNG_COLOR_TYPE_RGB) for (int k = 0; k < w * h; k++) t->px[k] |= 0xFF000000u;
    return t;
}
static void blend(int x, int y, uint32_t c, int cover) {
    if (x < 0 || y < 0 || x >= SCR_W || y >= SCR_H) return;
    int a = ((int)(c >> 24) * cover) / 255;
    uint32_t *d = &g_fb[y * SCR_W + x];
    int dr = *d & 255, dg = (*d >> 8) & 255, db = (*d >> 16) & 255;
    int r = (int)(c & 255), g = (int)((c >> 8) & 255), b = (int)((c >> 16) & 255);
    dr += (r - dr) * a / 255; dg += (g - dg) * a / 255; db += (b - db) * a / 255;
    *d = RGBA8(dr, dg, db, 255);
}
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int c) {
    for (int j = (int)y; j < (int)(y + h); j++) for (int i = (int)x; i < (int)(x + w); i++) blend(i, j, c, 255);
}
void vita2d_draw_texture_part(const vita2d_texture *t, float x, float y, float tx, float ty, float tw, float th) {
    for (int j = 0; j < (int)th; j++) for (int i = 0; i < (int)tw; i++) {
        int sx = (int)tx + i, sy = (int)ty + j;
        if (sx < 0 || sy < 0 || sx >= t->w || sy >= t->h) continue;
        blend((int)lroundf(x) + i, (int)lroundf(y) + j, t->px[sy * t->w + sx], 255);
    }
}
void vita2d_draw_texture(const vita2d_texture *t, float x, float y) { vita2d_draw_texture_part(t, x, y, 0, 0, (float)t->w, (float)t->h); }
int vita2d_init_advanced(unsigned int n) { (void)n; return 1; }
int vita2d_init(void) { return 1; }
int vita2d_fini(void) { return 1; }
void vita2d_clear_screen(void) { for (int i = 0; i < SCR_W * SCR_H; i++) g_fb[i] = 0xFF000000u; }
void vita2d_set_clear_color(unsigned int c) { (void)c; }
void vita2d_start_drawing(void) {}
void vita2d_end_drawing(void) {}
void vita2d_swap_buffers(void) {}
void vita2d_wait_rendering_done(void) {}
vita2d_pvf *vita2d_load_default_pvf(void) { return NULL; }
vita2d_pvf *vita2d_load_custom_pvf(const char *p) { (void)p; return NULL; }
int vita2d_pvf_draw_text(vita2d_pvf *f, int x, int y, unsigned int c, float s, const char *t) { (void)f; (void)x; (void)y; (void)c; (void)s; (void)t; return 0; }
uint64_t sceKernelGetProcessTimeWide(void) { return 0; }

vita2d_font *vita2d_load_font_file(const char *fn) {
    const char *map = strstr(fn, "mono") ? "DejaVuSansMono-Bold.ttf" : "DejaVuSansCondensed-Bold.ttf";
    char path[256]; snprintf(path, sizeof path, "../fonts/%s", map);
    vita2d_font *f = malloc(sizeof(*f));
    if (FT_New_Face(g_ft, path, 0, &f->face)) { free(f); return NULL; }
    return f;
}
int vita2d_font_draw_text(vita2d_font *f, int x, int y, unsigned int c, unsigned int size, const char *t) {
    FT_Set_Pixel_Sizes(f->face, 0, size);
    const unsigned char *p = (const unsigned char *)t;
    while (*p) {
        int cp = dec(&p);
        if (FT_Load_Char(f->face, (FT_ULong)cp, FT_LOAD_RENDER)) continue;
        FT_GlyphSlot g = f->face->glyph;
        for (unsigned j = 0; j < g->bitmap.rows; j++) for (unsigned i = 0; i < g->bitmap.width; i++)
            blend(x + g->bitmap_left + (int)i, y - g->bitmap_top + (int)j, c, g->bitmap.buffer[j * g->bitmap.pitch + i]);
        x += (int)(g->advance.x >> 6);
    }
    return 0;
}
int vita2d_font_text_width(vita2d_font *f, unsigned int size, const char *t) {
    FT_Set_Pixel_Sizes(f->face, 0, size);
    const unsigned char *p = (const unsigned char *)t;
    int w = 0;
    while (*p) { int cp = dec(&p); if (!FT_Load_Char(f->face, (FT_ULong)cp, FT_LOAD_DEFAULT)) w += (int)(f->face->glyph->advance.x >> 6); }
    return w;
}

int main(int argc, char **argv) {
    const char *outp = argc > 1 ? argv[1] : "/tmp/frame.raw";
    const char *sc = argc > 2 ? argv[2] : "main";
    g_fb = calloc((size_t)SCR_W * SCR_H, 4);
    FT_Init_FreeType(&g_ft);
    ev[nev++] = (Voice){"Pico US (SVOX)", 0, 0, 0, NULL};
    ev[nev++] = (Voice){"Pico UK (SVOX)", 0, 1, 0, NULL};
    ev[nev++] = (Voice){"eSpeak EN (retro)", 1, 0, 0, NULL};
    ev[nev++] = (Voice){"SAM C64", 3, 0, 0, NULL};
    ev[nev++] = (Voice){"RHVoice Aleksandr (RU)", 5, 0, 1, "Aleksandr-hq"};
    ev[nev++] = (Voice){"RHVoice Anna (RU)", 5, 1, 1, "Anna"};
    ev[nev++] = (Voice){"eSpeak RU (retro)", 1, 1, 1, NULL};
    cur_en = 0; cur_ru = 4; cur = 0;
    snprintf(hist[0], HL, "Hello, this is VITA. Experimental speech for electronic music.");
    snprintf(hist[1], HL, "\xD0\x9F\xD1\x80\xD0\xB8\xD0\xB2\xD0\xB5\xD1\x82, \xD1\x8D\xD1\x82\xD0\xBE \xD0\x92\xD0\xB8\xD1\x82\xD0\xB0. \xD0\xAD\xD0\xBA\xD1\x81\xD0\xBF\xD0\xB5\xD1\x80\xD0\xB8\xD0\xBC\xD0\xB5\xD0\xBD\xD1\x82\xD0\xB0\xD0\xBB\xD1\x8C\xD0\xBD\xD0\xB0\xD1\x8F \xD1\x80\xD0\xB5\xD1\x87\xD1\x8C.");
    snprintf(hist[2], HL, "Kraftwerk style: we are the robots, we are the robots, we are the robots");
    snprintf(hist[3], HL, "one two three four");
    snprintf(hist[4], HL, "Computer love");
    hn = 5; sel = 0;
    g_ready = 1; g_synth_ms = 480; g_audio_ms = 2300;
    ui_load();
    if (!strcmp(sc, "ru")) { sel = 1; cur = 4; }
    if (!strcmp(sc, "long")) { sel = 2; for (int i = 0; i < 6; i++) strcat(hist[2], " and again we are the robots"); }
    if (!strcmp(sc, "empty")) { hn = 0; }
    if (!strcmp(sc, "press")) { u_down = 1; u_tx = 400; u_ty = 400; }
    if (!strcmp(sc, "speak")) { g_busy = 1; speed = 130; pitch = 80; style = 2; }
    if (!strncmp(sc, "syl", 3)) {
        g_sylmode = 1; g_sy_ready = 1; nsyl = 9; sy_idx = 4; g_sy_last = 3; quant = 1; subdiv = 2; bpm = 124;
        if (!strcmp(sc, "syl_wait")) g_sy_ready = 0;
    }
    ui_draw();
    FILE *f = fopen(outp, "wb"); fwrite(g_fb, 4, (size_t)SCR_W * SCR_H, f); fclose(f);
    return 0;
}
