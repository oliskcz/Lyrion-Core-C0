/**
 * @file  cc1101.c
 * @brief Minimal CC1101 SPI driver (polling, no EXTI).
 *
 * SPI access is fully blocking (HAL_SPI_TransmitReceive). CHIP_RDYn handshake
 * uses a software timeout on MISO. Everything runs in thread context — no
 * interrupt handlers touch the radio.
 *
 * Register configuration tables adapted from dsoldevila/CC1101 (GitHub).
 *
 * No libc calls.
 */

#include "cc1101.h"

/* ---- MISO pin used for CHIP_RDYn handshake ------------------------------- */
/* Both radio modules on Lyrion Core C0 share SPI1 (MISO = PA6). */
#define CC1101_MISO_PORT  GPIOA
#define CC1101_MISO_PIN   GPIO_PIN_6

/* ---- Crystal frequency for FREQ computation ------------------------------ */
#define CC1101_XTAL_HZ    LYRION_XTAL_HZ

/* ---- Register config tables (from dsoldevila/CC1101, via SmartRF Studio) - */
/* Each table is 47 bytes, one per config register (IOCFG2 … TEST0). */
#define CC1101_NUM_CFG_REGS  47

static const uint8_t cc1101_cfg_gfsk_38k4[CC1101_NUM_CFG_REGS] = {
    0x06, 0x2E, 0x06, 0x07, 0x57, 0x43, 0x3E, 0xDC, 0x45, /* 00-08 */
    0xFF, 0x00, 0x06, 0x00, 0x21, 0x65, 0x6A, 0xCA, 0x83, /* 09-11 */
    0x13, 0xA0, 0xF8, 0x34, 0x07, 0x0C, 0x18, 0x16, 0x6C, /* 12-1A */
    0x43, 0x40, 0x91, 0x02, 0x26, 0x09, 0x56, 0x17, 0xA9, /* 1B-23 */
    0x0A, 0x00, 0x11, 0x41, 0x00, 0x59, 0x7F, 0x3F, 0x81, /* 24-2C */
    0x3F, 0x0B                                            /* 2D-2E */
};

static const uint8_t cc1101_cfg_gfsk_1k2[CC1101_NUM_CFG_REGS] = {
    0x07, 0x2E, 0x80, 0x07, 0x57, 0x43, 0x3E, 0xD8, 0x45,
    0xFF, 0x00, 0x08, 0x00, 0x21, 0x65, 0x6A, 0xF5, 0x83,
    0x13, 0xC0, 0xF8, 0x15, 0x07, 0x00, 0x18, 0x16, 0x6C,
    0x03, 0x40, 0x91, 0x02, 0x26, 0x09, 0x56, 0x17, 0xA9,
    0x0A, 0x00, 0x11, 0x41, 0x00, 0x59, 0x7F, 0x3F, 0x81,
    0x3F, 0x0B
};

/* ---- PATABLE values for 433 MHz (0 = -30 dBm … 7 = +10 dBm) -------------- */
static const uint8_t patable_433[8] = {
    0x6C, 0x1C, 0x06, 0x3A, 0x51, 0x85, 0xC8, 0xC0
};

/* ---- Band centre frequencies (Hz) ----------------------------------------- */
static const uint32_t band_centre[4] = {
    315000000UL,
    434000000UL,
    868300000UL,
    915000000UL
};

/* ---- Diagnostics --------------------------------------------------------- */
volatile uint8_t cc1101_dbg_ver = 0;  /* last VERSION read by Check */

/* ======================================================================== */
/*  Private helpers                                                          */
/* ======================================================================== */

static uint8_t rx_dummy = 0;

static inline void cs_low(cc1101_t *d) {
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_RESET);
}
static inline void cs_high(cc1101_t *d) {
    HAL_GPIO_WritePin(d->cs_port, d->cs_pin, GPIO_PIN_SET);
}

/*
 * Wait for MISO==1 (CHIP_RDYn). The GPIO IDR reflects pin level even in AF
 * mode, so a read of PA6 works.  Reference library (suleymaneskil/CC1101)
 * uses an unconditional spin — we add a 100 ms safety timeout to avoid
 * hanging on a dead chip.  The CC1101 typically releases CHIP_RDYn within
 * 150 us; after a warm reset it can take up to 1-2 ms.
 */
