/*
 * config.h
 *
 *  Created on: 3. 7. 2026
 *      Author: Oliver Zoller
 */
#pragma once

// ===== SETTINGS =====
#define ENABLE_UART1   1
#define ENABLE_UART2   0
#define ENABLE_WS2812  1
#define ENABLE_OLED    1
#define ENABLE_ADC     0
#define ENABLE_I2C     1
#define ENABLE_TMP102  1
#define ENABLE_SPI     1
#define ENABLE_I2C_SCAN 0
#define ENABLE_CC1101  1
#define UART_DEBUG      1

/* ===== CC1101 driver (ported from mfurga Arduino library) =====
 * Each CC1101 module can be enabled/disabled independently:
 *   CC1101_ENABLE_RADIO1 -> J1 module (CS1=PA11, GDO0_1=PA12, GDO2_1=PB3)
 *   CC1101_ENABLE_RADIO2 -> J3 module (CS2=PC6,  GDO0_2=PA2,  GDO2_2=PA15)
 * Both share SPI1. When both are enabled the examples self-test via loopback.
 */
#define CC1101_ENABLE_RADIO1  1   /* 1 = use J1 module, 0 = disabled */
#define CC1101_ENABLE_RADIO2  0   /* 1 = use J3 module, 0 = disabled */

/* CC1101 reference crystal frequency in MHz (verify on your module). */
#ifndef CC1101_CRYSTAL_FREQ
#define CC1101_CRYSTAL_FREQ   26
#endif



/* ===== OLED FONT SETTINGS ===== */
#define SSD1306_INCLUDE_FONT_6x8	1
#define SSD1306_INCLUDE_FONT_7x10	0
#define SSD1306_INCLUDE_FONT_11x18	0
#define SSD1306_INCLUDE_FONT_16x26	0
#define SSD1306_INCLUDE_FONT_16x24	0
#define SSD1306_INCLUDE_FONT_16x15	0
