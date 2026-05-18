#include "hrv.h"
#include <stdint.h>
#include <stdlib.h>

static uint32_t isqrt(uint32_t n)
{
    if (n == 0) return 0;
    uint32_t x = n, y = 1;
    while (x > y) { x = (x + y) / 2; y = n / x; }
    return x;
}

static uint8_t ibi_plausible(HRV_t *h, uint32_t new_ibi)
{
    if (h->buffer_idx < 3) return 1;
    uint8_t  count = (h->buffer_idx < 30) ? (uint8_t)h->buffer_idx : 30;
    uint8_t  start = (h->buffer_idx < 30) ? 0 : (h->buffer_idx % 30);
    uint32_t sum   = 0;
    for (uint8_t i = 0; i < count; i++)
        sum += h->ibi_buffer[(start + i) % 30];
    uint32_t mean = sum / count;
    uint32_t lo = (mean * 75)  / 100;
    uint32_t hi = (mean * 125) / 100;
    return (new_ibi >= lo && new_ibi <= hi) ? 1 : 0;
}

void HRV_OnBeat(HRV_t *h, uint32_t ts)
{
    if (h->last_beat_ts != 0)
    {
			uint32_t ibi_ticks  = ts - h->last_beat_ts;
			if (ibi_ticks < 1000) { h->last_beat_ts = ts; return; }
			uint32_t ibi_ms_val = ibi_ticks / 1000;

        if (ibi_ms_val >= 400 && ibi_ms_val <= 2000)
        {
            if (ibi_plausible(h, ibi_ms_val))
            {
                h->ibi_ms = ibi_ms_val;
                h->ibi_buffer[h->buffer_idx % 30] = ibi_ms_val;
                h->buffer_idx++;

                if (h->buffer_idx >= 2)
                    h->valid = true;
            }
        }
    }
    h->last_beat_ts = ts;
}

void HRV_Compute(HRV_t *h)
{
    if (!h->valid) return;

    uint8_t count = (h->buffer_idx < 30) ? (uint8_t)h->buffer_idx : 30;


    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++)
        sum += h->ibi_buffer[i];
    uint32_t mean = sum / count;
		if (mean > 0)
				h->hr_bpm = (uint16_t)(60000UL / mean);
    uint32_t var_sum = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        int32_t diff = (int32_t)h->ibi_buffer[i] - (int32_t)mean;
        var_sum += (uint32_t)(diff * diff);
    }
    h->sdnn_ms = isqrt(var_sum / count);

    uint32_t sq_sum = 0;
    uint8_t  pairs  = 0;
    uint8_t  start  = (h->buffer_idx < 30) ? 0 : (h->buffer_idx % 30);

    for (uint8_t i = 0; i < count - 1; i++)
    {
        uint8_t idx_a = (start + i)     % 30;
        uint8_t idx_b = (start + i + 1) % 30;
        int32_t d     = (int32_t)h->ibi_buffer[idx_b]
                      - (int32_t)h->ibi_buffer[idx_a];
        sq_sum += (uint32_t)(d * d);
        pairs++;
    }
    h->rmssd_ms = (pairs > 0) ? isqrt(sq_sum / pairs) : 0;


    if (h->rmssd_ms >= 80)
        h->stress_index = 0;
    else if (h->rmssd_ms <= 10)
        h->stress_index = 100;
    else
        h->stress_index = (uint8_t)(100U - ((h->rmssd_ms - 10U) * 100U / 70U));
}

int32_t process_ppg_signal(int32_t x)
{
    static int32_t xprev  = 0, yprev = 0;
    static uint8_t seeded = 0;
    if (!seeded) { xprev = x; seeded = 1; }

    int32_t dc_removed = x - xprev + (yprev * 995) / 1000;
    xprev = x;
    yprev = dc_removed;

    static int32_t ma_buf[5] = {0};
    static uint8_t mi = 0;
    ma_buf[mi % 5] = dc_removed;
    mi++;

    return (ma_buf[0] + ma_buf[1] + ma_buf[2] + ma_buf[3] + ma_buf[4]) / 5;
}