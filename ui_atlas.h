/* СОЗДАНО tools/gen_assets.py — не править руками. x, y, w, h в атласе и отступ (тень) pad */
#pragma once
typedef struct { short x, y, w, h, pad; } SprRect;
enum {
    S_BIG_ORANGE,
    S_BIG_ORANGE_P,
    S_BIG_YELLOW,
    S_BIG_YELLOW_P,
    S_BTN_CREAM,
    S_BTN_CREAM_P,
    S_BTN_ORANGE,
    S_BTN_ORANGE_P,
    S_BTN_YELLOW,
    S_BTN_YELLOW_P,
    S_CHIP0_OFF,
    S_CHIP0_ON,
    S_CHIP1_OFF,
    S_CHIP1_ON,
    S_CHIP2_OFF,
    S_CHIP2_ON,
    S_CHIP3_OFF,
    S_CHIP3_ON,
    S_CHIP4_OFF,
    S_CHIP4_ON,
    S_CHIP5_OFF,
    S_CHIP5_ON,
    S_DOT0,
    S_DOT1,
    S_DOT2,
    S_DOT3,
    S_HC0,
    S_HC0_SEL,
    S_HC1,
    S_HC1_SEL,
    S_HC2,
    S_HC2_SEL,
    S_HC3,
    S_HC3_SEL,
    S_HC4,
    S_HC4_SEL,
    S_IC_BACK,
    S_IC_EDIT,
    S_IC_NEW,
    S_IC_PLAY,
    S_IC_PLAY_BIG,
    S_IC_REPEAT,
    S_IC_REW,
    S_IC_STOP,
    S_IC_SYL,
    S_IC_VOL,
    S_LED_GREEN,
    S_LED_OFF,
    S_LED_ORANGE,
    S_LED_RED,
    S_PILL_CREAM,
    S_PILL_CREAM_P,
    S_PILL_ORANGE,
    S_PILL_ORANGE_P,
    S_PILL_YELLOW,
    S_PILL_YELLOW_P,
    S_ROWSEL,
    S_THUMB,
    S_THUMB_P,
    S_COUNT
};
static const SprRect SPR_TAB[S_COUNT] = {
    {0, 0, 344, 114, 7}, /* big_orange */
    {345, 0, 344, 114, 7}, /* big_orange_p */
    {0, 115, 344, 114, 7}, /* big_yellow */
    {345, 115, 344, 114, 7}, /* big_yellow_p */
    {690, 115, 202, 58, 7}, /* btn_cream */
    {0, 230, 202, 58, 7}, /* btn_cream_p */
    {203, 230, 202, 58, 7}, /* btn_orange */
    {406, 230, 202, 58, 7}, /* btn_orange_p */
    {609, 230, 202, 58, 7}, /* btn_yellow */
    {812, 230, 202, 58, 7}, /* btn_yellow_p */
    {86, 346, 112, 40, 5}, /* chip0_off */
    {199, 346, 112, 40, 5}, /* chip0_on */
    {312, 346, 112, 40, 5}, /* chip1_off */
    {425, 346, 112, 40, 5}, /* chip1_on */
    {538, 346, 112, 40, 5}, /* chip2_off */
    {651, 346, 112, 40, 5}, /* chip2_on */
    {764, 346, 112, 40, 5}, /* chip3_off */
    {877, 346, 112, 40, 5}, /* chip3_on */
    {0, 389, 112, 40, 5}, /* chip4_off */
    {113, 389, 112, 40, 5}, /* chip4_on */
    {226, 389, 112, 40, 5}, /* chip5_off */
    {339, 389, 112, 40, 5}, /* chip5_on */
    {694, 547, 16, 16, 2}, /* dot0 */
    {711, 547, 16, 16, 2}, /* dot1 */
    {728, 547, 16, 16, 2}, /* dot2 */
    {745, 547, 16, 16, 2}, /* dot3 */
    {452, 389, 316, 38, 4}, /* hc0 */
    {0, 430, 316, 38, 4}, /* hc0_sel */
    {317, 430, 316, 38, 4}, /* hc1 */
    {634, 430, 316, 38, 4}, /* hc1_sel */
    {0, 469, 316, 38, 4}, /* hc2 */
    {317, 469, 316, 38, 4}, /* hc2_sel */
    {634, 469, 316, 38, 4}, /* hc3 */
    {0, 508, 316, 38, 4}, /* hc3_sel */
    {317, 508, 316, 38, 4}, /* hc4 */
    {634, 508, 316, 38, 4}, /* hc4_sel */
    {317, 547, 28, 28, 0}, /* ic_back */
    {346, 547, 28, 28, 0}, /* ic_edit */
    {375, 547, 28, 28, 0}, /* ic_new */
    {404, 547, 28, 28, 0}, /* ic_play */
    {0, 289, 56, 56, 0}, /* ic_play_big */
    {433, 547, 28, 28, 0}, /* ic_repeat */
    {462, 547, 28, 28, 0}, /* ic_rew */
    {491, 547, 28, 28, 0}, /* ic_stop */
    {520, 547, 28, 28, 0}, /* ic_syl */
    {549, 547, 28, 28, 0}, /* ic_vol */
    {578, 547, 28, 28, 6}, /* led_green */
    {607, 547, 28, 28, 6}, /* led_off */
    {636, 547, 28, 28, 6}, /* led_orange */
    {665, 547, 28, 28, 6}, /* led_red */
    {57, 289, 154, 50, 7}, /* pill_cream */
    {212, 289, 154, 50, 7}, /* pill_cream_p */
    {367, 289, 154, 50, 7}, /* pill_orange */
    {522, 289, 154, 50, 7}, /* pill_orange_p */
    {677, 289, 154, 50, 7}, /* pill_yellow */
    {832, 289, 154, 50, 7}, /* pill_yellow_p */
    {0, 547, 316, 36, 4}, /* rowsel */
    {0, 346, 42, 42, 6}, /* thumb */
    {43, 346, 42, 42, 6}, /* thumb_p */
};
