/**
 * @file  cc1101_port.h
 * @brief STM32 HAL hardware-abstraction layer for the CC1101 driver.
 *
 * This isolates all STM32-specific glue (SPI transfer, GPIO, timing, EXTI
 * interrupt attach/detach/dispatch) from the portable protocol logic in
 * cc1101.c. The functions here are called only by cc1101.c; application code
 * interacts with the driver via the public API in cc1101.h.
 *
 * The EXTI dispatcher model: cc1101_init() registers each radio instance.
 * set_receive_action/set_transmit_action call cc1101_port_attach_interrupt()
 * which configures the GDO pin's EXTI edge and unmasks it. The application's
 * HAL_GPIO_EXTI_Rising/Falling_Callback must forward to
 * cc1101_on_rising_edge()/cc1101_on_falling_edge(), which dispatch to the
 * registered user callbacks.
 *
 * @copyright SPDX-License-Identifier: MIT
 */

#ifndef CC1101_PORT_H
#define CC1101_PORT_H

#include "cc1101.h"

/* The port layer needs the real HAL types for SPI/GPIO access. */
#include "stm32c0xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------- */
/*  Timing & GPIO primitives (called by cc1101.c)                             */
/* -------------------------------------------------------------------------- */

/** Millisecond tick (wraps every ~49 days). Equivalent of Arduino millis(). */
uint32_t cc1101_millis(void);

/** Blocking millisecond delay. Equivalent of Arduino delay(). */
void cc1101_delay_ms(uint32_t ms);

/**
 * @brief Blocking microsecond delay (busy loop, approximate).
 * @note  Made weak so the application can override with a timer-based version.
 *        Accuracy depends on SystemCoreClock and compiler optimisation; fine
 *        for the 15-100 us polling waits used by the driver.
 */
void cc1101_delay_us(uint32_t us) __attribute__((weak));

/** Write a GPIO pin (true = HIGH, false = LOW). @c port is an opaque pointer
 *  (e.g. @c GPIOA); the port layer casts it to @c GPIO_TypeDef*. */
void cc1101_gpio_write(void *port, uint16_t pin, bool high);

/** Read a GPIO pin input level (true = HIGH, false = LOW). @c port is an
 *  opaque pointer (e.g. @c GPIOA); the port layer casts it to @c GPIO_TypeDef*. */
bool cc1101_gpio_read(void *port, uint16_t pin);

/**
 * @brief Full-duplex SPI byte exchange (blocking).
 * @param spi   SPI handle.
 * @param tx    Pointer to byte to transmit.
 * @param rx    Pointer to where the received byte is stored.
 * @param size  Number of bytes to exchange.
 */
void cc1101_spi_transfer(SPI_HandleTypeDef *spi, const uint8_t *tx,
                          uint8_t *rx, uint16_t size);

/* -------------------------------------------------------------------------- */
/*  EXTI interrupt registry & dispatch                                        */
/* -------------------------------------------------------------------------- */

/**
 * @brief Register a radio instance with the EXTI dispatcher.
 *        Called by cc1101_init(); app code does not call this directly.
 */
void cc1101_port_register(cc1101_t *r);

/**
 * @brief Attach an EXTI interrupt to a GDO pin.
 *        Configures the edge, clears any pending flag, and unmasks the line.
 * @param r          Radio instance.
 * @param gpio_pin   The GDO GPIO pin (e.g. GDO0_1_Pin).
 * @param rising     true = rising edge, false = falling edge.
 */
void cc1101_port_attach_interrupt(cc1101_t *r, uint16_t gpio_pin, bool rising);

/**
 * @brief Detach the EXTI interrupt from a GDO pin (mask + clear edges).
 */
void cc1101_port_detach_interrupt(cc1101_t *r, uint16_t gpio_pin);

/**
 * @brief Dispatch a rising-edge EXTI to the registered RX-ready callback.
 *        Call this from the application's HAL_GPIO_EXTI_Rising_Callback.
 * @param gpio_pin  The pin that triggered the interrupt.
 */
void cc1101_on_rising_edge(uint16_t gpio_pin);

/**
 * @brief Dispatch a falling-edge EXTI to the registered TX-complete callback.
 *        Call this from the application's HAL_GPIO_EXTI_Falling_Callback.
 * @param gpio_pin  The pin that triggered the interrupt.
 */
void cc1101_on_falling_edge(uint16_t gpio_pin);

#ifdef __cplusplus
}
#endif

#endif /* CC1101_PORT_H */