uint8_t CC1101_WaitMiso(cc1101_t *d)
{
    (void)d;
    uint32_t deadline = HAL_GetTick() + 100;  /* 100 ms absolute cap */
    while (HAL_GPIO_ReadPin(CC1101_MISO_PORT, CC1101_MISO_PIN) == GPIO_PIN_RESET) {
        if (HAL_GetTick() > deadline) return 0;
    }
    return 1;
}

/* ======================================================================== */
/*  SPI access                                                               */
/* ======================================================================== */

void CC1101_WriteStrobe(cc1101_t *d, uint8_t strobe)
{
    uint8_t rx;
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, &strobe, &rx, 1, 100);
    cs_high(d);
}

void CC1101_WriteReg(cc1101_t *d, uint8_t addr, uint8_t value)
{
    uint8_t tx[2] = { (uint8_t)(addr | CC1101_WRITE), value };
    uint8_t rx[2];
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, tx, rx, 2, 100);
    cs_high(d);
}

uint8_t CC1101_ReadReg(cc1101_t *d, uint8_t addr)
{
    uint8_t hdr = (uint8_t)(addr | CC1101_READ | CC1101_BURST);
    uint8_t val = 0;
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, &hdr, &rx_dummy, 1, 100);
    HAL_SPI_TransmitReceive(d->spi, &rx_dummy, &val, 1, 100);
    cs_high(d);
    return val;
}

void CC1101_WriteBurst(cc1101_t *d, uint8_t addr, const uint8_t *data, uint8_t n)
{
    uint8_t hdr = (uint8_t)(addr | CC1101_WRITE | CC1101_BURST);
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, &hdr, &rx_dummy, 1, 100);
    for (uint8_t i = 0; i < n; i++)
        HAL_SPI_TransmitReceive(d->spi, (uint8_t *)&data[i], &rx_dummy, 1, 100);
    cs_high(d);
}

void CC1101_ReadBurst(cc1101_t *d, uint8_t addr, uint8_t *data, uint8_t n)
{
    uint8_t hdr = (uint8_t)(addr | CC1101_READ | CC1101_BURST);
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, &hdr, &rx_dummy, 1, 100);
    for (uint8_t i = 0; i < n; i++)
        HAL_SPI_TransmitReceive(d->spi, &rx_dummy, &data[i], 1, 100);
    cs_high(d);
}

/* ======================================================================== */
/*  Public API                                                               */
/* ======================================================================== */

/*
 * CC1101 power-on / reset sequence.
 *
 * Because there is no hardware reset line, after an STM32 warm reset the
 * CC1101 may be in a locked SPI state (interrupted transaction, sleep, etc).
 * The recovery is:
 *   1. Pulse CS low → high with a finite wait (clears stuck SPI FSM)
 *   2. Send SRES multiple times (each strobe re-initialises the digital core)
 *   3. Wait for the chip to stabilise before any register access
 */
void CC1101_Reset(cc1101_t *d)
{
    /* Start with CS deasserted. */
    cs_high(d);

    /* Pulse CS low → high to clear any stuck SPI transaction.
       Minimum low time ~20 us, high time ~40 us per datasheet. */
    cs_low(d);
    {
        volatile uint32_t t = 160;  /* ~20 us @ 8 MHz */
        while (--t) __NOP();
    }
    cs_high(d);
    {
        volatile uint32_t t = 320;  /* ~40 us @ 8 MHz */
        while (--t) __NOP();
    }

    /* Send SRES repeatedly.  The first may be ignored if the SPI FSM is
       wedged; the second or third usually reset the chip. */
    for (uint8_t i = 0; i < 3; i++) {
        CC1101_WriteStrobe(d, CC1101_SRES);
        /* SRES takes ~150 us internally; wait 1 ms between retries. */
        volatile uint32_t t = 8000;  /* ~1 ms @ 8 MHz */
        while (--t) __NOP();
    }

    /* Allow the digital core to stabilise after SRES. */
    volatile uint32_t t = 8000;
    while (--t) __NOP();
}

/*
 * Initialise: reset, verify SPI, then load config.
 *
 * Order matches dsoldevila/CC1101 reference: reset → check VERSION →
 * only then write config registers.  This avoids corrupting the CC1101
 * with 47 garbage burst-write bytes if SPI is not yet reliable.
 */
