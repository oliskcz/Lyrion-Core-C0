#ifndef WS2812_H
#define WS2812_H

#include <stdint.h>

#define WS2812_LED_COUNT 2

void WS2812_Init(void);
void WS2812_SetLED(uint8_t led, uint8_t red, uint8_t green, uint8_t blue);
void WS2812_SetBrightness(uint8_t led, uint8_t brightness);
void WS2812_Send(void);
void WS2812_Example1(void);

#endif /* WS2812_H */
