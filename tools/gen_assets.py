#!/usr/bin/env python3
"""Генератор PNG-интерфейса VitaTTS («+VITA»): рисуется с 4-кратной супервыборкой (PIL + numpy).
Запуск: python3 tools/gen_assets.py
Результат:
  assets/body_main.png, assets/body_syl.png — готовый фон экрана 960x544 (корпус, ЖК, панели, канавки ползунков, подписи)
  assets/atlas.png                          — атлас кнопок, чипов, карточек, ползунка, лампочек, значков
  ui_atlas.h                                — координаты спрайтов в атласе (подключается в main.c, не править руками)
  design/parts/*.png                        — те же детали по отдельности (для просмотра и правок)
Свет падает сверху-слева. Размеры спрайтов задаются здесь же, main.c берёт их из ui_atlas.h."""
import os
import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter, ImageChops

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, "..")
SS = 4
W, H = 960, 544
FB = os.path.join(ROOT, "fonts", "DejaVuSansCondensed-Bold.ttf")
FBO = "/usr/share/fonts/truetype/dejavu/DejaVuSansCondensed-BoldOblique.ttf"
FM = os.path.join(ROOT, "fonts", "DejaVuSansMono-Bold.ttf")
FSY = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf"   # для значков


def font(path, size):
    return ImageFont.truetype(path, size)


# ---------- палитра ----------
CREAM_L = (247, 241, 226)
CREAM = (235, 227, 205)
CREAM_D = (214, 204, 178)
EDGE = (148, 136, 110)
EDGE_D = (110, 100, 80)
INK = (58, 46, 32)
INK_L = (120, 108, 88)
YEL = (252, 208, 52)
YEL_D = (232, 168, 22)
ORG = (244, 116, 52)
ORG_D = (214, 78, 28)
RED = (208, 52, 44)
LCD_T = (244, 206, 52)
LCD_B = (222, 172, 24)
LCD_INK = (54, 38, 10)
CHIPS = [  # цвета чипов: оранжевый, зелёный, синий, фиолетовый, розовый, жёлтый
    (246, 118, 62), (128, 190, 150), (96, 164, 214), (156, 120, 216), (236, 150, 176), (240, 196, 60)]
HIST = [(246, 214, 150), (190, 226, 200), (178, 210, 238), (214, 196, 242), (244, 200, 214)]
ENG = [(86, 140, 214), (240, 130, 40), (90, 170, 100), (210, 60, 60)]   # точки движков: pico, espeak, sam, rhvoice


# ---------- примитивы ----------
def mask_rr(w, h, r, inset=0.0):
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    i = inset * SS
    d.rounded_rectangle([i, i, w * SS - 1 - i, h * SS - 1 - i], radius=max(0.0, (r - inset)) * SS, fill=255)
    return big.resize((w, h), Image.BOX)


def mask_ell(w, h, inset=0.0):
    big = Image.new("L", (w * SS, h * SS), 0)
    d = ImageDraw.Draw(big)
    i = inset * SS
    d.ellipse([i, i, w * SS - 1 - i, h * SS - 1 - i], fill=255)
    return big.resize((w, h), Image.BOX)


def vgrad(w, h, c0, c1):
    t = np.linspace(0, 1, h)[:, None, None]
    a = np.array(c0, np.float32) * (1 - t) + np.array(c1, np.float32) * t
    return Image.fromarray(np.broadcast_to(a, (h, w, 3)).astype(np.uint8), "RGB")


def solid(w, h, c):
    return Image.new("RGB", (w, h), tuple(c))


def put(canvas, rgb, mask, x=0, y=0, alpha=1.0):
    """кладёт цветную картинку rgb по маске mask на canvas (RGBA)"""
    m = mask if alpha >= 1 else mask.point(lambda v: int(v * alpha))
    layer = Image.new("RGBA", canvas.size, (0, 0, 0, 0))
    lay = rgb.convert("RGBA")
    if lay.size != m.size:
        lay = lay.resize(m.size)
    lay.putalpha(m)
    layer.paste(lay, (x, y))
    canvas.alpha_composite(layer)