uint8_t CC1101_Init(cc1101_t *d, cc1101_band_t band, cc1101_mod_t mod)
{
    if (!d || !d->spi) return 0;

    for (uint8_t attempt = 0; attempt < 3; attempt++) {
        /* Between retries, pulse CS to clear a stuck SPI state machine. */
        if (attempt > 0) {
            cs_low(d);
            { volatile uint32_t t = 160; while (--t) __NOP(); }
            cs_high(d);
            { volatile uint32_t t = 320; while (--t) __NOP(); }
        } else {
            cs_high(d);
        }

        CC1101_Reset(d);

        /* Verify SPI BEFORE any config writes — matches reference rf_begin(). */
        if (!CC1101_Check(d)) {
            HAL_Delay(5);
            continue;   /* try reset again */
        }

        /* SPI is confirmed working — now safe to write config. */
        CC1101_WriteStrobe(d, CC1101_SFRX);
        CC1101_WriteStrobe(d, CC1101_SFTX);

        const uint8_t *cfg;
        switch (mod) {
            case CC1101_MOD_GFSK_1_2KB:  cfg = cc1101_cfg_gfsk_1k2;  break;
            default:
            case CC1101_MOD_GFSK_38_4KB: cfg = cc1101_cfg_gfsk_38k4; break;
            case CC1101_MOD_GFSK_100KB:
            case CC1101_MOD_MSK_250KB:
            case CC1101_MOD_MSK_500KB:
            case CC1101_MOD_OOK_4_8KB:   cfg = cc1101_cfg_gfsk_38k4; break;
        }

        CC1101_WriteBurst(d, 0x00, cfg, CC1101_NUM_CFG_REGS);

        {
            uint32_t hz = band_centre[(band < CC1101_BAND_915) ? band : CC1101_BAND_868];
            CC1101_SetFrequency(d, hz);
        }

        CC1101_SetTxPower(d, -10);

        return 1;   /* Check already passed above */
    }

    return 0;
}

/*
 * Read VERSION register; expected value is 0x14.
 * Stores the last read value in cc1101_dbg_ver for diagnostics.
 * Returns 1 if version matches, 0 otherwise.
 */
uint8_t CC1101_Check(cc1101_t *d)
{
    cc1101_dbg_ver = 0;
    for (uint8_t i = 0; i < 3; i++) {
        cc1101_dbg_ver = CC1101_ReadReg(d, CC1101_VERSION);
        if (cc1101_dbg_ver == 0x14) return 1;
    }
    return 0;
}

/* ---- RF configuration ----------------------------------------------------- */

void CC1101_SetFrequency(cc1101_t *d, uint32_t hz)
{
    /* Clamp to valid range. */
    if (hz < 300000000UL) hz = 300000000UL;
    if (hz > 1000000000UL) hz = 1000000000UL;

    /* FREQ = hz * 2^16 / F_xtal */
    uint64_t freq_word = ((uint64_t)hz << 16) / CC1101_XTAL_HZ;
    uint32_t fw = (uint32_t)freq_word;

    CC1101_WriteReg(d, CC1101_FREQ2, (uint8_t)(fw >> 16));
    CC1101_WriteReg(d, CC1101_FREQ1, (uint8_t)(fw >> 8));
    CC1101_WriteReg(d, CC1101_FREQ0, (uint8_t)(fw & 0xFF));
}

void CC1101_SetChannel(cc1101_t *d, uint8_t ch)
{
    CC1101_WriteReg(d, CC1101_CHANNR, ch);
}

void CC1101_SetTxPower(cc1101_t *d, int8_t dbm)
{
    uint8_t idx;
    if      (dbm >= 10) idx = 7;
    else if (dbm >= 5)  idx = 6;
    else if (dbm >= 0)  idx = 5;
    else if (dbm >= -5) idx = 4;
    else if (dbm >= -10) idx = 3;
    else if (dbm >= -15) idx = 2;
    else if (dbm >= -20) idx = 1;
    else                 idx = 0;

    /* Write PATABLE entry (burst write to CC1101_PATABLE). */
    uint8_t pa = patable_433[idx];
    uint8_t hdr = CC1101_PATABLE | CC1101_WRITE | CC1101_BURST;
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, &hdr, &rx_dummy, 1, 100);
    HAL_SPI_TransmitReceive(d->spi, &pa, &rx_dummy, 1, 100);
    cs_high(d);

    /* FREND0 lower nibble selects PATABLE index. Keep at 0. */
    CC1101_WriteReg(d, CC1101_FREND0, 0x10);
}

