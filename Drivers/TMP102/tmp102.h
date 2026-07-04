/**
 * @file  tmp102.h
 * @brief Driver for the TI TMP102AIDRLR temperature sensor (I2C).
 *
 * Adapted from https://github.com/davidraclcl/TMP102-SCD30-sensors-for-I2C-STM32
 *
 * The TMP102 has two address pins (A0, A1) giving four possible 7-bit addresses:
 *   0x48, 0x49, 0x4A, 0x4B.
 * Default (both pins grounded) is 0x48.  Change TMP102_ADDR_7BIT below to match
 * your PCB wiring.
 *
 * === USER-CONFIGURABLE SETTINGS ===
 *
 * TMP102_DECIMAL_PLACES : 0 = whole degrees, 1 = 0.1°C, 2 = 0.01°C
 *   Native sensor resolution is 0.0625 °C (both 12-bit and 13-bit extended
 *   modes).  The extra bit in extended mode extends the measurement range
 *   to 150 °C; the LSB remains 0.0625 °C.  1 decimal is the max meaningful
 *   precision; 2 decimals are aesthetic with the 8 Hz conversion rate.
 *
 * TMP102_CONV_RATE : Conversion rate (config register CR1:CR0)
 *   0 = 0.25 Hz   (4 s between conversions, lowest power)
 *   1 = 1 Hz       (1 s between conversions)
 *   2 = 4 Hz       (250 ms, default after power-up)
 *   3 = 8 Hz       (125 ms, highest power)
 *
 * TMP102_EXTENDED_MODE : 0 = 12-bit (0.0625 °C/LSB), 1 = 13-bit (0.03125 °C/LSB)
 *   Extended mode doubles the resolution for smoother 2-decimal display.
 *   The driver auto-detects the mode from the temperature register bit 2.
 */

#ifndef TMP102_H
#define TMP102_H

#include "main.h"

/* ---- User settings (override in config.h if desired) -------------------- */
#ifndef TMP102_ADDR_7BIT
#define TMP102_ADDR_7BIT     0x49   /* 7-bit I2C address (0x48-0x4B) */
#endif

#ifndef TMP102_DECIMAL_PLACES
#define TMP102_DECIMAL_PLACES 2     /* 0, 1, or 2 decimal places */
#endif

#ifndef TMP102_CONV_RATE
#define TMP102_CONV_RATE      3     /* 0=0.25Hz, 1=1Hz, 2=4Hz(default), 3=8Hz */
#endif

#ifndef TMP102_EXTENDED_MODE
#define TMP102_EXTENDED_MODE 0      /* 0=12-bit, 1=13-bit extended */
#endif
/* -------------------------------------------------------------------------- */

/* Error sentinel returned by TMP102_ReadTemp on I2C failure. */
#define TMP102_ERROR  9999

/**
 * @brief  Read temperature with configured decimal precision.
 *
 * Automatically detects 12-bit (normal) or 13-bit (extended) mode from the
 * temperature register bit 2 and applies the correct scaling.
 *
 * @param  hi2c  Pointer to the I2C handle.
 * @retval Temperature scaled by 10^TMP102_DECIMAL_PLACES, or TMP102_ERROR.
 *
 * If TMP102_DECIMAL_PLACES == 1, a reading of 25.3 °C returns 253.
 * If TMP102_DECIMAL_PLACES == 2, a reading of 25.31 °C returns 2531.
 * Negative temperatures are returned as negative values (e.g. -55.0 → -550).
 */
int16_t TMP102_ReadTemp(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Read the raw 16-bit temperature register value.
 * @param  hi2c  Pointer to the I2C handle.
 * @retval Raw register value, or 0xFFFF on error.
 */
uint16_t TMP102_ReadRaw(I2C_HandleTypeDef *hi2c);

/**
 * @brief  Set the TMP102 conversion rate and extended mode.
 *
 * Writes CR1:CR0 into the config register (register 0x01, byte 2 bits 7:6)
 * and optionally sets the EM bit (bit 4) for 13-bit extended mode.
 * Call once during init; the sensor retains the setting until power-cycled.
 *
 * @param  hi2c  Pointer to the I2C handle.
 * @param  rate  0 = 0.25 Hz, 1 = 1 Hz, 2 = 4 Hz (default), 3 = 8 Hz.
 */
void TMP102_SetConversionRate(I2C_HandleTypeDef *hi2c, uint8_t rate);

#endif /* TMP102_H */