def shift(mask, dx, dy, size=None):
    s = size or mask.size
    out = Image.new("L", s, 0)
    out.paste(mask, (dx, dy))
    return out


def blur(mask, r):
    return mask.filter(ImageFilter.GaussianBlur(r))


def shadow(canvas, mask, x, y, dy=3, r=3, a=0.32, dx=0):
    big = Image.new("L", canvas.size, 0)
    big.paste(mask, (x + dx, y + dy))
    put(canvas, solid(*canvas.size, (40, 28, 10)), blur(big, r), 0, 0, a)


def crescent(mask, d, up=False):
    """полоска у верхнего (или нижнего) края маски толщиной d"""
    sh = shift(mask, 0, -d if up else d)
    return ImageChops.subtract(mask, sh)


def inner_shadow(canvas, mask, x, y, dy=2, r=2.5, a=0.45, col=(60, 40, 10)):
    inv = ImageChops.invert(mask)
    sh = blur(shift(inv, 0, dy), r)
    m = ImageChops.multiply(sh, mask)
    big = Image.new("L", canvas.size, 0)
    big.paste(m, (x, y))
    put(canvas, solid(*canvas.size, col), big, 0, 0, a)


def raised(w, h, r, top, bot, pressed=False, pad=7, rim=None):
    """объёмная плашка (кнопка): тень, кромка, градиент, блик. Возвращает RGBA размера (w+2pad, h+2pad)"""
    cv = Image.new("RGBA", (w + 2 * pad, h + 2 * pad), (0, 0, 0, 0))
    m = mask_rr(w, h, r)
    if pressed:
        shadow(cv, m, pad, pad, dy=1, r=1.5, a=0.25)
    else:
        shadow(cv, m, pad, pad, dy=3, r=3, a=0.34)
    oy = 1 if pressed else 0
    rc = rim or (EDGE, EDGE_D)
    put(cv, vgrad(w, h, *rc), m, pad, pad + oy)
    fm = mask_rr(w, h, r, inset=2)
    if pressed:
        t2 = tuple(int(c * 0.9) for c in bot)
        b2 = tuple(int(c * 0.97) for c in top)
        put(cv, vgrad(w, h, t2, b2), fm, pad, pad + oy)
        inner_shadow(cv, fm, pad, pad + oy, dy=3, r=2.5, a=0.35)
    else:
        put(cv, vgrad(w, h, top, bot), fm, pad, pad)
        gm = ImageChops.subtract(mask_rr(w, h, r, inset=3), shift(mask_rr(w, h, r, inset=3), 0, 3))
        put(cv, solid(w, h, (255, 255, 255)), gm, pad, pad, 0.6)
        lm = ImageChops.subtract(shift(mask_rr(w, h, r, inset=3), 0, -3), shift(mask_rr(w, h, r, inset=3), 0, 0))
        lm = ImageChops.subtract(mask_rr(w, h, r, inset=3), shift(mask_rr(w, h, r, inset=3), 0, -3))
        put(cv, solid(w, h, (120, 90, 30)), lm, pad, pad, 0.10)
    return cv


def sunk(cv, x, y, w, h, r, top, bot, border=True, a=0.45):
    """вдавленная панель прямо на canvas"""
    m = mask_rr(w, h, r)
    if border:
        lm = ImageChops.subtract(shift(m, 0, 1, m.size), Image.new("L", m.size, 0))
        big = Image.new("L", cv.size, 0)
        big.paste(m, (x, y + 1))
        put(cv, solid(*cv.size, (255, 255, 255)), big, 0, 0, 0.8)    # светлая кромка снизу
    put(cv, vgrad(w, h, top, bot), m, x, y)
    inner_shadow(cv, m, x, y, dy=2, r=2.2, a=a)
    ring = ImageChops.subtract(m, mask_rr(w, h, r, inset=1))
    put(cv, solid(w, h, EDGE), ring, x, y, 0.55)


def text(cv, xy, s, f, fill, anchor="la", spacing=0):
    d = ImageDraw.Draw(cv)
    if spacing:
        x, y = xy
        for ch in s:
            d.text((x, y), ch, font=f, fill=fill, anchor=anchor)
            x += d.textlength(ch, font=f) + spacing
    else:
        d.text(xy, s, font=f, fill=fill, anchor=anchor)


