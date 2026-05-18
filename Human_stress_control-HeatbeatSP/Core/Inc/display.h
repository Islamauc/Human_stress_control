#ifndef DISPLAY_H
#define DISPLAY_H

#include "u8g2.h"
#include <stdint.h>

/**
 * @brief Render HR, SpO2, Stress Index and PPG waveform on the OLED.
 *
 * @param g     Pointer to the u8g2 handle (already initialised)
 * @param hr    Heart rate in bpm
 * @param spo2  Blood oxygen saturation in % (0-100)
 * @param si    Stress index (0 = relaxed, 100 = stressed)
 * @param wave  Pointer to 128-sample filtered PPG buffer (int32_t[128])
 */
void display_update(u8g2_t *g, uint8_t hr, uint8_t spo2,
                    uint8_t si, int32_t *wave);

#endif /* DISPLAY_H */