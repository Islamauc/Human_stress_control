#ifndef HRV_H
#define HRV_H

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    /* Basic Vitals */
    uint8_t  hr_bpm;          // Heart rate in beats per minute
    uint8_t  stress_index;    // Stress index 0 (relaxed) – 100 (stressed)

    /* HRV Metrics */
    uint32_t rmssd_ms;        // Root Mean Square of Successive Differences (ms)
    uint32_t sdnn_ms;         // Standard Deviation of NN intervals (ms)

    /* Internal State */
    uint32_t ibi_ms;          // Latest Inter-Beat Interval (ms)
    uint32_t last_beat_ts;    // TIM2 timestamp of the previous beat
    bool     valid;           // true once = 2 valid beats have been recorded

    /* Ring Buffer — stores last 30 IBIs (was uint16_t, now uint32_t) */
    uint32_t ibi_buffer[30];
    uint8_t  buffer_idx;      // Wraps freely; use (buffer_idx % 30) to index
} HRV_t;

/**
 * @brief Record a new heartbeat.
 *        Call from EXTI8 ISR: HRV_OnBeat(&hrv, __HAL_TIM_GET_COUNTER(&htim2));
 */
void HRV_OnBeat(HRV_t *h, uint32_t ts);

/**
 * @brief Compute HR, RMSSD, SDNN and Stress Index from buffered IBIs.
 *        Call periodically in main loop (e.g. every 5 s) or after 30 beats.
 */
void HRV_Compute(HRV_t *h);

/**
 * @brief DC-removal + moving-average filter for raw PPG samples.
 */
int32_t process_ppg_signal(int32_t x);

#endif /* HRV_H */