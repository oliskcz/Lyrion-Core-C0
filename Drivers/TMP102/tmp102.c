/**
 * @file  tmp102.c
 * @brief Driver for the TI TMP102AIDRLR temperature sensor (I2C).
 *
 * The TMP102 temperature register is 16 bits:
 *   [15:4] = temperature (12-bit signed, 0.0625 °C / LSB)
 *   [3:0]  = don't care when in 12-bit mode (default)
 *
 * Conversion:  (raw >> 4) * 0.0625  °C
 *
 * Note on STM32C0/G0 I2C HAL:
 *   Polling-mode I2C transactions (e.g. ssd1306's HAL_I2C_Mem_Write with
 *   HAL_MAX_DELAY) can leave hi2c1.State == HAL_I2C_STATE_BUSY, causing
 *   subsequent HAL_I2C_Mem_Read calls to immediately return HAL_BUSY.
 *   This driver recovers automatically with a single re-init retry.
 */

#include "tmp102.h"
#include "main.h"
#include <string.h>

/* 7-bit I2C address — shift left once for HAL's 8-bit format. */
#define TMP102_ADDR       (0x49 << 1)
#define TMP102_REG_TEMP   0x00U

extern I2C_HandleTypeDef hi2c1;
extern UART_HandleTypeDef huart1;

/* ---------- Private helpers ------------------------------------------------ */

/*
 * Recover a stuck I2C peripheral by resetting it.
 * This costs ~2 I2C clock cycles and only fires on the rare HAL_BUSY path.
 */
static void tmp102_recover_i2c(I2C_HandleTypeDef *hi2c)
{
    /* Force HAL state back to READY, then re-init the peripheral. */
    hi2c->State = HAL_I2C_STATE_READY;
    HAL_I2C_DeInit(hi2c);
    HAL_I2C_Init(hi2c);
}

/* ---------- Public --------------------------------------------------------- */

uint16_t TMP102_ReadRaw(I2C_HandleTypeDef *hi2c)
{
    uint8_t rxbuf[2] = {0, 0};

    /* Point to the temperature register, then read 2 bytes. */
    HAL_StatusTypeDef status = HAL_I2C_Mem_Read(hi2c, TMP102_ADDR, TMP102_REG_TEMP,
                                               I2C_MEMADD_SIZE_8BIT, rxbuf, 2, 100);

    /* Auto-recover if the bus was left BUSY (common on STM32C0/G0). */
    if (status == HAL_BUSY)
    {
        tmp102_recover_i2c(hi2c);
        status = HAL_I2C_Mem_Read(hi2c, TMP102_ADDR, TMP102_REG_TEMP,
                                  I2C_MEMADD_SIZE_8BIT, rxbuf, 2, 100);
    }

    if (status != HAL_OK)
    {
        return 0xFFFFU;
    }

    return (uint16_t)((rxbuf[0] << 8) | rxbuf[1]);
}

int16_t TMP102_ReadTemp(I2C_HandleTypeDef *hi2c)
{
    uint16_t raw = TMP102_ReadRaw(hi2c);

    if (raw == 0xFFFFU)
    {
        return 9999;   /* sentinel for I2C error */
    }

    /*
     * Extend the 12-bit signed value to a full int16_t, then convert
     * from 0.0625 °C units to whole degrees Celsius.
     *
     * Right-shift by 4 discards the unused lower nibble and gives the
     * value in units of 0.0625 °C.  Multiply by 100/16 = 25/4 = 6.25
     * would give centidegrees, but for the OLED we just want whole
     * degrees, so divide by 16 (= 4 shifts).
     *
     * Sign handling: the raw value from the sensor is big-endian and
     * left-justified as a signed 16-bit number.  Casting to int16_t
     * and right-shifting by 4 performs an arithmetic (sign-extending)
     * shift on GCC/Clang because the left operand is signed.
     */
    int16_t celsius_x16 = (int16_t)raw >> 4;

    /* Round to nearest whole degree instead of truncating. */
    int16_t celsius = (celsius_x16 + 8) / 16;

    return celsius;
}

static void UART_Print(const char *str)
{
    HAL_UART_Transmit(&huart1, (uint8_t *)str, strlen(str), HAL_MAX_DELAY);
}

static void UART_PrintHex(uint8_t value)
{
    char hex[] = "0123456789ABCDEF";
    char buf[2];

    buf[0] = hex[(value >> 4) & 0x0F];
    buf[1] = hex[value & 0x0F];

    HAL_UART_Transmit(&huart1, (uint8_t *)buf, 2, HAL_MAX_DELAY);
}

void I2C_Scan(void)
{
    uint8_t found = 0;

    UART_Print("\r\nScanning I2C bus...\r\n");

    for (uint8_t address = 1; address < 128; address++)
    {
        if (HAL_I2C_IsDeviceReady(&hi2c1, address << 1, 3, 10) == HAL_OK)
        {
            UART_Print("Found device at 0x");
            UART_PrintHex(address);
            UART_Print("\r\n");
            found++;
        }
    }

    if (found == 0)
    {
        UART_Print("No I2C devices found.\r\n");
    }
    else
    {
        UART_Print("Scan complete.\r\n");
    }
}
