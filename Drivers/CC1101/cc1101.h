/**
 * @file  cc1101.h
 * @brief Minimal bare CC1101 SPI driver (polling, no EXTI).
 *
 * Reference: github.com/dsoldevila/CC1101 (dsoldevila/CC1101)
 *
 * === DEFAULT 26 MHz CRYSTAL ===
 * The CC1101 modules on Lyrion Core C0 use a 26 MHz reference crystal.
 * Override LYRION_XTAL_HZ in config.h if your module differs.
 *
 * No libc calls — compatible with the project's /DISCARD/ of libc/libm/libgcc.
 */

#ifndef CC1101_H
#define CC1101_H

#include "main.h"
#include <stdint.h>

/* ---- User-configurable (override in config.h) --------------------------- */
#ifndef LYRION_XTAL_HZ
#define LYRION_XTAL_HZ  26000000UL
#endif
#ifndef CC1101_MAX_PACKET
#define CC1101_MAX_PACKET  64
#endif
/* -------------------------------------------------------------------------- */

/* ---- Register addresses -------------------------------------------------- */
#define CC1101_IOCFG2     0x00
#define CC1101_IOCFG1     0x01
#define CC1101_IOCFG0     0x02
#define CC1101_FIFOTHR    0x03
#define CC1101_SYNC1      0x04
#define CC1101_SYNC0      0x05
#define CC1101_PKTLEN     0x06
#define CC1101_PKTCTRL1   0x07
#define CC1101_PKTCTRL0   0x08
#define CC1101_ADDR       0x09
#define CC1101_CHANNR     0x0A
#define CC1101_FSCTRL1    0x0B
#define CC1101_FSCTRL0    0x0C
#define CC1101_FREQ2      0x0D
#define CC1101_FREQ1      0x0E
#define CC1101_FREQ0      0x0F
#define CC1101_MDMCFG4    0x10
#define CC1101_MDMCFG3    0x11
#define CC1101_MDMCFG2    0x12
#define CC1101_MDMCFG1    0x13
#define CC1101_MDMCFG0    0x14
#define CC1101_DEVIATN    0x15
#define CC1101_MCSM0      0x18
#define CC1101_FOCCFG     0x19
#define CC1101_BSCFG      0x1A
#define CC1101_AGCCTRL2   0x1B
#define CC1101_AGCCTRL1   0x1C
#define CC1101_AGCCTRL0   0x1D
#define CC1101_WOREVT1    0x1E
#define CC1101_WOREVT0    0x1F
#define CC1101_WORCTRL    0x20
#define CC1101_FREND1     0x21
#define CC1101_FREND0     0x22
#define CC1101_FSCAL3     0x23
#define CC1101_FSCAL2     0x24
#define CC1101_FSCAL1     0x25
#define CC1101_FSCAL0     0x26
#define CC1101_FSTEST     0x29
#define CC1101_TEST2      0x2C
#define CC1101_TEST1      0x2D
#define CC1101_TEST0      0x2E

/* ---- Status registers (read with burst bit) ----------------------------- */
#define CC1101_PARTNUM    0x30
#define CC1101_VERSION    0x31
#define CC1101_RSSI       0x34
#define CC1101_MARCSTATE  0x35
#define CC1101_RXBYTES    0x3B

/* ---- Command strobes ----------------------------------------------------- */
#define CC1101_SRES       0x30
#define CC1101_SFSTXON    0x31
#define CC1101_SXOFF      0x32
#define CC1101_SCAL       0x33
#define CC1101_SRX        0x34
#define CC1101_STX        0x35
#define CC1101_SIDLE      0x36
#define CC1101_SWOR       0x37
#define CC1101_SPWD       0x38
#define CC1101_SFRX       0x3A
#define CC1101_SFTX       0x3B
#define CC1101_SWORRST    0x3C

/* ---- SPI header bits ----------------------------------------------------- */
#define CC1101_WRITE      0x00
#define CC1101_READ       0x80
#define CC1101_BURST      0x40

/* ---- FIFO and PATABLE access (use with R/W + BURST flags) ---------------- */
#define CC1101_TXFIFO     0x3F
#define CC1101_RXFIFO     0x3F
#define CC1101_PATABLE    0x3E

