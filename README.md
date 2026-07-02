# STM32 Lyrion Core C0 - Lyrion Link M1 / STM32C031 Peripheral Test Platform

## Overview

This project is based on the **STM32C031G6U6TR** microcontroller and serves as the firmware for the **Lyrion Core C0 development board**.

The board is designed as:
- A carrier platform for **2× Lyrion Link M1 Lite H / L modules**
- A general-purpose STM32C031 test and validation board for future PCB designs

It provides access to multiple peripherals for testing and prototyping:
- 1× User button (`UserButton`)
- 2× User LEDs (`Blink1`, `Blink2`)
- USB-UART bridge via **CH340 (USART1)**
- Exposed **USART2 header**
- Exposed **I2C interface**
- 0.96" I2C OLED (128×64) support header
- Exposed **SPI interface**
- 2× WS2812B addressable LEDs on pin `WS2812B`
- Boot button and reset button
- **TMP102AIDRLR I2C temperature sensor**

---

## WS2812 Driver Module

The WS2812 driver is implemented using:
- TIM3 PWM output (Channel 3)
- DMA-based transmission
- ~800 kHz timing base

---

## API Functions

```
void WS2812_Init(void);
void WS2812_SetLED(uint8_t led, uint8_t red, uint8_t green, uint8_t blue);
void WS2812_SetBrightness(uint8_t led, uint8_t brightness);
void WS2812_Send(void);
void WS2812_Example1(void);
```

---

## Configuration Macros (ws2812.c)

These values control timing and animation behavior:

```
#define WS2812_BITS_PER_LED      24
#define WS2812_RESET_SLOTS       80
#define WS2812_LEADING_SLOTS     1
#define WS2812_PWM_HIGH_0        3
#define WS2812_PWM_HIGH_1        6
#define WS2812_Example1_SPEED    50
```

Notes:
- PWM resolution depends on TIM3 ARR configuration
- TIM3 is configured for ~800 kHz WS2812 timing
- DMA is used for continuous bitstream generation
- Reset time must exceed 50 µs

---

## ws2812.h Configuration

```
#define WS2812_LED_COUNT 2
```

This defines the number of WS2812 LEDs in the chain.

---

## Typical Usage

### Initialization
```
WS2812_Init();
```

### Set LED Color
```
WS2812_SetLED(0, 255, 0, 0);   // LED 0 = Red
WS2812_SetLED(1, 0, 255, 0);   // LED 1 = Green
```

### Set Brightness
```
WS2812_SetBrightness(0, 64);   // LED 0 ~25%
WS2812_SetBrightness(1, 255);  // LED 1 full brightness
```

### Send Data to LEDs
```
WS2812_Send();
```

---

## Example Animation Loop

```
while (1)
{
    WS2812_Example1();
}
```

This function:
- Converts a hue value into RGB
- Applies a color offset between LEDs
- Generates a smooth rainbow effect
- Uses DMA for minimal CPU usage
- Runs continuously for animation

---

## Hardware Configuration

### Timer Setup (WS2812)
- Timer: TIM3
- Channel: CH3
- Mode: PWM + DMA
- Target frequency: ~800 kHz

### Critical Requirements
- Timing must be precise for WS2812 protocol
- DMA transfer must not be interrupted
- Reset pulse must be >50 µs low signal

---

## File Structure

Recommended STM32CubeIDE structure:

```
Core/
 ├── Inc/
 │    ├── ws2812.h
 │    └── main.h
 │
 ├── Src/
 │    ├── main.c
 │    ├── ws2812.c
 │    └── stm32c0xx_it.c
```

---

## Recommended Usage Notes

- Do not modify generated HAL files outside `USER CODE` sections
- Keep all WS2812 logic inside `ws2812.c`
- Ensure DMA interrupts are enabled for TIM3
- Always call `WS2812_Send()` after updating LED data

---

## Project Purpose

This firmware is intended for:

- Hardware bring-up validation
- Peripheral testing (UART, I2C, SPI, ADC, PWM, DMA)
- WS2812 RGB LED control experiments
- RF module integration testing (Lyrion Link series)
- STM32C031G6U6TR evaluation platform development

---

## License

This project uses **STMicroelectronics HAL drivers** and follows their licensing terms included in STM32CubeIDE.
