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
#define LYRION_NUM_MODULES 1   /* 1 or 2 — controls radio2 init + loopback test */
#define UART_DEBUG      1

// ===== OLED FONT SETTINGS =====
#define SSD1306_INCLUDE_FONT_6x8	1
#define SSD1306_INCLUDE_FONT_7x10	0
#define SSD1306_INCLUDE_FONT_11x18	0
#define SSD1306_INCLUDE_FONT_16x26	0
#define SSD1306_INCLUDE_FONT_16x24	0
#define SSD1306_INCLUDE_FONT_16x15	0