def text_w(s, f, spacing=0):
    d = ImageDraw.Draw(Image.new("RGB", (4, 4)))
    return d.textlength(s, font=f) + spacing * max(0, len(s) - 1)


def screw(cv, cx, cy, r=6):
    sz = r * 2 + 8
    c = Image.new("RGBA", (sz, sz), (0, 0, 0, 0))
    m = mask_ell(r * 2, r * 2)
    shadow(c, m, 4, 4, dy=1, r=1.2, a=0.35)
    put(c, vgrad(r * 2, r * 2, (230, 224, 206), (160, 150, 128)), m, 4, 4)
    d = ImageDraw.Draw(c)
    d.line([(4 + 3, 4 + r + 1), (4 + 2 * r - 3, 4 + r - 1)], fill=(96, 86, 68, 255), width=2)
    cv.alpha_composite(c, (cx - sz // 2, cy - sz // 2))


# ---------- спрайты ----------
SPR = {}   # имя -> (RGBA картинка, pad)


def reg(name, img, pad):
    SPR[name] = (img, pad)


def make_buttons():
    styles = {"cream": (CREAM_L, CREAM_D, None), "yellow": ((255, 224, 96), (240, 176, 28), (EDGE, (140, 100, 20))),
              "orange": ((255, 156, 96), ORG_D, ((170, 80, 40), (120, 50, 20)))}
    for sz, w, h, r in (("btn", 188, 44, 12), ("pill", 140, 36, 10), ("big", 330, 100, 24)):
        for st, (t, b, rim) in styles.items():
            if sz == "big" and st == "cream":
                continue
            for pr in (0, 1):
                reg(f"{sz}_{st}{'_p' if pr else ''}", raised(w, h, r, t, b, bool(pr), rim=rim), 7)


def make_chips():
    for i, c in enumerate(CHIPS):
        on_t = tuple(min(255, int(v * 1.12 + 18)) for v in c)
        on_b = tuple(int(v * 0.86) for v in c)
        off_t = tuple(int(v * 0.30 + CREAM_L[k] * 0.70) for k, v in enumerate(c))
        off_b = tuple(int(v * 0.30 + CREAM_D[k] * 0.70) for k, v in enumerate(c))
        reg(f"chip{i}_off", raised(102, 30, 9, off_t, off_b, False, pad=5, rim=((176, 164, 138), (138, 126, 102))), 5)
        cv = raised(102, 30, 9, on_t, on_b, True, pad=5, rim=((tuple(int(v * 0.6) for v in c)), tuple(int(v * 0.5) for v in c)))
        reg(f"chip{i}_on", cv, 5)


def make_cards():
    for i, c in enumerate(HIST):
        t = tuple(min(255, int(v * 1.04 + 4)) for v in c)
        b = tuple(int(v * 0.90) for v in c)
        reg(f"hc{i}", raised(308, 30, 8, t, b, False, pad=4, rim=((190, 176, 150), (150, 136, 110))), 4)
        dk = tuple(int(v * 0.55) for v in c)
        cv = raised(308, 30, 8, tuple(min(255, v + 6) for v in c), tuple(int(v * 0.95) for v in c), False, pad=4, rim=(dk, dk))
        m = mask_rr(308, 30, 8)
        ring = ImageChops.subtract(m, mask_rr(308, 30, 8, inset=2.4))
        put(cv, solid(308, 30, (200, 60, 30)), ring, 4, 4, 0.95)
        reg(f"hc{i}_sel", cv, 4)


def make_row_sel():
    w, h = 308, 28
    cv = Image.new("RGBA", (w + 8, h + 8), (0, 0, 0, 0))
    m = mask_rr(w, h, 7)
    shadow(cv, m, 4, 4, dy=2, r=2, a=0.3)
    put(cv, vgrad(w, h, (170, 90, 30), (140, 64, 16)), m, 4, 4)
    put(cv, vgrad(w, h, (255, 190, 90), (246, 140, 52)), mask_rr(w, h, 7, inset=1.5), 4, 4)
    put(cv, solid(w, h, (255, 255, 255)), ImageChops.subtract(mask_rr(w, h, 7, inset=2.5), shift(mask_rr(w, h, 7, inset=2.5), 0, 3)), 4, 4, 0.45)
    reg("rowsel", cv, 4)


def make_thumb():
    d = 30
    for pr in (0, 1):
        pad = 6
        cv = Image.new("RGBA", (d + 2 * pad, d + 2 * pad), (0, 0, 0, 0))
        m = mask_ell(d, d)
        shadow(cv, m, pad, pad, dy=1 if pr else 3, r=2.5, a=0.4)
        put(cv, vgrad(d, d, EDGE, EDGE_D), m, pad, pad)
        top, bot = ((236, 230, 212), (200, 190, 164)) if pr else ((252, 248, 236), (214, 204, 178))
        put(cv, vgrad(d, d, top, bot), mask_ell(d, d, 2), pad, pad)
        put(cv, solid(d, d, (255, 255, 255)), ImageChops.subtract(mask_ell(d, d, 3.5), shift(mask_ell(d, d, 3.5), 2, 4)), pad, pad, 0.7)
        cm = mask_ell(10, 10)
        put(cv, vgrad(10, 10, (255, 150, 90), ORG_D), cm, pad + d // 2 - 5, pad + d // 2 - 5)
        reg("thumb_p" if pr else "thumb", cv, pad)


def make_leds():
    cols = {"off": (150, 140, 120), "green": (60, 210, 90), "orange": (255, 150, 30), "red": (240, 50, 40)}
    for name, c in cols.items():
        s = 16
        cv = Image.new("RGBA", (s + 12, s + 12), (0, 0, 0, 0))
        if name != "off":
            g = mask_ell(s, s).resize((s + 12, s + 12))
            gl = Image.new("L", cv.size, 0)
            gl.paste(mask_ell(s + 4, s + 4), (4, 4))
            put(cv, solid(*cv.size, c), blur(gl, 4), 0, 0, 0.7)
        m = mask_ell(s, s)
        put(cv, vgrad(s, s, (90, 80, 62), (50, 44, 34)), m, 6, 6)
        lit = tuple(int(v * (1.0 if name != "off" else 0.8)) for v in c)
        hi = tuple(min(255, v + 80) for v in c) if name != "off" else (180, 170, 150)
        put(cv, vgrad(s, s, hi, lit), mask_ell(s, s, 2), 6, 6)
        put(cv, solid(s, s, (255, 255, 255)), mask_ell(6, 5), 6 + 4, 6 + 3, 0.7 if name != "off" else 0.3)
        reg(f"led_{name}", cv, 6)
    for i, c in enumerate(ENG):
        s = 12
        cv = Image.new("RGBA", (s + 4, s + 4), (0, 0, 0, 0))
        m = mask_ell(s, s)
        put(cv, solid(s, s, EDGE_D), m, 2, 2)
        put(cv, vgrad(s, s, tuple(min(255, v + 60) for v in c), c), mask_ell(s, s, 1.5), 2, 2)
        put(cv, solid(s, s, (255, 255, 255)), mask_ell(4, 3), 2 + 3, 2 + 2, 0.6)
        reg(f"dot{i}", cv, 2)


def icon_canvas(s=28):
    return Image.new("RGBA", (s * SS, s * SS), (0, 0, 0, 0)), s


def fin(cv, s):
    return cv.resize((s, s), Image.BOX)


def make_icons():
    ink = INK + (255,)
    # play
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.polygon([(8 * SS, 4 * SS), (24 * SS, 14 * SS), (8 * SS, 24 * SS)], fill=ink)
    reg("ic_play", fin(cv, s), 0)
    # play большой
    cv, s = icon_canvas(56)
    d = ImageDraw.Draw(cv)
    d.polygon([(16 * SS, 8 * SS), (48 * SS, 28 * SS), (16 * SS, 48 * SS)], fill=ink)
    reg("ic_play_big", fin(cv, s), 0)
    # stop
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.rounded_rectangle([6 * SS, 6 * SS, 22 * SS, 22 * SS], radius=3 * SS, fill=ink)
    reg("ic_stop", fin(cv, s), 0)
    # новый (+)
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.rounded_rectangle([12 * SS, 5 * SS, 16 * SS, 23 * SS], radius=1.5 * SS, fill=ink)
    d.rounded_rectangle([5 * SS, 12 * SS, 23 * SS, 16 * SS], radius=1.5 * SS, fill=ink)
    reg("ic_new", fin(cv, s), 0)
    # правка (карандаш)
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.polygon([(6 * SS, 22 * SS), (7 * SS, 16 * SS), (19 * SS, 4 * SS), (24 * SS, 9 * SS), (12 * SS, 21 * SS)], fill=ink)
    d.polygon([(6 * SS, 22 * SS), (7 * SS, 17 * SS), (11 * SS, 21 * SS)], fill=(235, 227, 205, 255))
    reg("ic_edit", fin(cv, s), 0)
    # повтор (стрелка по кругу)
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.arc([5 * SS, 5 * SS, 23 * SS, 23 * SS], start=40, end=330, fill=ink, width=3 * SS)
    d.polygon([(21 * SS, 3 * SS), (25 * SS, 13 * SS), (14 * SS, 11 * SS)], fill=ink)
    reg("ic_repeat", fin(cv, s), 0)
    # слоги (столбики)
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    for i, hh in enumerate((9, 18, 12, 20, 7)):
        x = (3 + i * 5) * SS
        d.rounded_rectangle([x, (14 - hh / 2) * SS, x + 3 * SS, (14 + hh / 2) * SS], radius=1.4 * SS, fill=ink)
    reg("ic_syl", fin(cv, s), 0)
    # назад (стрелка влево)
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.polygon([(4 * SS, 14 * SS), (14 * SS, 5 * SS), (14 * SS, 10 * SS), (24 * SS, 10 * SS), (24 * SS, 18 * SS), (14 * SS, 18 * SS), (14 * SS, 23 * SS)], fill=ink)
    reg("ic_back", fin(cv, s), 0)
    # к началу (|<<)
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.rounded_rectangle([4 * SS, 5 * SS, 7 * SS, 23 * SS], radius=1 * SS, fill=ink)
    d.polygon([(23 * SS, 5 * SS), (23 * SS, 23 * SS), (9 * SS, 14 * SS)], fill=ink)
    reg("ic_rew", fin(cv, s), 0)
    # динамик
    cv, s = icon_canvas(28)
    d = ImageDraw.Draw(cv)
    d.polygon([(3 * SS, 10 * SS), (9 * SS, 10 * SS), (16 * SS, 4 * SS), (16 * SS, 24 * SS), (9 * SS, 18 * SS), (3 * SS, 18 * SS)], fill=ink)
    d.arc([12 * SS, 8 * SS, 22 * SS, 20 * SS], start=-55, end=55, fill=ink, width=2 * SS)
    d.arc([14 * SS, 4 * SS, 26 * SS, 24 * SS], start=-55, end=55, fill=ink, width=2 * SS)
    reg("ic_vol", fin(cv, s), 0)


# ---------- фон ----------
def body(mode):
    rng = np.random.default_rng(7)
    base = vgrad(W, H, (240, 233, 212), (226, 217, 192))
    cv = base.convert("RGBA")
    nz = rng.normal(0, 2.2, (H, W, 1))
    arr = np.asarray(cv).astype(np.float32)
    arr[..., :3] += nz
    cv = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), "RGBA")
    d = ImageDraw.Draw(cv)
    d.rectangle([0, 0, W - 1, H - 1], outline=EDGE_D + (255,), width=2)
    d.rectangle([2, 2, W - 3, H - 3], outline=(255, 252, 240, 255), width=1)
    for (x, y) in ((11, 11), (W - 12, 11), (11, H - 12), (W - 12, H - 12)):
        screw(cv, x, y, 5)

    f_tag = font(FB, 11)
    # логотип
    fl = font(FBO, 46)
    text(cv, (36, 11), "+", fl, RED + (255,))
    text(cv, (66, 11), "VITA", fl, RED + (255,), spacing=3)
    text(cv, (204, 17), "TEXT TO SPEECH", f_tag, INK_L + (255,), spacing=1)
    text(cv, (204, 32), "FOR EXPERIMENTAL", f_tag, INK_L + (255,), spacing=1)
    text(cv, (204, 47), "& ELECTRONIC MUSIC", f_tag, INK_L + (255,), spacing=1)
    d = ImageDraw.Draw(cv)
    d.line([(192, 16), (192, 56)], fill=EDGE + (255,), width=1)

    # ЖК-экран
    LX, LY, LW, LH = 16, 64, 580, 150
    bm = mask_rr(LW, LH, 16)
    shadow(cv, bm, LX, LY, dy=3, r=3, a=0.3)
    put(cv, vgrad(LW, LH, (96, 88, 74), (40, 34, 26)), bm, LX, LY)
    put(cv, vgrad(LW, LH, (74, 66, 54), (34, 28, 20)), mask_rr(LW, LH, 16, inset=2), LX, LY)
    put(cv, solid(LW, LH, (255, 255, 255)), ImageChops.subtract(mask_rr(LW, LH, 16, inset=2.5), shift(mask_rr(LW, LH, 16, inset=2.5), 0, 2)), LX, LY, 0.18)
    SX, SY, SW, SH = 26, 74, 560, 130
    sm = mask_rr(SW, SH, 9)
    put(cv, vgrad(SW, SH, LCD_T, LCD_B), sm, SX, SY)
    # сетка пикселей и блик
    px = Image.new("RGBA", (SW, SH), (0, 0, 0, 0))
    pd = ImageDraw.Draw(px)
    for yy in range(0, SH, 3):
        pd.line([(0, yy), (SW, yy)], fill=(120, 80, 0, 22))
    big = Image.new("L", cv.size, 0)
    pm = Image.new("L", (SW, SH), 0)
    pm.paste(px.split()[3], (0, 0))
    big.paste(ImageChops.multiply(pm, sm), (SX, SY))
    put(cv, solid(*cv.size, (110, 70, 0)), big, 0, 0, 1.0)
    gl = Image.new("L", (SW, SH), 0)
    gd = ImageDraw.Draw(gl)
    gd.polygon([(0, 0), (SW * 0.55, 0), (SW * 0.25, SH), (0, SH)], fill=40)
    big = Image.new("L", cv.size, 0)
    big.paste(ImageChops.multiply(blur(gl, 6), sm), (SX, SY))
    put(cv, solid(*cv.size, (255, 255, 235)), big, 0, 0, 1.0)
    inner_shadow(cv, sm, SX, SY, dy=3, r=4, a=0.55, col=(70, 40, 0))
    f_lcd = font(FM, 12)
    if mode == "main":
        text(cv, (SX + 12, SY + 7), "INPUT TEXT", f_lcd, LCD_INK + (200,), spacing=1)
    else:
        text(cv, (SX + 12, SY + 7), "SYLLABLE MODE", f_lcd, LCD_INK + (200,), spacing=1)
    d = ImageDraw.Draw(cv)
    d.line([(SX + 10, SY + 24), (SX + SW - 10, SY + 24)], fill=LCD_INK + (60,), width=1)

    # панель ползунков/чипов
    PX, PY, PW, PH = 16, 276, 580, 132
    sunk(cv, PX, PY, PW, PH, 12, CREAM_D, (222, 212, 186))
    f_lab = font(FB, 13)
    rows = [(318, "SPEED"), (366, "PITCH")] if mode == "main" else [(304, "BPM"), (342, "PITCH"), (380, "SENS")]
    for cy, lb in rows:
        text(cv, (30, cy), lb, f_lab, INK + (255,), anchor="lm", spacing=1)
        groove(cv, 108, cy, 190)
    ttl = "eSpeak STYLE" if mode == "main" else "TIMING"
    text(cv, (380, 281), ttl, font(FB, 11), INK_L + (255,), spacing=1)

    # нижняя правая панель (громкость, диагностика)
    sunk(cv, 358, 418, 238, 110, 12, CREAM_D, (222, 212, 186))
    text(cv, (370, 446), "VOL", f_lab, INK + (255,), anchor="lm", spacing=1)
    groove(cv, 410, 446, 128)
    d = ImageDraw.Draw(cv)
    d.line([(370, 466), (584, 466)], fill=EDGE + (150,), width=1)

    # правая колонка: голоса
    sunk(cv, 612, 64, 332, 248, 14, CREAM_D, (222, 212, 186))
    text(cv, (626, 70), "VOICE", font(FB, 13), INK + (255,), spacing=2)
    text(cv, (938 - text_w("L / R", font(FB, 11), 1), 72), "L / R", font(FB, 11), INK_L + (255,), spacing=1)
    for i in range(7):
        y = 94 + i * 30
        sunk(cv, 624, y, 308, 28, 7, (244, 238, 222), (232, 224, 202), border=False, a=0.18)
    # история
    sunk(cv, 612, 322, 332, 200, 14, CREAM_D, (222, 212, 186))
    text(cv, (626, 328), "HISTORY", font(FB, 13), INK + (255,), spacing=2)
    text(cv, (938 - text_w("LAST 5", font(FB, 11), 1), 330), "LAST 5", font(FB, 11), INK_L + (255,), spacing=1)
    for i in range(5):
        sunk(cv, 624, 350 + i * 32, 308, 30, 8, (226, 216, 190), (216, 206, 180), border=False, a=0.3)
    return cv


def groove(cv, x, cy, w):
    h = 10
    sunk(cv, x - 4, cy - h // 2, w + 8, h, 5, (176, 164, 138), (206, 196, 170), border=True, a=0.6)


# ---------- атлас ----------
def pack(items, width=1024):
    x = y = rowh = 0
    pos = {}
    for name, (img, pad) in items:
        w, h = img.size
        if x + w > width:
            x, y, rowh = 0, y + rowh + 1, 0
        pos[name] = (x, y, w, h, pad)
        x += w + 1
        rowh = max(rowh, h)
    atlas = Image.new("RGBA", (width, y + rowh), (0, 0, 0, 0))
    for name, (img, pad) in items:
        atlas.alpha_composite(img, pos[name][:2])
    return atlas, pos


def main():
    make_buttons(); make_chips(); make_cards(); make_row_sel(); make_thumb(); make_leds(); make_icons()
    os.makedirs(os.path.join(ROOT, "assets"), exist_ok=True)
    os.makedirs(os.path.join(ROOT, "design", "parts"), exist_ok=True)
    items = sorted(SPR.items(), key=lambda kv: (-kv[1][0].size[1], kv[0]))
    atlas, pos = pack(items)
    atlas.save(os.path.join(ROOT, "assets", "atlas.png"), optimize=True)
    for name, (img, pad) in SPR.items():
        img.save(os.path.join(ROOT, "design", "parts", name + ".png"))
    for mode in ("main", "syl"):
        b = body(mode).convert("RGB")
        b.save(os.path.join(ROOT, "assets", f"body_{mode}.png"), optimize=True)
        b.save(os.path.join(ROOT, "design", f"body_{mode}.png"))
    with open(os.path.join(ROOT, "ui_atlas.h"), "w") as f:
        f.write("/* СОЗДАНО tools/gen_assets.py — не править руками. x, y, w, h в атласе и отступ (тень) pad */\n#pragma once\n")
        f.write("typedef struct { short x, y, w, h, pad; } SprRect;\nenum {\n")
        names = sorted(pos)
        for n in names:
            f.write(f"    S_{n.upper()},\n")
        f.write("    S_COUNT\n};\nstatic const SprRect SPR_TAB[S_COUNT] = {\n")
        for n in names:
            x, y, w, h, p = pos[n]
            f.write(f"    {{{x}, {y}, {w}, {h}, {p}}}, /* {n} */\n")
        f.write("};\n")
    print("atlas", atlas.size, len(SPR), "sprites")


if __name__ == "__main__":
    main()
