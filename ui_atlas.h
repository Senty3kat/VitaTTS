/* СОЗДАНО tools/gen_assets.py — не править руками. x, y, w, h в атласе и отступ (тень) pad */
#pragma once
typedef struct { short x, y, w, h, pad; } SprRect;
enum {
    S_BIG_ORANGE,
    S_BIG_ORANGE_P,
    S_BIG_YELLOW,
    S_BIG_YELLOW_P,
    S_BTNW_CREAM,
    S_BTNW_CREAM_P,
    S_BTNW_ORANGE,
    S_BTNW_ORANGE_P,
    S_BTNW_YELLOW,
    S_BTNW_YELLOW_P,
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
    S_IC_STOP_BIG,
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
    {690, 115, 298, 58, 7}, /* btnw_cream */
    {0, 230, 298, 58, 7}, /* btnw_cream_p */
    {299, 230, 298, 58, 7}, /* btnw_orange */
    {598, 230, 298, 58, 7}, /* btnw_orange_p */
    {0, 289, 298, 58, 7}, /* btnw_yellow */
    {299, 289, 298, 58, 7}, /* btnw_yellow_p */
    {706, 348, 112, 40, 5}, /* chip0_off */
    {819, 348, 112, 40, 5}, /* chip0_on */
    {0, 399, 112, 40, 5}, /* chip1_off */
    {113, 399, 112, 40, 5}, /* chip1_on */
    {226, 399, 112, 40, 5}, /* chip2_off */
    {339, 399, 112, 40, 5}, /* chip2_on */
    {452, 399, 112, 40, 5}, /* chip3_off */
    {565, 399, 112, 40, 5}, /* chip3_on */
    {678, 399, 112, 40, 5}, /* chip4_off */
    {791, 399, 112, 40, 5}, /* chip4_on */
    {904, 399, 112, 40, 5}, /* chip5_off */
    {0, 440, 112, 40, 5}, /* chip5_on */
    {0, 598, 16, 16, 2}, /* dot0 */
    {17, 598, 16, 16, 2}, /* dot1 */
    {34, 598, 16, 16, 2}, /* dot2 */
    {51, 598, 16, 16, 2}, /* dot3 */
    {270, 440, 316, 38, 4}, /* hc0 */
    {587, 440, 316, 38, 4}, /* hc0_sel */
    {0, 481, 316, 38, 4}, /* hc1 */
    {317, 481, 316, 38, 4}, /* hc1_sel */
    {634, 481, 316, 38, 4}, /* hc2 */
    {0, 520, 316, 38, 4}, /* hc2_sel */
    {317, 520, 316, 38, 4}, /* hc3 */
    {634, 520, 316, 38, 4}, /* hc3_sel */
    {0, 559, 316, 38, 4}, /* hc4 */
    {317, 559, 316, 38, 4}, /* hc4_sel */
    {634, 559, 28, 28, 0}, /* ic_back */
    {663, 559, 28, 28, 0}, /* ic_edit */
    {692, 559, 28, 28, 0}, /* ic_new */
    {721, 559, 28, 28, 0}, /* ic_play */
    {598, 289, 56, 56, 0}, /* ic_play_big */
    {750, 559, 28, 28, 0}, /* ic_repeat */
    {779, 559, 28, 28, 0}, /* ic_rew */
    {808, 559, 28, 28, 0}, /* ic_stop */
    {655, 289, 56, 56, 0}, /* ic_stop_big */
    {837, 559, 28, 28, 0}, /* ic_syl */
    {866, 559, 28, 28, 0}, /* ic_vol */
    {895, 559, 28, 28, 6}, /* led_green */
    {924, 559, 28, 28, 6}, /* led_off */
    {953, 559, 28, 28, 6}, /* led_orange */
    {982, 559, 28, 28, 6}, /* led_red */
    {712, 289, 154, 50, 7}, /* pill_cream */
    {867, 289, 154, 50, 7}, /* pill_cream_p */
    {0, 348, 154, 50, 7}, /* pill_orange */
    {155, 348, 154, 50, 7}, /* pill_orange_p */
    {310, 348, 154, 50, 7}, /* pill_yellow */
    {465, 348, 154, 50, 7}, /* pill_yellow_p */
    {113, 440, 156, 40, 4}, /* rowsel */
    {620, 348, 42, 42, 6}, /* thumb */
    {663, 348, 42, 42, 6}, /* thumb_p */
};
