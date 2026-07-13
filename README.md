<p align="center">
  <h1 align="center">Lyrion Core C0</h1>
  <p align="center">
    <strong>STM32C031G6UX Development Board Firmware</strong><br>
    CC1101 sub-1 GHz RF, I2C OLED, TMP102 temperature sensing, and WS2812B addressable LEDs
  </p>
</p>

<p align="center">
  <a href="#current-status"><img alt="Status: In Development" src="https://img.shields.io/badge/Status-In%20Development-bf8700?style=flat-square"></a>
  <a href="#board-features"><img alt="MCU: STM32C031" src="https://img.shields.io/badge/MCU-STM32C031-0969da?style=flat-square"></a>
  <a href="#cc1101-driver-driverscc1101"><img alt="Radio: CC1101" src="https://img.shields.io/badge/Radio-CC1101-6639ba?style=flat-square"></a>
  <a href="#license"><img alt="License: GPL-3.0" src="https://img.shields.io/badge/License-GPL--3.0-1a7f37?style=flat-square"></a>
</p>

---

## Table of Contents

1. [Current Status](#current-status)
2. [Board Features](#board-features)
3. [CC1101 Driver](#cc1101-driver-driverscc1101)
4. [Display (OLED)](#display-oled-12864)
5. [Configuration](#configuration-coreincconfigh)
6. [File Structure](#file-structure)
7. [EXTI Interrupts](#exti-interrupts)
8. [Building](#building)
9. [License](#license)

---

This is the firmware for the **Lyrion Core C0 development board**, an **STM32C031G6UX** based platform for prototyping with **CC1101 sub-1 GHz RF modules** (Lyrion Link series), an I2C OLED, temperature sensing, and addressable LEDs.

## Current Status

The CC1101 driver has been ported from the Arduino library by Mateusz Furga
([mfurga/CC1101](https://github.com/mfurga/CC1101)) to plain C for the STM32 HAL.
The board runs in **interrupt-driven receive mode**, displaying received packets
and RSSI on the OLED. The AliExpress CC1101 module has been confirmed to
transmit successfully to the Lyrion Link module.

### Flash optimisation

The original port used `double` throughout, which pulled in about 12 KB of software
double-precision math routines (`__aeabi_d*`) on the Cortex-M0+ (no FPU).
All arithmetic was converted to `float`, and `log2`, `round`, and `fabs` were replaced
with lightweight inline equivalents. The double math library is no longer linked,
which saves about 11 KB of flash.

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

This is a C port of the **Arduino CC1101 library by Mateusz Furga**
([mfurga/CC1101](https://github.com/mfurga/CC1101)) for the STM32 HAL. The full
public API and behaviour of the original library is preserved, and the C++ `Radio`
class is replaced by a `cc1101_t` instance struct passed as the first argument
to every function, which enables multiple independent radios on the shared SPI bus.

**Files:**

| File | Purpose |
|------|---------|
| `cc1101.h` / `cc1101.c` | Portable protocol logic: register map, enums, frequency/power/TX/RX |
| `cc1101_port.h` / `cc1101_port.c` | STM32 HAL glue: SPI transfer, GPIO, timing, EXTI registry + dispatcher |

**Key features (full fidelity with the original).** Supported modulations are
ASK/OOK, 2-FSK, GFSK, 4-FSK, and MSK, across the 300 to 348, 387 to 464, and
779 to 928 MHz bands, with a configurable channel grid, data rate, and RX bandwidth.
Output power spans −30 to +10 dBm via PATABLE with per-band tables. The packet
format supports fixed or variable length, address filtering, CRC, data whitening,
Manchester encoding, and FEC. Both **blocking** TX/RX (`cc1101_transmit`,
`cc1101_receive`) and **interrupt-driven** TX/RX (`cc1101_start_transmit` /
`set_transmit_action` / `finish_transmit`, `cc1101_start_receive` /
`set_receive_action` / `cc1101_read_data`) are available, along with direct
register access (`cc1101_read_reg` / `write_reg` plus field and burst variants),
RSSI and LQI readback, chip ID check, and the datasheet errata workarounds
(FIFO byte re-read).

**Multi-radio and EXTI model.** The board has two CC1101 sockets, **J1**
(CS1=PA11, GDO0=PA12, GDO2=PB3) and **J3** (CS2=PC6, GDO0=PA2, GDO2=PA15), which
share SPI1, and each is a separate `cc1101_t` instance. Interrupt callbacks use a
registry and dispatcher: `set_receive_action` / `set_transmit_action` configure the
GDO pin's EXTI edge and unmask it after the GDO signal is configured, and the
application's `HAL_GPIO_EXTI_Rising/Falling_Callback` forwards to
`cc1101_on_rising_edge()` / `cc1101_on_falling_edge()`, which dispatch to the
registered user callback. ISRs stay tiny and only set a flag. The GDO EXTI lines
are **masked at boot** (`EXTI->IMR1` in `MX_GPIO_Init`) to prevent the CC1101's
default 26 MHz GDO clock output from causing an ISR livelock before `cc1101_init`
writes IOCFG, and the library unmasks a line only when `set_*_action` is called.

**Config (`Core/Inc/config.h`):**
```c
#define ENABLE_CC1101         1   // master enable
#define CC1101_ENABLE_RADIO1  1   // J1 module
#define CC1101_ENABLE_RADIO2  1   // J3 module (0 if not populated)
#define CC1101_CRYSTAL_FREQ   26  // MHz (verify on module)
```

**Do not call CC1101 SPI APIs from ISRs.** ISRs only set flags, and all SPI work
happens in the main loop. The two radios share SPI1 with no mutual exclusion.

### Using the library from `main.c`

The driver is plain C, so there is no Arduino-style `Radio` object to construct.
You declare a `cc1101_t` per physical module, fill in a `cc1101_config_t` with
its pins, call `cc1101_init()`, then configure and use it.

**1. Declare instances and include the headers**

```c
#include "cc1101.h"
#include "cc1101_port.h"   // only needed if you use interrupt actions
```

```c
#if CC1101_ENABLE_RADIO1
cc1101_t radio1;
#endif
#if CC1101_ENABLE_RADIO2
cc1101_t radio2;
#endif
```

**2. Initialise after `MX_SPI1_Init()`**

`cc1101_init()` takes a hardware binding (SPI handle, CS port/pin, MISO
port/pin for `waitReady()` polling, and the two GDO pins, set to
`CC1101_PIN_UNUSED` if not wired). The last two arguments are the initial
modulation, frequency (MHz), and data rate (kBaud), and you can change them
afterwards.

```c
cc1101_config_t cfg1 = {
    .spi       = &hspi1,
    .cs_port   = CS1_GPIO_Port,    .cs_pin   = CS1_Pin,
    .miso_port = GPIOA,            .miso_pin = GPIO_PIN_6,   // SPI1 MISO
    .gdo0_port = GDO0_1_GPIO_Port, .gdo0_pin = GDO0_1_Pin,
    .gdo2_port = GDO2_1_GPIO_Port, .gdo2_pin = GDO2_1_Pin,
};
cc1101_status_t st = cc1101_init(&radio1, &cfg1, CC1101_MOD_ASK_OOK,
                                 433.5, 4.0);
if (st != CC1101_STATUS_OK) { /* error handling */ }
```

**3. Configure radio parameters (mirror of the Arduino setters)**

All `set_*` calls are void or return a `cc1101_status_t`, and the Arduino
settings map one-to-one:

```c
cc1101_set_modulation(&radio1, CC1101_MOD_ASK_OOK);
cc1101_set_frequency (&radio1, 433.8);
cc1101_set_data_rate (&radio1, 10);
cc1101_set_output_power(&radio1, 10);

cc1101_set_packet_length_mode(&radio1, CC1101_PKT_LEN_MODE_VARIABLE, 255);
cc1101_set_address_filtering_mode(&radio1, CC1101_ADDR_FILTER_MODE_NONE);
cc1101_set_preamble_length (&radio1, 64);
cc1101_set_sync_word  (&radio1, 0x1234);
cc1101_set_sync_mode  (&radio1, CC1101_SYNC_MODE_16_16);
cc1101_set_crc        (&radio1, true);
cc1101_set_data_whitening(&radio1, true);
cc1101_set_manchester(&radio1, false);
cc1101_set_fec        (&radio1, false);
```

Other available setters: `cc1101_set_channel`, `cc1101_set_channel_spacing`,
`cc1101_set_frequency_deviation`, `cc1101_set_rx_bandwidth`.

**4. Blocking transmit / receive**

The blocking API takes a `cc1101_t *`, a buffer, a length, and (for
`cc1101_transmit` / `cc1101_receive`) an optional address byte for hardware
address filtering. `cc1101_transmit` returns once the packet has been fully
emitted, and `cc1101_receive` returns once a packet arrives or after
`CC1101_RECV_TIMEOUT_MS` (250 ms default, down from the original 5 s for this
project, defined in `cc1101.h`).

```c
uint8_t tx_buf[] = "Hello world";
cc1101_status_t tx_st = cc1101_transmit(&radio1, tx_buf, sizeof(tx_buf) - 1, 0);

uint8_t rx_buf[64];
size_t   rx_len = 0;
cc1101_status_t rx_st = cc1101_receive(&radio1, rx_buf, sizeof(rx_buf),
                                       &rx_len, 0);
if (rx_st == CC1101_STATUS_OK) {
    int8_t  rssi = cc1101_get_rssi(&radio1);
    uint8_t lqi  = cc1101_get_lqi(&radio1);
    /* rx_buf[0..rx_len-1] is the payload */
}
```

**5. Interrupt-driven transmit / receive**

For non-blocking operation, register a no-argument callback on a GDO pin and
let the EXTI dispatcher in `cc1101_port.c` wake your code. ISRs stay tiny, since
they only flip a flag that the main loop polls.

```c
static volatile bool tx_done = false;
static void on_tx_complete(void) { tx_done = true; }

cc1101_set_transmit_action(&radio1, on_tx_complete, CC1101_GDO0);

cc1101_status_t st = cc1101_start_transmit(&radio1, tx_buf,
                                           sizeof(tx_buf) - 1, 0);
if (st == CC1101_STATUS_OK) {
    while (!tx_done) { /* main loop can do other work here */ }
    tx_done = false;
    cc1101_finish_transmit(&radio1);   // flush TX FIFO / clear flags
}

/* Receive: */
static volatile bool rx_ready = false;
static void on_rx_ready(void) { rx_ready = true; }

cc1101_set_receive_action(&radio1, on_rx_ready, CC1101_GDO0);
cc1101_start_receive(&radio1, 0);

if (rx_ready) {
    rx_ready = false;
    uint8_t rx_buf[64]; size_t rx_len = 0;
    cc1101_read_data(&radio1, rx_buf, sizeof(rx_buf), &rx_len);
    cc1101_start_receive(&radio1, 0);   // re-arm for the next packet
}
```

**Current `main.c` receive block**, interrupt-driven, displays packets on the OLED:

```c
static volatile bool cc1101_rx_ready = false;
static void on_cc1101_rx_data(void) { cc1101_rx_ready = true; }

/* After init + config: */
cc1101_set_receive_action(r, on_cc1101_rx_data, CC1101_GDO0);
cc1101_start_receive(r, 0);

/* In the main loop: */
if (cc1101_radio1 && cc1101_rx_ready) {
    cc1101_rx_ready = false;

    uint8_t rx_buf[64]; size_t rx_len = 0;
    cc1101_status_t st = cc1101_read_data(cc1101_radio1, rx_buf,
                                          sizeof(rx_buf), &rx_len);
    if (st == CC1101_STATUS_OK) {
        rx_buf[rx_len] = '\0';
        int8_t  rssi = cc1101_get_rssi(cc1101_radio1);
        uint8_t lqi  = cc1101_get_lqi(cc1101_radio1);

        /* display on OLED: */
        oled_show_rx((char *)rx_buf, rssi, lqi);
        ssd1306_UpdateScreen();
    }
    cc1101_start_receive(cc1101_radio1, 0);   /* re-arm */
}
```

The `uitoa()` helper (already in `main.c`) is used throughout for converting
numbers to ASCII without pulling in `<stdio.h>` (saves several kB of flash).

---

## Display (OLED 128×64)

The temperature line updates once per second. Received CC1101 packets and their
RSSI/LQI appear immediately (updated from the main loop via interrupt):

```
Lyrion Core C0         ← drawn once at boot
Temp: 24.5 C           ← TMP102 (1 s tick)
RX: Hello #42          ← last received packet
RSSI: -75 dBm  LQI:120 ← RSSI + link quality
(remaining lines blank)
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
| `ENABLE_CC1101` | 1 | CC1101 radio driver (master enable) |
| `ENABLE_AES` | 1 | AES-128-CCM encryption (0 = plaintext) |
| `CC1101_ENABLE_RADIO1` | 1 | J1 module on/off |
| `CC1101_ENABLE_RADIO2` | 1 | J3 module on/off |
| `CC1101_CRYSTAL_FREQ` | 26 | CC1101 reference crystal (MHz) |
| `UART_DEBUG` | 1 | UART diagnostic messages |

---

## File Structure

```
Core/
 ├── Inc/
 │    ├── config.h         feature toggles
 │    ├── main.h           pin definitions
 │    ├── stm32c0xx_it.h   IRQ declarations
 │    └── ...
 ├── Src/
 │    ├── main.c           init loop, EXTI callbacks, OLED display
 │    ├── stm32c0xx_it.c   EXTI/UART/DMA IRQ handlers
 │    └── ...
Drivers/
  ├── CC1101/
  │    ├── cc1101.h        register map, enums, cc1101_t, public API
  │    ├── cc1101.c        protocol logic (SPI regs, freq/power, TX/RX)
  │    ├── cc1101_port.h   STM32 HAL glue declarations
  │    └── cc1101_port.c   HAL glue: SPI/GPIO/timing, EXTI registry
  ├── Crypto/
  │    ├── aes128.h        AES-128 encrypt + decrypt API
  │    ├── aes128.c        S-box + key expansion + cipher rounds
  │    ├── ccm.h           CCM encode/decode API (with AAD)
  │    ├── ccm.c           CCM mode logic (CBC-MAC + CTR)
  │    ├── key.h.example   template (committed)
  │    └── key.h           actual key + nonce prefix (git-ignored)
  ├── LyrionLink/
  │    ├── lyrion_link.h   public API: init, task, send, callbacks
  │    ├── lyrion_link.c   core: state, dispatch, AES-CCM + SEQ
  │    ├── lyrion_link_types.h  enums, flags, error codes
  │    └── lyrion_link_packet.h/c  wire format pack/unpack/AAD
  ├── OLED/
  │    ├── ssd1306.c       I2C OLED controller
  │    └── ssd1306.h
  ├── TMP102/
  │    ├── tmp102.c        I2C temperature sensor
  │    └── tmp102.h
  └── WS2812B/
       ├── ws2812.c        PWM+DMA addressable LED driver
       └── ws2812.h
```

---

## AES-128-CCM Encryption (`Drivers/Crypto/`)

The CC1101 link supports optional AES-128-CCM encryption + authentication, toggled
by `ENABLE_AES` in `config.h`. When enabled, the receiver decrypts and verifies each
packet before displaying it on the OLED.

### Config

```c
#define ENABLE_AES  1   // 1 = encrypted, 0 = plaintext
```

### Key provisioning

1. Copy `Drivers/Crypto/key.h.example` to `Drivers/Crypto/key.h`.
2. Replace the 16-byte `AES_KEY` and 9-byte `AES_NONCE_PREFIX` with your own random
   values.
3. `key.h` is git-ignored — never commit real keys.
4. The same key and nonce prefix must be used on both the STM32 receiver and the
   Arduino transmitter.

### Packet format (CCM-encrypted)

```
Offset  Content                     Size
0-3     packet counter (big-endian) 4 bytes (sent in clear)
4..N-4  ciphertext                   variable (same length as plaintext)
N-4..N  encrypted MAC tag            4 bytes
```

Overhead: 8 bytes/packet (55 bytes max payload, down from 63).

CCM parameters: L=2, M=4, 13-byte nonce (9-byte prefix + 4-byte counter), no AAD.
The counter is authenticated indirectly through the CCM nonce — flipping a counter bit
causes MAC verification to fail.

### Error display on OLED

| Condition | OLED message | Cause |
|-----------|-------------|-------|
| Valid packet | `RX: <decrypted msg>` | Normal operation |
| CC1101 CRC fail | `CRC ERR` | Transmission bit error (hardware CRC-16 caught it) |
| CCM MAC fail | `MAC FAIL` | Deliberate tampering (CRC passed but MAC doesn't match) |
| Replay | `REPLAY!` | Counter <= last seen (packet was recorded and retransmitted) |

### Flash / RAM cost

| Component | Flash | RAM |
|-----------|-------|-----|
| AES-128 encrypt core (S-box + cipher) | ~1.2 KB code + 256 B rodata | 176 B (round keys) |
| CCM encode + decode | ~0.8 KB | 32 B (stack) |
| Integration (nonce, replay check) | ~0.4 KB | 12 B |
| **Total** | **~2.5 KB** | **~220 B** |

---

## EXTI Interrupts

| Pin | Signal | Edge (runtime) | Purpose |
|-----|--------|----------------|---------|
| PA1 | UserButton | rising | Button press |
| PA2 | GDO0_2 (J3) | rising | Packet RX done (set by `set_receive_action`) |
| PA12 | GDO0_1 (J1) | rising | Packet RX done (set by `set_receive_action`) |
| PB3 | GDO2_1 (J1) | falling | TX complete (set by `set_transmit_action`) |
| PA15 | GDO2_2 (J3) | falling | TX complete (set by `set_transmit_action`) |

EXTI interrupts are **masked at boot** (`EXTI->IMR1`) to prevent the CC1101's
default 26 MHz GDO clock output from causing an ISR livelock before `cc1101_init`
writes the IOCFG registers. The library unmasks a line and overrides the edge
only after the GDO signal is configured in `set_receive_action` /
`set_transmit_action`.

---

## Building

Open the project in **STM32CubeIDE** (the `.cproject` / `.project` files are
included). Build the **Debug** configuration and flash via the on-board debugger
(SWD) or the USB-UART bootloader.

---

## License

This project is licensed under the GNU General Public License v3.0 (see
[LICENSE](LICENSE)). The STMicroelectronics HAL drivers are used under ST's
license terms (included with STM32CubeIDE).
