#include "u8g2.h"
#include <stdint.h>
#include <stdio.h>
#include "display.h"

static int32_t clamp(int32_t val, int32_t min_val, int32_t max_val)
{
    if (val < min_val) return min_val;
    if (val > max_val) return max_val;
    return val;
}

static uint8_t get_wave_range(int32_t *wave, uint8_t len,
                               int32_t *out_min, int32_t *out_max)
{
    int32_t wmin = wave[0], wmax = wave[0];
    for (uint8_t i = 1; i < len; i++) {
        if (wave[i] < wmin) wmin = wave[i];
        if (wave[i] > wmax) wmax = wave[i];
    }
    *out_min = wmin;
    *out_max = wmax;
    return (wmax - wmin) > 0 ? 1 : 0;
}

void display_update(u8g2_t *g, uint8_t hr, uint8_t spo2,
                    uint8_t si, int32_t *wave)
{
    char str[24];
    (void)spo2;   /* not displayed — hardcoded value is not meaningful */

    u8g2_ClearBuffer(g);

    /* ---- Line 1: HR + STATUS (matches Tera Term STATE label) ---- */
    u8g2_SetFont(g, u8g2_font_ncenB08_tr);
    snprintf(str, sizeof(str), "HR: %d BPM", hr);
    u8g2_DrawStr(g, 0, 10, str);

    /* ---- Line 2: STRESS value + STATE label ---- */
    u8g2_SetFont(g, u8g2_font_ncenB08_tr);
    const char *state;
    if      (si > 70) state = "HIGH STRESS";
    else if (si > 40) state = "MEDIUM";
    else if (si > 20) state = "RELAXED";
    else              state = "VERY RELAXED";
    snprintf(str, sizeof(str), "STR:%d %s", si, state);
    u8g2_DrawStr(g, 0, 22, str);

    /* ---- Separator ---- */
    u8g2_DrawHLine(g, 0, 25, 128);

    /* ---- Waveform zone: y=27..63 ---- */
#define WAVE_TOP  27
#define WAVE_BOT  63
#define WAVE_H    (WAVE_BOT - WAVE_TOP)
#define WAVE_LEN  128

    int32_t wmin, wmax;
    if (get_wave_range(wave, WAVE_LEN, &wmin, &wmax))
    {
        int32_t range = wmax - wmin;
        for (int i = 0; i < WAVE_LEN - 1; i++)
        {
            int32_t y0 = WAVE_BOT - ((wave[i]     - wmin) * WAVE_H / range);
            int32_t y1 = WAVE_BOT - ((wave[i + 1] - wmin) * WAVE_H / range);
            y0 = clamp(y0, WAVE_TOP, WAVE_BOT);
            y1 = clamp(y1, WAVE_TOP, WAVE_BOT);
            u8g2_DrawLine(g, i, (int8_t)y0, i + 1, (int8_t)y1);
        }
    }
    else
    {
        int32_t mid = (WAVE_TOP + WAVE_BOT) / 2;
        u8g2_DrawHLine(g, 0, (uint8_t)mid, 128);
        u8g2_SetFont(g, u8g2_font_ncenB08_tr);
        u8g2_DrawStr(g, 10, (uint8_t)(mid + 8), "CALCULATING...");
    }

    u8g2_SendBuffer(g);
}