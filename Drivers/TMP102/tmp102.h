/**
 * @file  tmp102.h
 * @brief Driver for the TI TMP102AIDRLR temperature sensor (I2C).
 *
 * Adapted from https://github.com/davidracl/TMP102-SCD30-sensors-for-I2C-STM32
 *
 * The TMP102 has two address pins (A0, A1) giving four possible 7-bit addresses:
 *   0x48, 0x49, 0x4A, 0x4B.
 * Default (both pins grounded) is 0x48.  Change TMP102_ADDR in tmp102.c to match
 * your PCB wiring.
 */

#ifndef TMP102_H
#define TMP102_H

#include "main.h"

/**
 * @brief  Read the current temperature from the sensor.
 * @param  hi2c  Pointer to the I2C handle (e.g. &hi2c1).
 * @retval Temperature in degrees Celsius as a signed fixed-point value
 *         (integer part only, truncated).  Returns 9999 on error.
 */
int16_t TMP102_ReadTemp(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Read the raw 16-bit temperature register value.
 * @param  hi2c  Pointer to the I2C handle.
 * @retval Raw register value, or 0xFFFF on I2C error.
 */
uint16_t TMP102_ReadRaw(I2C_HandleTypeDef *hi2c);

static void UART_Print(const char *str);
static void UART_PrintHex(uint8_t value);
void I2C_Scan(void);

#endif /* TMP102_H */
