# STM32 Lyrion Core C0

Firmware for the **Lyrion Core C0 development board** — an **STM32C031G6UX**-based platform for prototyping with **CC1101 sub-1 GHz RF modules** (Lyrion Link series), I2C OLED, temperature sensing, and addressable LEDs.

---

## Board Features

| Peripheral | Interface | Driver |
|-----------|-----------|--------|
| **CC1101 module J1** (header) | SPI1 (PA5/PA6/PA7), CS1=PA11, GDO0=PA12, GDO2=PB3 | `Drivers/CC1101/cc1101.c` |
| **CC1101 module J3** (header) | SPI1 (shared), CS2=PC6, GDO0=PA2, GDO2=PA15 | same (multi-instance) |
| **0.96" OLED 128×64** | I2C1 (PB7/PB8), addr 0x3C | `Drivers/OLED/ssd1306.c` |
| **TMP102 temperature sensor** | I2C1 (shared), addr 0x48 | `Drivers/TMP102/tmp102.c` |
| **2× WS2812B RGB LEDs** | TIM3_CH3 PWM + DMA | `Drivers/WS2812B/ws2812.c` |
| **USB-UART bridge** (CH340) | USART1 (PB6/PB7) | CubeMX HAL |
| **User button** | PA1 (EXTI0_1 rising) | main.c callback |
| **2× user LEDs** | Blink1=PB5, Blink2=PB4 | GPIO output |

---

## CC1101 Driver (`Drivers/CC1101/`)

Multi-instance SPI driver for the **CC1101 sub-1 GHz transceiver** (26 MHz crystal, GFSK/OOK/2-FSK/4-FSK).

**Key features:**
- Polling or **EXTI-driven RX** (GDO0 falling = packet ready, GDO2 rising = sync detected)
- CHIP_RDYn handshake with timeout
- Dynamic SPI prescaler (safe under HSI 48 MHz fallback)
- Frequency synthesis, channel grid, TX power control (PATABLE)
- Variable-length packets with hardware CRC

**Status displayed on OLED:**
- `R1:OK` / `R2:OK` — radio init status
- `REG:OK` — SPI register readback test
- `SY:nnn` — sync detection counter (GDO2)

**Config (`config.h`):**
```c
#define ENABLE_CC1101  1            // enable the driver
#define LYRION_NUM_MODULES 1        // 1 or 2 radios
```

---

## Display (OLED 128×64)

Updated once per second via the main loop 1-second tick:

```
Lyrion Core C0
Hello, World!
Up: 123s        ← uptime
Temp: 24.5 C    ← TMP102
Led: ON         ← Blink1 state
R1:OK           ← radio init
REG:OK SY:042   ← register test + sync count
```

---

## Configuration (`Core/Inc/config.h`)

| Macro | Default | Purpose |
|-------|---------|---------|
| `ENABLE_UART1` | 1 | USB-UART bridge |
| `ENABLE_WS2812` | 1 | Addressable LEDs |
| `ENABLE_OLED` | 1 | I2C OLED display |
| `ENABLE_TMP102` | 1 | Temperature sensor |
| `ENABLE_SPI` | 1 | SPI1 for CC1101 |
| `ENABLE_CC1101` | 1 | CC1101 radio driver |
| `LYRION_NUM_MODULES` | 1 | Number of CC1101 modules (1 or 2) |
| `UART_DEBUG` | 1 | UART diagnostic messages |
| `LYRION_XTAL_HZ` | 26000000 | CC1101 reference crystal (verify on module) |

---

## File Structure

```
Core/
 ├── Inc/
 │    ├── config.h          — feature toggles
 │    ├── main.h            — pin definitions
 │    ├── stm32c0xx_it.h    — IRQ declarations
 │    └── ...
 ├── Src/
 │    ├── main.c            — init loop, EXTI callbacks, OLED display
 │    ├── stm32c0xx_it.c    — EXTI/UART/DMA IRQ handlers
 │    └── ...
Drivers/
 ├── CC1101/
 │    ├── cc1101.h          — register map, API, driver instance struct
 │    └── cc1101.c          — SPI access, frequency/channel/power, TX/RX
 ├── OLED/
 │    ├── ssd1306.c         — I2C OLED controller
 │    └── ssd1306.h
 ├── TMP102/
 │    ├── tmp102.c          — I2C temperature sensor
 │    └── tmp102.h
 └── WS2812B/
      ├── ws2812.c          — PWM+DMA addressable LED driver
      └── ws2812.h
```

---

## EXTI Interrupts

| Pin | Signal | Edge | Purpose |
|-----|--------|------|---------|
| PA1 | UserButton | rising | Button press |
| PA2 | GDO0_2 (J3) | falling | Packet RX done |
| PA12 | GDO0_1 (J1) | falling | Packet RX done |
| PB3 | GDO2_1 (J1) | rising | Sync detected |
| PA15 | GDO2_2 (J3) | rising | Sync detected |

EXTI interrupts are **masked during boot** (`EXTI->IMR1`) to prevent the CC1101's default 26 MHz clock output from causing an ISR livelock. They are unmasked in `USER CODE BEGIN 2` after `CC1101_Init()` has written `IOCFG=0x06` to disable the clock output.

---

## Building

Open the project in **STM32CubeIDE** (the `.cproject` / `.project` files are included). Build the **Debug** configuration and flash via the on-board debugger (SWD) or USB-UART bootloader.

---

## License

STMicroelectronics HAL drivers are used under ST's license terms (included with STM32CubeIDE).
