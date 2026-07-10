/**
 * @file  cc1101_port.c
 * @brief STM32 HAL hardware-abstraction layer for the CC1101 driver.
 *
 * Implements the SPI/GPIO/timing primitives and the EXTI interrupt registry +
 * dispatcher declared in cc1101_port.h.
 *
 * EXTI model:
 *  - CubeMX configures the GDO pins as EXTI (rising/falling) and enables the
 *    NVIC at boot. To avoid the CC1101's default 26 MHz GDO clock output
 *    causing an ISR livelock before IOCFG is written, the application must
 *    mask those EXTI lines (EXTI->IMR1) in MX_GPIO_Init USER CODE. This file's
 *    cc1101_port_attach_interrupt() then unmasks a line only after the library
 *    has configured the GDO signal (in set_receive/transmit_action).
 *  - A static array holds all initialised radio instances. The dispatch
 *    functions cc1101_on_rising_edge()/cc1101_on_falling_edge() look up which
 *    instance owns the triggered pin and invoke its stored user callback.
 *
 * @copyright SPDX-License-Identifier: MIT
 */

#include "cc1101_port.h"
#include "main.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Timing primitives                                                         */
/* -------------------------------------------------------------------------- */

uint32_t cc1101_millis(void)
{
    return HAL_GetTick();
}

void cc1101_delay_ms(uint32_t ms)
{
    HAL_Delay(ms);
}

/* Weak default: calibrated busy loop on SystemCoreClock. Override with a
 * timer/counter-based implementation for higher accuracy if needed. */
__attribute__((weak)) void cc1101_delay_us(uint32_t us)
{
    /* Each NOP consumes ~1 cycle. Add a small margin for loop overhead. */
    uint32_t cycles = (SystemCoreClock / 1000000UL) * us;
    while (cycles--) {
        __NOP();
    }
}

/* -------------------------------------------------------------------------- */
/*  GPIO primitives                                                           */
/* -------------------------------------------------------------------------- */

void cc1101_gpio_write(void *port, uint16_t pin, bool high)
{
    HAL_GPIO_WritePin((GPIO_TypeDef *)port, pin, high ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool cc1101_gpio_read(void *port, uint16_t pin)
{
    /* GPIO IDR is readable even when the pin is in alternate-function mode
     * (as MISO is), so this works for waitReady() polling. */
    return HAL_GPIO_ReadPin((GPIO_TypeDef *)port, pin) == GPIO_PIN_SET;
}

/* -------------------------------------------------------------------------- */
/*  SPI primitive                                                             */
/* -------------------------------------------------------------------------- */

void cc1101_spi_transfer(SPI_HandleTypeDef *spi, const uint8_t *tx,
                          uint8_t *rx, uint16_t size)
{
    /* HAL_SPI_TransmitReceive requires non-const tx; cast is safe because the
     * HAL does not modify the transmit buffer. */
    HAL_SPI_TransmitReceive(spi, (uint8_t *)tx, rx, size, CC1101_SPI_TIMEOUT_MS);
}

/* -------------------------------------------------------------------------- */
/*  EXTI registry & dispatch                                                  */
/* -------------------------------------------------------------------------- */

#ifndef CC1101_MAX_INSTANCES
#define CC1101_MAX_INSTANCES 4
#endif

static cc1101_t *g_instances[CC1101_MAX_INSTANCES];
static uint8_t   g_instance_count;

void cc1101_port_register(cc1101_t *r)
{
    /* Avoid double-registration on re-init. */
    for (uint8_t i = 0; i < g_instance_count; i++) {
        if (g_instances[i] == r) {
            return;
        }
    }
    if (g_instance_count < CC1101_MAX_INSTANCES) {
        g_instances[g_instance_count++] = r;
        r->registered = true;
    }
}

static void exti_clear_pending(uint16_t pin)
{
    /* Clear both rising and falling pending flags. */
    EXTI->RPR1 = pin;
    EXTI->FPR1 = pin;
}

void cc1101_port_attach_interrupt(cc1101_t *r, uint16_t gpio_pin, bool rising)
{
    (void)r;
    if (gpio_pin == CC1101_PIN_UNUSED) {
        return;
    }

    /* Mask while reconfiguring to avoid a glitch firing on the edge change. */
    EXTI->IMR1 &= ~gpio_pin;

    exti_clear_pending(gpio_pin);

    if (rising) {
        EXTI->FTSR1 &= ~gpio_pin;   /* disable falling */
        EXTI->RTSR1 |=  gpio_pin;   /* enable rising  */
    } else {
        EXTI->RTSR1 &= ~gpio_pin;   /* disable rising  */
        EXTI->FTSR1 |=  gpio_pin;   /* enable falling  */
    }

    exti_clear_pending(gpio_pin);

    /* Unmask: the GDO signal is now configured, so edges are meaningful. */
    EXTI->IMR1 |= gpio_pin;
}

void cc1101_port_detach_interrupt(cc1101_t *r, uint16_t gpio_pin)
{
    (void)r;
    if (gpio_pin == CC1101_PIN_UNUSED) {
        return;
    }
    EXTI->IMR1  &= ~gpio_pin;
    EXTI->RTSR1 &= ~gpio_pin;
    EXTI->FTSR1 &= ~gpio_pin;
    exti_clear_pending(gpio_pin);
}

void cc1101_on_rising_edge(uint16_t gpio_pin)
{
    for (uint8_t i = 0; i < g_instance_count; i++) {
        cc1101_t *r = g_instances[i];
        if (r->rx_action && r->rx_action_gpio == gpio_pin) {
            r->rx_action();
            return;
        }
    }
}

void cc1101_on_falling_edge(uint16_t gpio_pin)
{
    for (uint8_t i = 0; i < g_instance_count; i++) {
        cc1101_t *r = g_instances[i];
        if (r->tx_action && r->tx_action_gpio == gpio_pin) {
            r->tx_action();
            return;
        }
    }
}