/* ---- State strobes ------------------------------------------------------- */

void CC1101_SetRx(cc1101_t *d)
{
    CC1101_WriteStrobe(d, CC1101_SRX);
}

void CC1101_SetIdle(cc1101_t *d)
{
    CC1101_WriteStrobe(d, CC1101_SIDLE);
}

void CC1101_SetSleep(cc1101_t *d)
{
    CC1101_WriteStrobe(d, CC1101_SPWD);
}

void CC1101_FlushRx(cc1101_t *d)
{
    CC1101_WriteStrobe(d, CC1101_SIDLE);
    CC1101_WriteStrobe(d, CC1101_SFRX);
}

void CC1101_FlushTx(cc1101_t *d)
{
    CC1101_WriteStrobe(d, CC1101_SIDLE);
    CC1101_WriteStrobe(d, CC1101_SFTX);
}

/* ---- Packet I/O ----------------------------------------------------------- */

uint8_t CC1101_SendPacket(cc1101_t *d, const uint8_t *data, uint8_t len)
{
    if (!d || !data || len == 0 || len > 61) return 0;

    CC1101_FlushTx(d);

    /* Write len byte + payload to TX FIFO (burst). */
    uint8_t hdr = (uint8_t)(CC1101_TXFIFO | CC1101_WRITE | CC1101_BURST);
    cs_low(d);
    CC1101_WaitMiso(d);
    HAL_SPI_TransmitReceive(d->spi, &hdr, &rx_dummy, 1, 100);
    HAL_SPI_TransmitReceive(d->spi, &len, &rx_dummy, 1, 100);
    for (uint8_t i = 0; i < len; i++)
        HAL_SPI_TransmitReceive(d->spi, (uint8_t *)&data[i], &rx_dummy, 1, 100);
    cs_high(d);

    /* Strobe STX and poll for TX completion. */
    CC1101_WriteStrobe(d, CC1101_STX);

    uint32_t deadline = HAL_GetTick() + 100;  /* 100 ms cap */
    uint8_t state;
    do {
        state = CC1101_ReadReg(d, CC1101_MARCSTATE) & 0x1F;
        if (HAL_GetTick() > deadline) {
            CC1101_WriteStrobe(d, CC1101_SIDLE);
            CC1101_FlushTx(d);
            return 0;
        }
    } while (state != CC1101_STATE_IDLE);

    return 1;
}

uint8_t CC1101_ReceivePacket(cc1101_t *d, uint8_t *buf, uint8_t max_len, int8_t *rssi)
{
    if (!d || !buf || max_len == 0) return 0;

    uint8_t rxbytes = CC1101_ReadReg(d, CC1101_RXBYTES) & 0x7F;
    if (rxbytes == 0) return 0;

    /* Read length byte from RX FIFO. */
    uint8_t pktlen;
    CC1101_ReadBurst(d, CC1101_RXFIFO, &pktlen, 1);

    if (pktlen == 0 || pktlen > max_len) {
        CC1101_FlushRx(d);
        CC1101_SetRx(d);
        return 0;
    }

    /* Read payload bytes from RX FIFO. */
    CC1101_ReadBurst(d, CC1101_RXFIFO, buf, pktlen);

    /* The CC1101 may append RSSI and LQI bytes after the payload if
       PKTCTRL1.APPEND_STATUS is set.  We ignore those and read RSSI
       from the status register instead. */
    if (rssi) {
        uint8_t r = CC1101_ReadReg(d, CC1101_RSSI);
        int16_t dbm = (int16_t)r;
        if (dbm >= 128) dbm -= 256;
        *rssi = (int8_t)(dbm / 2 - 74);
    }

    CC1101_FlushRx(d);
    CC1101_SetRx(d);
    return pktlen;
}

/* ---- EXTI dispatch helpers ----------------------------------------------- */

void CC1101_HandleGdo0(cc1101_t *d, uint16_t pin)
{
    if (d && d->gdo0_pin == pin)
        d->rx_ready = 1;
}

void CC1101_HandleGdo2(cc1101_t *d, uint16_t pin)
{
    if (d && d->gdo2_pin == pin)
        d->sync_seen = 1;
}
