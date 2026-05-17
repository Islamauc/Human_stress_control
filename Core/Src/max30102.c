#include "max30102.h"

#define MAX_ADDR            0xAE
#define REG_INT_STATUS1     0x00
#define REG_INT_ENABLE1     0x02
#define REG_FIFO_WR_PTR     0x04
#define REG_FIFO_OVF        0x05
#define REG_FIFO_RD_PTR     0x06
#define REG_FIFO_DATA       0x07
#define REG_FIFO_CONFIG     0x08
#define REG_MODE_CONFIG     0x09
#define REG_SPO2_CONFIG     0x0A
#define REG_LED1_PA         0x0C
#define REG_LED2_PA         0x0D

static HAL_StatusTypeDef reg_read(I2C_HandleTypeDef *hi2c,
                                   uint8_t reg, uint8_t *val)
{
    return HAL_I2C_Mem_Read(hi2c, MAX_ADDR, reg, 1, val, 1, 50);
}

static HAL_StatusTypeDef reg_write(I2C_HandleTypeDef *hi2c,
                                    uint8_t reg, uint8_t val)
{
    return HAL_I2C_Mem_Write(hi2c, MAX_ADDR, reg, 1, &val, 1, 50);
}

HAL_StatusTypeDef MAX30102_ReadRaw(I2C_HandleTypeDef *hi2c,
                                    uint32_t *red, uint32_t *ir)
{
    uint8_t wr_ptr, rd_ptr;
    if (reg_read(hi2c, REG_FIFO_WR_PTR, &wr_ptr) != HAL_OK) return HAL_ERROR;
    if (reg_read(hi2c, REG_FIFO_RD_PTR, &rd_ptr) != HAL_OK) return HAL_ERROR;
    if (wr_ptr == rd_ptr) return HAL_ERROR;

    uint8_t buf[6];
    if (HAL_I2C_Mem_Read(hi2c, MAX_ADDR, REG_FIFO_DATA,
                          1, buf, 6, 50) != HAL_OK) return HAL_ERROR;

    *red = ((uint32_t)(buf[0] & 0x03) << 16)
         | ((uint32_t) buf[1]         <<  8)
         |  (uint32_t) buf[2];
    *ir  = ((uint32_t)(buf[3] & 0x03) << 16)
         | ((uint32_t) buf[4]         <<  8)
         |  (uint32_t) buf[5];

    uint8_t dummy;
    reg_read(hi2c, REG_INT_STATUS1, &dummy);
    return HAL_OK;
}

/* Returns number of unread samples waiting in FIFO (0-31) */
uint8_t MAX30102_SamplesAvailable(I2C_HandleTypeDef *hi2c)
{
    uint8_t wr_ptr = 0, rd_ptr = 0;
    reg_read(hi2c, REG_FIFO_WR_PTR, &wr_ptr);
    reg_read(hi2c, REG_FIFO_RD_PTR, &rd_ptr);
    return (uint8_t)((wr_ptr - rd_ptr + 32) % 32);
}

HAL_StatusTypeDef MAX30102_Init(I2C_HandleTypeDef *hi2c)
{
    HAL_StatusTypeDef ret;

    ret = reg_write(hi2c, REG_MODE_CONFIG, 0x40);
    if (ret != HAL_OK) return ret;
    HAL_Delay(300);

    ret = reg_write(hi2c, REG_FIFO_CONFIG, 0x1F);
    if (ret != HAL_OK) return ret;

    ret = reg_write(hi2c, REG_MODE_CONFIG, 0x03);
    if (ret != HAL_OK) return ret;

    ret = reg_write(hi2c, REG_SPO2_CONFIG, 0x27);
    if (ret != HAL_OK) return ret;

    ret = reg_write(hi2c, REG_LED1_PA, 0x24);
    if (ret != HAL_OK) return ret;
    ret = reg_write(hi2c, REG_LED2_PA, 0x24);
    if (ret != HAL_OK) return ret;

    ret = reg_write(hi2c, REG_INT_ENABLE1, 0x80);
    if (ret != HAL_OK) return ret;

    ret = reg_write(hi2c, REG_FIFO_WR_PTR, 0x00);
    if (ret != HAL_OK) return ret;
    ret = reg_write(hi2c, REG_FIFO_RD_PTR, 0x00);
    if (ret != HAL_OK) return ret;
    ret = reg_write(hi2c, REG_FIFO_OVF,    0x00);
    if (ret != HAL_OK) return ret;

    uint8_t dummy;
    reg_read(hi2c, REG_INT_STATUS1, &dummy);
    return HAL_OK;
}