/* ---- MARCSTATE values ---------------------------------------------------- */
#define CC1101_STATE_IDLE         0x01
#define CC1101_STATE_RX           0x0D
#define CC1101_STATE_TX           0x13

/* ---- Modulation types (register config tables) --------------------------- */
typedef enum {
    CC1101_MOD_GFSK_38_4KB = 0,
    CC1101_MOD_GFSK_1_2KB,
    CC1101_MOD_GFSK_100KB,
    CC1101_MOD_MSK_250KB,
    CC1101_MOD_MSK_500KB,
    CC1101_MOD_OOK_4_8KB,
} cc1101_mod_t;

/* ---- ISM bands ----------------------------------------------------------- */
typedef enum {
    CC1101_BAND_315 = 0,
    CC1101_BAND_433,
    CC1101_BAND_868,
    CC1101_BAND_915,
} cc1101_band_t;

/* ---- Driver instance ----------------------------------------------------- */
typedef struct {
    SPI_HandleTypeDef *spi;          /* shared &hspi1 */
    GPIO_TypeDef    *cs_port;  uint16_t cs_pin;
    GPIO_TypeDef    *gdo0_port; uint16_t gdo0_pin; /* RX-done (falling EXTI) */
    GPIO_TypeDef    *gdo2_port; uint16_t gdo2_pin; /* sync detected (rising EXTI) */
    /* buffers / state */
    uint8_t          tx_buf[CC1101_MAX_PACKET];
    uint8_t          rx_buf[CC1101_MAX_PACKET];
    volatile uint8_t rx_ready;       /* set in EXTI falling callback */
    volatile uint8_t sync_seen;      /* set in EXTI rising callback  */
} cc1101_t;

/* ---- Public API ---------------------------------------------------------- */

/* Lifecycle */
uint8_t CC1101_Init(cc1101_t *d, cc1101_band_t band, cc1101_mod_t mod);
void    CC1101_Reset(cc1101_t *d);
uint8_t CC1101_Check(cc1101_t *d);         /* read VERSION, expect 0x14, return 1 if OK */

/* Diagnotics: last VERSION byte read by Check (useful on failure). */
extern volatile uint8_t cc1101_dbg_ver;

/* SPI register access */
void    CC1101_WriteReg(cc1101_t *d, uint8_t addr, uint8_t value);
uint8_t CC1101_ReadReg(cc1101_t *d, uint8_t addr);
void    CC1101_WriteStrobe(cc1101_t *d, uint8_t strobe);
void    CC1101_WriteBurst(cc1101_t *d, uint8_t addr, const uint8_t *data, uint8_t n);
void    CC1101_ReadBurst(cc1101_t *d, uint8_t addr, uint8_t *data, uint8_t n);

/* RF configuration */
void    CC1101_SetFrequency(cc1101_t *d, uint32_t hz);  /* raw Hz (clamped to 300-1000 MHz) */
void    CC1101_SetChannel(cc1101_t *d, uint8_t ch);     /* CHANNR register */
void    CC1101_SetTxPower(cc1101_t *d, int8_t dbm);     /* -30 to +10 dBm */

/* State */
void    CC1101_SetRx(cc1101_t *d);
void    CC1101_SetIdle(cc1101_t *d);
void    CC1101_SetSleep(cc1101_t *d);

/* Packet (polling = no GDO interrupt) */
void    CC1101_FlushRx(cc1101_t *d);
void    CC1101_FlushTx(cc1101_t *d);
uint8_t CC1101_SendPacket(cc1101_t *d, const uint8_t *data, uint8_t len);  /* len <= 61 */
uint8_t CC1101_ReceivePacket(cc1101_t *d, uint8_t *buf, uint8_t max_len, int8_t *rssi);

/* Utility */
uint8_t CC1101_WaitMiso(cc1101_t *d);     /* CHIP_RDYn spin */

/**
 * @brief  EXTI dispatch helpers — called from HAL_GPIO_EXTI_Rising/Falling
 *         callback in main.c.  Set rx_ready / sync_seen on the instance
 *         whose GDO pin matches.  No-op if pin does not match.
 */
void    CC1101_HandleGdo0(cc1101_t *d, uint16_t pin);
void    CC1101_HandleGdo2(cc1101_t *d, uint16_t pin);

#endif /* CC1101_H */
