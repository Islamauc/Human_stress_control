#ifndef __MAX30102_H
#define __MAX30102_H

#include "main.h"

#define MAX30102_ADDR   0xAE

HAL_StatusTypeDef MAX30102_Init(I2C_HandleTypeDef *hi2c);

HAL_StatusTypeDef MAX30102_ReadRaw(I2C_HandleTypeDef *hi2c,
                                    uint32_t *red, uint32_t *ir);

uint8_t MAX30102_SamplesAvailable(I2C_HandleTypeDef *hi2c);

#endif /* __MAX30102_H */