/* заглушка vita2d для хост-сборки: сигнатуры как в настоящем vita2d.h */
#pragma once
#include <stdint.h>
#ifndef RGBA8
#define RGBA8(r,g,b,a) ((uint32_t)(((a)<<24)|((b)<<16)|((g)<<8)|(r)))
#endif
typedef struct vita2d_texture vita2d_texture;
typedef struct vita2d_pgf vita2d_pgf;
typedef struct vita2d_font vita2d_font;
typedef struct vita2d_pvf vita2d_pvf;
int vita2d_init(void);
int vita2d_init_advanced(unsigned int temp_pool_size);
int vita2d_fini(void);
void vita2d_clear_screen(void);
void vita2d_set_clear_color(unsigned int color);
void vita2d_start_drawing(void);
void vita2d_end_drawing(void);
void vita2d_swap_buffers(void);
void vita2d_common_dialog_update(void);
void vita2d_wait_rendering_done(void);
vita2d_texture *vita2d_create_empty_texture(unsigned int w, unsigned int h);
void vita2d_free_texture(vita2d_texture *texture);
void *vita2d_texture_get_datap(const vita2d_texture *texture);
unsigned int vita2d_texture_get_stride(const vita2d_texture *texture);
void vita2d_draw_texture(const vita2d_texture *texture, float x, float y);
void vita2d_draw_texture_part(const vita2d_texture *texture, float x, float y, float tex_x, float tex_y, float tex_w, float tex_h);
void vita2d_draw_rectangle(float x, float y, float w, float h, unsigned int color);
vita2d_pgf *vita2d_load_default_pgf(void);
void vita2d_free_pgf(vita2d_pgf *font);
int vita2d_pgf_draw_text(vita2d_pgf *font, int x, int y, unsigned int color, float scale, const char *text);
vita2d_font *vita2d_load_font_file(const char *filename);
void vita2d_free_font(vita2d_font *font);
int vita2d_font_draw_text(vita2d_font *font, int x, int y, unsigned int color, unsigned int size, const char *text);
int vita2d_font_text_width(vita2d_font *font, unsigned int size, const char *text);
vita2d_pvf *vita2d_load_default_pvf(void);
vita2d_pvf *vita2d_load_custom_pvf(const char *path);
void vita2d_free_pvf(vita2d_pvf *font);
int vita2d_pvf_draw_text(vita2d_pvf *font, int x, int y, unsigned int color, float scale, const char *text, ...);
