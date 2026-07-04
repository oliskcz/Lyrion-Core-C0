/**
 * @file  tmp102.c
 * @brief Driver for the TI TMP102AIDRLR temperature sensor (I2C).
 *
 * The TMP102 temperature register (0x00) is 16 bits:
 *   [15:4] = temperature (12-bit signed, 0.0625 °C / LSB)
 *   [3:0]  = reserved (0 in 12-bit mode)
 *
 * Conversion:  (raw >> 4) * 0.0625  °C
 *
 * The config register (0x01) is 16 bits.  Conversion rate CR1:CR0 lives in
 * byte 2 (LSB), bits 7:6:
 *   00 = 0.25 Hz, 01 = 1 Hz, 10 = 4 Hz (default), 11 = 8 Hz
 *
 * Note on STM32C0/G0 I2C HAL:
 *   Polling-mode I2C transactions (e.g. ssd1306's HAL_I2C_Mem_Write with
 *   HAL_MAX_DELAY) can leave hi2c1.State == HAL_I2C_STATE_BUSY, causing
 *   subsequent HAL_I2C_Mem_Read calls to immediately return HAL_BUSY.
 *   This driver recovers automatically with a single re-init retry.
 */

#include "tmp102.h"
#include "main.h"

/* 7-bit I2C address — shift left once for HAL's 8-bit format. */
#define TMP102_ADDR         (TMP102_ADDR_7BIT << 1)
#define TMP102_REG_TEMP     0x00U
#define TMP102_REG_CONFIG   0x01U

extern I2C_HandleTypeDef hi2c1;

/* ---------- Private helpers ------------------------------------------------ */

/*
 * Recover a stuck I2C peripheral by resetting it.
 * This costs ~2 I2C clock cycles and only fires on the rare HAL_BUSY path.
 */
static void tmp102_recover_i2c(I2C_HandleTypeDef *hi2c)
{
    hi2c->State = HAL_I2C_STATE_READY;
    HAL_I2C_DeInit(hi2c);
    HAL_I2C_Init(hi2c);
}

/* ---------- Public --------------------------------------------------------- */

uint16_t TMP102_ReadRaw(I2C_HandleTypeDef *hi2c)
{
    uint8_t rxbuf[2] = {0, 0};

    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(hi2c, TMP102_ADDR, TMP102_REG_TEMP,
                                                I2C_MEMADD_SIZE_8BIT, rxbuf, 2, 100);

    if (status == HAL_BUSY)
    {
        tmp102_recover_i2c(hi2c);
        status = HAL_I2C_Mem_Read(hi2c, TMP102_ADDR, TMP102_REG_TEMP,
                                  I2C_MEMADD_SIZE_8BIT, rxbuf, 2, 100);
    }

    if (status != HAL_OK)
        return 0xFFFFU;

    return (uint16_t)((rxbuf[0] << 8) | rxbuf[1]);
}

int16_t TMP102_ReadTemp(I2C_HandleTypeDef *hi2c)
{
    uint16_t raw = TMP102_ReadRaw(hi2c);

    if (raw == 0xFFFFU)
        return TMP102_ERROR;

    /*
     * The temperature register contains a signed value with LSB always
     * 0.0625 °C (1/16 °C), regardless of 12-bit or 13-bit extended mode.
     *
     * 12-bit (EM=0): value in bits [15:4]  → (int16_t)raw >> 4
     * 13-bit (EM=1): value in bits [15:3]  → (int16_t)(raw&0xFFF8) >> 3
     *
     * Both give the same numeric result in units of 1/16 °C.
     * The extra bit in 13-bit mode only extends the upper range to 150 °C.
     */
    int32_t raw_x16;

    if (raw & 0x0004)
    {
        /* Extended mode (13-bit) — value in bits [15:3]. */
        raw_x16 = (int16_t)(raw & 0xFFF8) >> 3;
    }
    else
    {
        /* Normal mode (12-bit) — value in bits [15:4]. */
        raw_x16 = (int16_t)raw >> 4;
    }

    /* raw_x16 is always in units of 1/16 °C, so divisor is always 16. */
#if   TMP102_DECIMAL_PLACES == 0
    return (int16_t)((raw_x16 + 8) / 16);
#elif TMP102_DECIMAL_PLACES == 1
    return (int16_t)((raw_x16 * 10 + 8) / 16);
#elif TMP102_DECIMAL_PLACES == 2
    return (int16_t)((raw_x16 * 100 + 8) / 16);
#else
#error "TMP102_DECIMAL_PLACES must be 0, 1, or 2"
#endif
}

void TMP102_SetConversionRate(I2C_HandleTypeDef *hi2c, uint8_t rate)
{
    uint8_t cfg[2];

    /*
     * Byte 1 (MSB):  R1=1, R0=0  (12-bit resolution, always valid for TMP102)
     *                OS=0, F1:F0=00, POL=0, TM=0, SD=0  (normal operation)
     */
    cfg[0] = 0x60;

    /*
     * Byte 2 (LSB):  bits 7:6 = CR1:CR0 (conversion rate)
     *                bit  5   = AL (0 = alert latch disabled)
     *                bit  4   = EM (extended mode)
     *                bits 3:0 = reserved (0)
     */
    cfg[1] = (uint8_t)(((rate & 0x03) << 6) | 0x00);
#if TMP102_EXTENDED_MODE
    cfg[1] |= 0x10;   /* Set EM bit for 13-bit extended mode */
#endif

    /*
     * Write config register (register 0x01, 2 bytes).
     * Retry once if the I2C bus is stuck.
     */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Write(hi2c, TMP102_ADDR, TMP102_REG_CONFIG,
                                                  I2C_MEMADD_SIZE_8BIT, cfg, 2, 100);
    if (status == HAL_BUSY)
    {
        tmp102_recover_i2c(hi2c);
        HAL_I2C_Mem_Write(hi2c, TMP102_ADDR, TMP102_REG_CONFIG,
                          I2C_MEMADD_SIZE_8BIT, cfg, 2, 100);
    }
}
