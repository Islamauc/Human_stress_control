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

    /* DBG-1: show every plausibility decision */
    uint8_t ok = (new_ibi >= lo && new_ibi <= hi) ? 1 : 0;
    DBG("[PLAUS] new=%lu mean=%lu lo=%lu hi=%lu -> %s\r\n",
        (unsigned long)new_ibi, (unsigned long)mean,
        (unsigned long)lo, (unsigned long)hi,
        ok ? "OK" : "REJECTED");
    return ok;
}

void HRV_OnBeat(HRV_t *h, uint32_t ts)
{
    if (h->last_beat_ts != 0)
    {
        uint32_t ibi_ticks  = ts - h->last_beat_ts;

        /* DBG-2: show raw ticks so we can verify TIM2 is 1MHz */
        DBG("[BEAT] ticks=%lu ibi_ms=%lu buf=%u\r\n",
            (unsigned long)ibi_ticks,
            (unsigned long)(ibi_ticks / 1000),
            (unsigned int)h->buffer_idx);

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

                /* DBG-3: show buffer contents after every store */
                DBG("[BUF] stored %lu ms at slot %u  valid=%u\r\n",
                    (unsigned long)ibi_ms_val,
                    (unsigned int)((h->buffer_idx - 1) % 30),
                    (unsigned int)h->valid);
            }
        }
        else
        {
            /* DBG-4: show why ibi was range-rejected */
            DBG("[BEAT] range FAIL ibi_ms=%lu (need 400-2000)\r\n",
                (unsigned long)ibi_ms_val);
        }
    }
    h->last_beat_ts = ts;
}

void HRV_Compute(HRV_t *h)
{
    if (!h->valid) return;

    uint8_t count = (h->buffer_idx < 30) ? (uint8_t)h->buffer_idx : 30;
    uint8_t start = (h->buffer_idx < 30) ? 0 : (h->buffer_idx % 30);

    /* DBG-5: show exactly which slots and values Compute is reading */
    DBG("[COMPUTE] buf_idx=%u count=%u start=%u\r\n",
        (unsigned int)h->buffer_idx,
        (unsigned int)count,
        (unsigned int)start);
    DBG("[COMPUTE] IBI values: ");
    for (uint8_t i = 0; i < count; i++)
        DBG("%lu ", (unsigned long)h->ibi_buffer[i]);
    DBG("\r\n");

    /* --- original HR/SDNN code uses linear read ibi_buffer[i] --- */
    uint32_t sum = 0;
    for (uint8_t i = 0; i < count; i++)
        sum += h->ibi_buffer[i];
    uint32_t mean = sum / count;
    if (mean > 0)
        h->hr_bpm = (uint16_t)(60000UL / mean);

    DBG("[COMPUTE] mean=%lu hr=%u bpm\r\n",
        (unsigned long)mean, (unsigned int)h->hr_bpm);

    uint32_t var_sum = 0;
    for (uint8_t i = 0; i < count; i++)
    {
        int32_t diff = (int32_t)h->ibi_buffer[i] - (int32_t)mean;
        var_sum += (uint32_t)(diff * diff);
    }
    h->sdnn_ms = isqrt(var_sum / count);

    DBG("[COMPUTE] sdnn=%u ms\r\n", (unsigned int)h->sdnn_ms);

    /* --- original RMSSD uses circular read --- */
    uint32_t sq_sum = 0;
    uint8_t  pairs  = 0;
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

    DBG("[COMPUTE] rmssd=%u ms (pairs=%u sq_sum=%lu)\r\n",
        (unsigned int)h->rmssd_ms,
        (unsigned int)pairs,
        (unsigned long)sq_sum);

    /* --- original stress formula --- */
    if (h->rmssd_ms >= 100)
        h->stress_index = 0;
    else if (h->rmssd_ms <= 15)
        h->stress_index = 100;
    else
        h->stress_index = (uint8_t)(100U - ((h->rmssd_ms - 15U) * 100U / 85U));

    DBG("[COMPUTE] stress=%u/100\r\n", (unsigned int)h->stress_index);
}

int32_t process_ppg_signal(int32_t x)
{
    static int32_t xprev  = 0, yprev = 0;
    static uint8_t seeded = 0;
    static uint32_t call_n = 0;
    call_n++;

    if (!seeded) { xprev = x; seeded = 1; }

    int32_t dc_removed = x - xprev + (yprev * 995) / 1000;
    xprev = x;
    yprev = dc_removed;

    static int32_t ma_buf[5] = {0};
    static uint8_t mi = 0;
    ma_buf[mi % 5] = dc_removed;
    mi++;
    int32_t out = (ma_buf[0] + ma_buf[1] + ma_buf[2] + ma_buf[3] + ma_buf[4]) / 5;

    /* DBG-6: every 50 samples — watch 'hp' decay toward zero after finger attach.
     * If hp is still in thousands after sample 500, alpha=0.995 is too slow. */
    if (call_n % 50 == 1)
        DBG("[FILTER] n=%lu x=%ld hp=%ld out=%ld\r\n",
            (unsigned long)call_n, (long)x, (long)dc_removed, (long)out);

    return out;
}