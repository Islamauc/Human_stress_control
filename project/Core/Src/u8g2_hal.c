#include "main.h"
#include "u8g2.h"
#include <string.h>

extern I2C_HandleTypeDef hi2c3;


#define OLED_I2C_ADDR   0x78    


uint8_t u8x8_byte_stm32_hw_i2c(u8x8_t *u8x8, uint8_t msg,
                                 uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[256];
    static uint8_t buf_idx = 0;

    switch (msg)
    {
        case U8X8_MSG_BYTE_INIT:
            /* Nothing to do — I2C already initialised in main() */
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;

        case U8X8_MSG_BYTE_SEND:
            /* Guard against buffer overflow */
            if ((uint16_t)buf_idx + arg_int <= sizeof(buffer)) {
                memcpy(&buffer[buf_idx], arg_ptr, arg_int);
                buf_idx += arg_int;
            }
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            if (HAL_I2C_Master_Transmit(&hi2c3, OLED_I2C_ADDR,
                                         buffer, buf_idx, 100) != HAL_OK) {
                return 0;   /* signal error to u8g2 */
            }
            buf_idx = 0;
            break;

        default:
            break;
    }
    return 1;
}

uint8_t u8x8_gpio_delay_stm32(u8x8_t *u8x8, uint8_t msg,
                                uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            break;

        case U8X8_MSG_DELAY_MILLI:
            HAL_Delay(arg_int);
            break;

        case U8X8_MSG_DELAY_10MICRO:
            for (uint16_t n = 0; n < 800; n++) { __NOP(); }
            break;

        case U8X8_MSG_DELAY_100NANO:
            for (uint8_t n = 0; n < 8; n++) { __NOP(); }
            break;

        default:
            break;
    }
    return 1;
}