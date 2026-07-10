/**
 * @file  cc1101.c
 * @brief C driver for the TI CC1101 sub-1 GHz RF transceiver (protocol logic).
 *
 * Faithful C port of the Arduino CC1101 library by Mateusz Furga. This file
 * contains only the chip protocol logic (register access, frequency/power
 * computation, packet TX/RX). All hardware-abstraction calls (SPI transfer,
 * GPIO CS/MISO, microsecond delay, EXTI callback attach/detach) are delegated
 * to the port layer in cc1101_port.c, keeping this file portable.
 *
 * @copyright Copyright (c) 2023 Mateusz Furga (original Arduino library, MIT)
 * @copyright SPDX-License-Identifier: MIT
 */

#include "cc1101.h"
#include "cc1101_port.h"

#include <string.h>

/* -------------------------------------------------------------------------- */
/*  Internal helpers                                                          */
/* -------------------------------------------------------------------------- */

#define CC1101_MIN(a, b) (((a) < (b)) ? (a) : (b))

/* Status registers span this address range (burst-on-read, read-only). */
#define CC1101_STATUS_REG_LO  CC1101_REG_PARTNUM         /* 0x30 */
#define CC1101_STATUS_REG_HI  CC1101_REG_RCCTRL0_STATUS  /* 0x3d */

static inline bool is_status_reg(uint8_t addr)
{
    return addr >= CC1101_STATUS_REG_LO && addr <= CC1101_STATUS_REG_HI;
}

static void set_gdo_config(cc1101_t *r, cc1101_gdo_pin_t pin, cc1101_gdo_config_t cfg)
{
    uint8_t reg = (pin == CC1101_GDO2) ? CC1101_REG_IOCFG2 : CC1101_REG_IOCFG0;
    cc1101_write_reg_field(r, reg, (uint8_t)cfg, 5, 0);
}

static uint16_t gdo_to_gpio(cc1101_t *r, cc1101_gdo_pin_t pin)
{
    return (pin == CC1101_GDO2) ? r->gdo2_pin : r->gdo0_pin;
}

static bool is_gdo_pin_configured(cc1101_t *r, cc1101_gdo_pin_t pin)
{
    return gdo_to_gpio(r, pin) != CC1101_PIN_UNUSED;
}

static void chip_select(cc1101_t *r)
{
    cc1101_gpio_write(r->cs_port, r->cs_pin, false);
}

static void chip_deselect(cc1101_t *r)
{
    cc1101_gpio_write(r->cs_port, r->cs_pin, true);
}

/* Wait until MISO goes low (chip ready). On STM32 the MISO pin is configured
 * as SPI alternate function but its input data register (IDR) is still
 * readable, so we can poll it directly. */
static void wait_ready(cc1101_t *r)
{
    while (cc1101_gpio_read(r->miso_port, r->miso_pin)) {
        /* spin */
    }
}

/* A single full-duplex SPI byte exchange. Returns the byte clocked in. */
static uint8_t spi_xfer(cc1101_t *r, uint8_t tx)
{
    uint8_t rx = 0;
    cc1101_spi_transfer(r->spi, &tx, &rx, 1);
    return rx;
}

static void save_status(cc1101_t *r, uint8_t status)
{
    r->currentState = (cc1101_state_t)((status >> 4) & 0x07);
}

static void send_cmd(cc1101_t *r, uint8_t addr)
{
    uint8_t header = (uint8_t)(CC1101_WRITE | (addr & 0x3F));

    chip_select(r);
    wait_ready(r);
    save_status(r, spi_xfer(r, header));
    chip_deselect(r);
}

static void hard_reset(cc1101_t *r)
{
    /* Manual power-up reset sequence per datasheet (Section 11.1). */
    chip_deselect(r);
    cc1101_delay_us(5);
    chip_select(r);
    cc1101_delay_us(5);
    chip_deselect(r);
    cc1101_delay_us(40);

    chip_select(r);
    wait_ready(r);
    save_status(r, spi_xfer(r, CC1101_CMD_RES));
    wait_ready(r);
    chip_deselect(r);
}

static void flush_rx_buffer(cc1101_t *r)
{
    if (r->currentState != CC1101_STATE_IDLE &&
        r->currentState != CC1101_STATE_RXFIFO_OVERFLOW) {
        return;
    }
    send_cmd(r, CC1101_CMD_FRX);
}

static void flush_tx_buffer(cc1101_t *r)
{
    if (r->currentState != CC1101_STATE_IDLE &&
        r->currentState != CC1101_STATE_TXFIFO_UNDERFLOW) {
        return;
    }
    send_cmd(r, CC1101_CMD_FTX);
}

static cc1101_state_t get_state(cc1101_t *r)
{
    send_cmd(r, CC1101_CMD_NOP);
    return r->currentState;
}

static void set_state(cc1101_t *r, cc1101_state_t state)
{
    switch (state) {
    case CC1101_STATE_IDLE:
        send_cmd(r, CC1101_CMD_IDLE);
        break;
    case CC1101_STATE_TX:
        send_cmd(r, CC1101_CMD_TX);
        break;
    case CC1101_STATE_RX:
        send_cmd(r, CC1101_CMD_RX);
        break;
    default:
        return;   /* not supported */
    }
    while (get_state(r) != state) {
        cc1101_delay_us(100);
    }
}

static void set_regs(cc1101_t *r)
{
    /* Automatically calibrate when going from IDLE to RX or TX. */
    cc1101_write_reg_field(r, CC1101_REG_MCSM0, 1, 5, 4);

    /* Return to IDLE after a packet is received (MCSM1.RXOFF_MODE = IDLE) */
    cc1101_write_reg_field(r, CC1101_REG_MCSM1, 0, 3, 2);

    /* Append the 2 status bytes (RSSI + LQI/CRC_OK) to every packet and never
     * auto-flush the RX FIFO on CRC error; both are assumptions receive()
     * relies on. */
    cc1101_write_reg_field(r, CC1101_REG_PKTCTRL1, 1, 2, 2);  /* APPEND_STATUS */
    cc1101_write_reg_field(r, CC1101_REG_PKTCTRL1, 0, 3, 3);  /* CRC_AUTOFLUSH */

    /* RX FIFO threshold = 40 bytes (TX FIFO threshold = 25 bytes) */
    cc1101_write_reg_field(r, CC1101_REG_FIFOTHR, 0x09, 3, 0);

    /* Disable data whitening. */
    cc1101_set_data_whitening(r, false);
}

/*
 * Read the NUM_*BYTES field of a FIFO byte-count status register repeatedly
 * until the same value is returned twice, per datasheet errata (SWRZ020E).
 */
static uint8_t read_fifo_byte_count(cc1101_t *r, uint8_t addr)
{
    uint8_t a = cc1101_read_reg_field(r, addr, 6, 0);
    uint8_t b;
    for (uint8_t i = 0; i < CC1101_FIFO_BYTES_MAX_READS; i++) {
        b = a;
        a = cc1101_read_reg_field(r, addr, 6, 0);
        if (a == b) {
            break;
        }
    }
    return a;
}

/*
 * Reliable RX FIFO overflow check. RXBYTES.RXFIFO_OVERFLOW is a single-bit
 * field, which the SPI read-synchronization errata lists as immune to
 * corruption (unlike the status-byte STATE field).
 */
static bool rx_fifo_overflowed(cc1101_t *r)
{
    return cc1101_read_reg_field(r, CC1101_REG_RXBYTES, 7, 7) != 0;
}

static bool tx_fifo_underflowed(cc1101_t *r)
{
    return cc1101_read_reg_field(r, CC1101_REG_TXBYTES, 7, 7) != 0;
}

static uint8_t wait_for_bytes_in_fifo(cc1101_t *r, uint8_t min_bytes)
{
    uint32_t start = cc1101_millis();
    while (true) {
        if (rx_fifo_overflowed(r)) {
            return 0;
        }
        uint8_t bytes_in_fifo = read_fifo_byte_count(r, CC1101_REG_RXBYTES);
        if (bytes_in_fifo >= min_bytes) {
            return bytes_in_fifo;
        }
        if (cc1101_millis() - start > CC1101_RECV_TIMEOUT_MS) {
            return 0;
        }
        cc1101_delay_us(15);
    }
}

static uint8_t wait_for_space_in_fifo(cc1101_t *r, uint8_t min_space)
{
    uint32_t start = cc1101_millis();
    while (true) {
        if (tx_fifo_underflowed(r)) {
            return 0;
        }
        uint8_t bytes_in_fifo = read_fifo_byte_count(r, CC1101_REG_TXBYTES);
        uint8_t space_in_fifo =
            (bytes_in_fifo >= CC1101_FIFO_SIZE) ? 0
                                                : (CC1101_FIFO_SIZE - bytes_in_fifo);
        if (space_in_fifo >= min_space) {
            return space_in_fifo;
        }
        if (cc1101_millis() - start > CC1101_XMIT_TIMEOUT_MS) {
            return 0;
        }
        cc1101_delay_us(15);
    }
}

static cc1101_status_t abort_receive(cc1101_t *r)
{
    bool overflow = rx_fifo_overflowed(r);
    set_state(r, CC1101_STATE_IDLE);
    flush_rx_buffer(r);
    return overflow ? CC1101_STATUS_RXFIFO_OVERFLOW : CC1101_STATUS_TIMEOUT;
}

static cc1101_status_t abort_transmit(cc1101_t *r)
{
    bool underflow = tx_fifo_underflowed(r);
    set_state(r, CC1101_STATE_IDLE);
    flush_tx_buffer(r);
    return underflow ? CC1101_STATUS_TXFIFO_UNDERFLOW : CC1101_STATUS_TIMEOUT;
}

/* -------------------------------------------------------------------------- */
/*  Public API                                                                 */
/* -------------------------------------------------------------------------- */

cc1101_status_t cc1101_init(cc1101_t *r, const cc1101_config_t *cfg,
                             cc1101_modulation_t mod, float freq, float drate)
{
    cc1101_status_t status;

    /* Copy hardware binding. */
    r->spi       = cfg->spi;
    r->cs_port   = cfg->cs_port;   r->cs_pin   = cfg->cs_pin;
    r->miso_port = cfg->miso_port; r->miso_pin = cfg->miso_pin;
    r->gdo0_port = cfg->gdo0_port; r->gdo0_pin = cfg->gdo0_pin;
    r->gdo2_port = cfg->gdo2_port; r->gdo2_pin = cfg->gdo2_pin;

    /* Reset internal state to defaults. */
    r->currentState    = CC1101_STATE_IDLE;
    r->mod             = CC1101_MOD_2FSK;
    r->pktLenMode      = CC1101_PKT_LEN_MODE_FIXED;
    r->addrFilterMode  = CC1101_ADDR_FILTER_MODE_NONE;
    r->freq            = 433.5f;
    r->drate           = 4.0f;
    r->power           = 0;
    r->pktLen          = 0;
    r->rssi            = 0;
    r->lqi             = 0;
    r->manchester      = false;
    r->fec             = false;
    r->transmitActionPin = CC1101_GDO0;
    r->receiveActionPin  = CC1101_GDO0;
    r->tx_action_gpio  = CC1101_PIN_UNUSED;
    r->rx_action_gpio  = CC1101_PIN_UNUSED;
    r->tx_action       = NULL;
    r->rx_action       = NULL;

    /* Register with the EXTI dispatcher. */
    cc1101_port_register(r);

    chip_deselect(r);

    hard_reset(r);
    cc1101_delay_ms(10);

    uint8_t partnum = cc1101_get_chip_part_number(r);
    uint8_t version = cc1101_get_chip_version(r);
    if (partnum != CC1101_PARTNUM ||
        (version != CC1101_VERSION && version != CC1101_VERSION_LEGACY)) {
        return CC1101_STATUS_CHIP_NOT_FOUND;
    }

    set_regs(r);
    cc1101_set_modulation(r, mod);

    if ((status = cc1101_set_frequency(r, freq)) != CC1101_STATUS_OK) {
        return status;
    }
    if ((status = cc1101_set_data_rate(r, drate)) != CC1101_STATUS_OK) {
        return status;
    }

    cc1101_set_output_power(r, 0);
    set_state(r, CC1101_STATE_IDLE);
    flush_rx_buffer(r);
    flush_tx_buffer(r);

    return CC1101_STATUS_OK;
}

void cc1101_set_modulation(cc1101_t *r, cc1101_modulation_t mod)
{
    r->mod = mod;
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG2, (uint8_t)mod, 6, 4);

    if (mod == CC1101_MOD_ASK_OOK) {
        cc1101_write_reg(r, CC1101_REG_AGCCTRL2, 0x07); /* MAGN_TARGET */
        cc1101_write_reg(r, CC1101_REG_AGCCTRL1, 0x00); /* AGC_LNA_PRIORITY=0 */
        cc1101_write_reg(r, CC1101_REG_AGCCTRL0, 0x91); /* 8 dB ASK boundary */
    } else {
        cc1101_write_reg(r, CC1101_REG_AGCCTRL2, 0x03); /* reset defaults */
        cc1101_write_reg(r, CC1101_REG_AGCCTRL1, 0x40);
        cc1101_write_reg(r, CC1101_REG_AGCCTRL0, 0x91);
    }

    cc1101_set_output_power(r, r->power);

    if (mod == CC1101_MOD_MSK || mod == CC1101_MOD_4FSK) {
        cc1101_set_manchester(r, false);
    }
}

cc1101_status_t cc1101_set_frequency(cc1101_t *r, float freq)
{
    if (!((freq >= 300.0f && freq <= 348.0f) ||
          (freq >= 387.0f && freq <= 464.0f) ||
          (freq >= 779.0f && freq <= 928.0f))) {
        return CC1101_STATUS_INVALID_PARAM;
    }

    r->freq = freq;
    set_state(r, CC1101_STATE_IDLE);

    uint32_t f = (uint32_t)((freq * 65536.0f) / (float)CC1101_CRYSTAL_FREQ);
    cc1101_write_reg(r, CC1101_REG_FREQ0, (uint8_t)(f & 0xff));
    cc1101_write_reg(r, CC1101_REG_FREQ1, (uint8_t)((f >> 8) & 0xff));
    cc1101_write_reg(r, CC1101_REG_FREQ2, (uint8_t)((f >> 16) & 0xff));

    cc1101_set_output_power(r, r->power);
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_set_frequency_deviation(cc1101_t *r, float dev)
{
    float xosc = (float)(CC1101_CRYSTAL_FREQ * 1000);

    float dev_min = (xosc / (float)(1UL << 17)) * (8 + 0) * 1.0f;
    float dev_max = (xosc / (float)(1UL << 17)) * (8 + 7) * (float)(1UL << 7);

    if (dev < dev_min || dev > dev_max) {
        return CC1101_STATUS_INVALID_PARAM;
    }

    uint8_t best_e = 0, best_m = 0;
    float diff = dev_max;

    for (uint8_t e = 0; e <= 7; e++) {
        for (uint8_t m = 0; m <= 7; m++) {
            float t = (xosc / (float)(1UL << 17)) * (float)(8 + m) * (float)(1UL << e);
            float d = dev - t;
            if (d < 0.0f) d = -d;
            if (d < diff) {
                diff = d;
                best_e = e;
                best_m = m;
            }
        }
    }

    cc1101_write_reg_field(r, CC1101_REG_DEVIATN, best_m, 2, 0);
    cc1101_write_reg_field(r, CC1101_REG_DEVIATN, best_e, 6, 4);
    return CC1101_STATUS_OK;
}

void cc1101_set_channel(cc1101_t *r, uint8_t ch)
{
    cc1101_write_reg(r, CC1101_REG_CHANNR, ch);
}

cc1101_status_t cc1101_set_channel_spacing(cc1101_t *r, float sp)
{
    float xosc = (float)(CC1101_CRYSTAL_FREQ * 1000);

    float sp_min = (xosc / (float)(1UL << 18)) * (256.0f + 0.0f) * 1.0f;
    float sp_max = (xosc / (float)(1UL << 18)) * (256.0f + 255.0f) * 8.0f;

    if (sp < sp_min || sp > sp_max) {
        return CC1101_STATUS_INVALID_PARAM;
    }

    uint8_t best_e = 0, best_m = 0;
    float diff = sp_max;

    for (uint8_t e = 0; e <= 3; e++) {
        for (uint16_t m = 0; m <= 255; m++) {
            float t = (xosc / (float)(1UL << 18)) * (256.0f + (float)m) * (float)(1UL << e);
            float d = sp - t;
            if (d < 0.0f) d = -d;
            if (d < diff) {
                diff = d;
                best_e = e;
                best_m = (uint8_t)m;
            }
        }
    }

    cc1101_write_reg(r, CC1101_REG_MDMCFG0, best_m);
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG1, best_e, 1, 0);
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_set_data_rate(cc1101_t *r, float drate)
{
    /* Allowed data-rate ranges per modulation (kBaud). */
    static const float range[8][2] = {
        [CC1101_MOD_2FSK]    = {  0.6f, 500.0f },
        [CC1101_MOD_GFSK]    = {  0.6f, 250.0f },
        [2]                  = {  0.0f,   0.0f },   /* gap */
        [CC1101_MOD_ASK_OOK] = {  0.6f, 250.0f },
        [CC1101_MOD_4FSK]    = {  0.6f, 300.0f },
        [5]                  = {  0.0f,   0.0f },   /* gap */
        [6]                  = {  0.0f,   0.0f },   /* gap */
        [CC1101_MOD_MSK]     = { 26.0f, 500.0f },
    };

    if (drate < range[r->mod][0] || drate > range[r->mod][1]) {
        return CC1101_STATUS_INVALID_PARAM;
    }

    r->drate = drate;

    /* Compute e (exponent) via integer floor(log2) — no math lib needed.
     *
     *   target = drate * 2^20 / xosc   (xosc = XTAL in kHz)
     *   e = floor(log2(target))
     */
    float xosc = (float)(CC1101_CRYSTAL_FREQ * 1000);
    float target_f = (drate * 1048576.0f) / xosc;
    uint32_t t = (uint32_t)target_f;
    uint8_t e = 0;
    while (t > 1) { t >>= 1; e++; }

    /* Compute m (mantissa) with round = plus 0.5, then truncate. */
    float m_f = drate * ((float)(1UL << (28 - e)) / xosc) - 256.0f;
    uint32_t m = (uint32_t)(m_f + 0.5f);

    if (m == 256) {
        m = 0;
        e++;
    }

    cc1101_write_reg_field(r, CC1101_REG_MDMCFG4, e, 3, 0);
    cc1101_write_reg(r, CC1101_REG_MDMCFG3, (uint8_t)m);
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_set_rx_bandwidth(cc1101_t *r, float bw)
{
    /*
     * CC1101 channel filter bandwidths [kHz] (26 MHz crystal):
     *   \ E  0     1     2     3
     *   M +----------------------
     *   0 | 812 | 406 | 203 | 102
     *   1 | 650 | 335 | 162 |  81
     *   2 | 541 | 270 | 135 |  68
     *   3 | 464 | 232 | 116 |  58
     */
    float bw_min = (float)(CC1101_CRYSTAL_FREQ * 1000) / (8.0f * (4 + 3) * (1 << 3));
    float bw_max = (float)(CC1101_CRYSTAL_FREQ * 1000) / (8.0f * (4 + 0) * (1 << 0));

    if (bw < bw_min || bw > bw_max) {
        return CC1101_STATUS_INVALID_PARAM;
    }

    uint8_t best_e = 0, best_m = 0;
    float diff = bw_max;

    for (uint8_t e = 0; e <= 3; e++) {
        for (uint8_t m = 0; m <= 3; m++) {
            float t = (float)(CC1101_CRYSTAL_FREQ * 1000) / (8.0f * (4 + m) * (float)(1 << e));
            float d = bw - t;
            if (d < 0.0f) d = -d;
            if (d < diff) {
                diff = d;
                best_e = e;
                best_m = m;
            }
        }
    }

    cc1101_write_reg_field(r, CC1101_REG_MDMCFG4, best_e, 7, 6);
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG4, best_m, 5, 4);
    return CC1101_STATUS_OK;
}

void cc1101_set_output_power(cc1101_t *r, int8_t power)
{
    /* PATABLE entries per frequency band and power index (from datasheet). */
    static const uint8_t powers[4][8] = {
        [0] = { 0x12, 0x0d, 0x1c, 0x34, 0x51, 0x85, 0xcb, 0xc2 }, /* 315 MHz */
        [1] = { 0x12, 0x0e, 0x1d, 0x34, 0x60, 0x84, 0xc8, 0xc0 }, /* 433 MHz */
        [2] = { 0x03, 0x0f, 0x1e, 0x27, 0x50, 0x81, 0xcb, 0xc2 }, /* 868 MHz */
        [3] = { 0x03, 0x0e, 0x1e, 0x27, 0x8e, 0xcd, 0xc7, 0xc0 }, /* 915 MHz */
    };

    uint8_t power_idx, freq_idx;

    if (r->freq <= 348.0f) {
        freq_idx = 0;
    } else if (r->freq <= 464.0f) {
        freq_idx = 1;
    } else if (r->freq <= 891.5f) {
        freq_idx = 2;
    } else {
        freq_idx = 3;
    }

    if (power <= -30)       power_idx = 0;
    else if (power <= -20)  power_idx = 1;
    else if (power <= -15)  power_idx = 2;
    else if (power <= -10)  power_idx = 3;
    else if (power <= 0)    power_idx = 4;
    else if (power <= 5)    power_idx = 5;
    else if (power <= 7)    power_idx = 6;
    else                    power_idx = 7;

    r->power = power;

    if (r->mod == CC1101_MOD_ASK_OOK) {
        /* No shaping: use only the first 2 PATABLE entries. */
        uint8_t data[2] = { 0x00, powers[freq_idx][power_idx] };
        cc1101_write_reg_burst(r, CC1101_REG_PATABLE, data, 2);
        cc1101_write_reg_field(r, CC1101_REG_FREND0, 1, 2, 0); /* PA_POWER = 1 */
    } else {
        cc1101_write_reg(r, CC1101_REG_PATABLE, powers[freq_idx][power_idx]);
        cc1101_write_reg_field(r, CC1101_REG_FREND0, 0, 2, 0); /* PA_POWER = 0 */
    }
}

cc1101_status_t cc1101_set_preamble_length(cc1101_t *r, uint8_t length)
{
    uint8_t data;
    switch (length) {
    case 16:  data = 0; break;
    case 24:  data = 1; break;
    case 32:  data = 2; break;
    case 48:  data = 3; break;
    case 64:  data = 4; break;
    case 96:  data = 5; break;
    case 128: data = 6; break;
    case 192: data = 7; break;
    default:  return CC1101_STATUS_INVALID_PARAM;
    }
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG1, data, 6, 4);
    return CC1101_STATUS_OK;
}

void cc1101_set_sync_word(cc1101_t *r, uint16_t sync)
{
    cc1101_write_reg(r, CC1101_REG_SYNC1, (uint8_t)(sync >> 8));
    cc1101_write_reg(r, CC1101_REG_SYNC0, (uint8_t)(sync & 0xff));
}

void cc1101_set_sync_mode(cc1101_t *r, cc1101_sync_mode_t mode)
{
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG2, (uint8_t)mode, 2, 0);
}

void cc1101_set_packet_length_mode(cc1101_t *r, cc1101_pkt_len_mode_t mode,
                                    uint8_t length)
{
    r->pktLenMode = mode;
    r->pktLen = length;

    cc1101_write_reg_field(r, CC1101_REG_PKTCTRL0, (uint8_t)mode, 1, 0);

    switch (mode) {
    case CC1101_PKT_LEN_MODE_FIXED:
        cc1101_write_reg(r, CC1101_REG_PKTLEN, length);
        break;
    case CC1101_PKT_LEN_MODE_VARIABLE:
        /* Indicates the maximum packet length allowed. */
        cc1101_write_reg(r, CC1101_REG_PKTLEN, length);
        break;
    }
}

void cc1101_set_address_filtering_mode(cc1101_t *r, cc1101_addr_filter_mode_t mode)
{
    r->addrFilterMode = mode;
    cc1101_write_reg_field(r, CC1101_REG_PKTCTRL1, (uint8_t)mode, 1, 0);
}

void cc1101_set_crc(cc1101_t *r, bool enable)
{
    cc1101_write_reg_field(r, CC1101_REG_PKTCTRL0, (uint8_t)enable, 2, 2);
}

void cc1101_set_data_whitening(cc1101_t *r, bool enable)
{
    cc1101_write_reg_field(r, CC1101_REG_PKTCTRL0, (uint8_t)enable, 6, 6);
}

cc1101_status_t cc1101_set_manchester(cc1101_t *r, bool enable)
{
    if (enable && (r->mod == CC1101_MOD_MSK || r->mod == CC1101_MOD_4FSK || r->fec)) {
        return CC1101_STATUS_BAD_STATE;
    }
    r->manchester = enable;
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG2, (uint8_t)enable, 3, 3);
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_set_fec(cc1101_t *r, bool enable)
{
    if (enable && (r->pktLenMode != CC1101_PKT_LEN_MODE_FIXED || r->manchester)) {
        return CC1101_STATUS_BAD_STATE;
    }
    r->fec = enable;
    cc1101_write_reg_field(r, CC1101_REG_MDMCFG1, (uint8_t)enable, 7, 7);
    return CC1101_STATUS_OK;
}

int8_t cc1101_get_rssi(cc1101_t *r)
{
    return ((int8_t)r->rssi / 2) - 74;
}

uint8_t cc1101_get_lqi(cc1101_t *r)
{
    return r->lqi;
}

/* -------------------------------------------------------------------------- */
/*  Blocking transmit / receive                                               */
/* -------------------------------------------------------------------------- */

cc1101_status_t cc1101_transmit(cc1101_t *r, const uint8_t *data, size_t length,
                                 uint8_t addr)
{
    size_t cur_pkt_len = length;

    if (r->addrFilterMode != CC1101_ADDR_FILTER_MODE_NONE) {
        cur_pkt_len++;
    }

    if (cur_pkt_len > 255) {
        return CC1101_STATUS_LENGTH_TOO_BIG;
    }

    if (r->pktLenMode == CC1101_PKT_LEN_MODE_FIXED) {
        if (cur_pkt_len < r->pktLen) {
            return CC1101_STATUS_LENGTH_TOO_SMALL;
        }
        if (cur_pkt_len > r->pktLen) {
            return CC1101_STATUS_LENGTH_TOO_BIG;
        }
    }

    set_state(r, CC1101_STATE_IDLE);
    flush_tx_buffer(r);

    uint8_t header_bytes = 0;

    if (r->pktLenMode == CC1101_PKT_LEN_MODE_VARIABLE) {
        cc1101_write_reg(r, CC1101_REG_FIFO, (uint8_t)cur_pkt_len);
        header_bytes++;
    }

    if (r->addrFilterMode != CC1101_ADDR_FILTER_MODE_NONE) {
        cc1101_write_reg(r, CC1101_REG_FIFO, addr);
        header_bytes++;
    }

    /* Fill the FIFO with the first chunk of payload and start transmitting,
     * then keep topping it up as the chip drains it. */
    uint8_t first_chunk = CC1101_MIN((uint8_t)length,
                                      (uint8_t)(CC1101_FIFO_SIZE - header_bytes));
    cc1101_write_reg_burst(r, CC1101_REG_FIFO, data, first_chunk);
    size_t data_written = first_chunk;

    set_state(r, CC1101_STATE_TX);

    while (data_written < length) {
        uint8_t space_in_fifo = wait_for_space_in_fifo(r, 1);
        if (space_in_fifo == 0) {
            return abort_transmit(r);
        }
        uint8_t bytes_to_write = CC1101_MIN((uint8_t)(length - data_written), space_in_fifo);
        cc1101_write_reg_burst(r, CC1101_REG_FIFO, data + data_written, bytes_to_write);
        data_written += bytes_to_write;
    }

    /* Whole packet is in the FIFO; wait for the chip to transmit it and return
     * to IDLE on its own (MCSM1.TXOFF_MODE = IDLE). */
    uint32_t start = cc1101_millis();
    while (get_state(r) != CC1101_STATE_IDLE) {
        if (tx_fifo_underflowed(r) || cc1101_millis() - start > CC1101_XMIT_TIMEOUT_MS) {
            return abort_transmit(r);
        }
        cc1101_delay_us(50);
    }
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_receive(cc1101_t *r, uint8_t *data, size_t length,
                                size_t *read, uint8_t addr)
{
    cc1101_status_t status = cc1101_start_receive(r, addr);
    if (status != CC1101_STATUS_OK) {
        return status;
    }
    return cc1101_read_data(r, data, length, read);
}

cc1101_status_t cc1101_read_data(cc1101_t *r, uint8_t *data, size_t length,
                                  size_t *read)
{
    if (read != NULL) {
        *read = 0;
    }

    if (length > 255) {
        return CC1101_STATUS_LENGTH_TOO_BIG;
    }

    uint8_t header_bytes = 0;
    uint8_t data_length = r->pktLen;

    if (r->pktLenMode == CC1101_PKT_LEN_MODE_VARIABLE) {
        if (wait_for_bytes_in_fifo(r, 1) == 0) {
            return abort_receive(r);
        }
        data_length = cc1101_read_reg(r, CC1101_REG_FIFO);
        header_bytes++;
    }

    if (r->addrFilterMode != CC1101_ADDR_FILTER_MODE_NONE && data_length > 0) {
        if (wait_for_bytes_in_fifo(r, 1) == 0) {
            return abort_receive(r);
        }
        (void)cc1101_read_reg(r, CC1101_REG_FIFO);
        header_bytes++;
        data_length--;
    }

    if (data_length > length) {
        set_state(r, CC1101_STATE_IDLE);
        flush_rx_buffer(r);
        return CC1101_STATUS_LENGTH_TOO_SMALL;
    }

    /* For packets < 64 bytes it is recommended to wait until the complete
     * packet has been received before reading it out of the RX FIFO. Include
     * the 2 appended status bytes (RSSI + CRC_OK|LQI) in the count. */
    uint16_t full_packet = (uint16_t)data_length + 2;
    if (full_packet <= (uint16_t)(CC1101_FIFO_SIZE - header_bytes)) {
        if (wait_for_bytes_in_fifo(r, (uint8_t)full_packet) == 0) {
            return abort_receive(r);
        }
    }

    uint8_t data_read = 0;
    while (data_read < data_length) {
        uint8_t remaining = data_length - data_read;

        uint8_t bytes_in_fifo = wait_for_bytes_in_fifo(r, 2);
        if (bytes_in_fifo == 0) {
            return abort_receive(r);
        }

        /* Per the datasheet the RX FIFO must never be emptied before the last
         * byte of the packet has been received, otherwise the last read byte
         * may be duplicated. Keep one byte back until the whole packet
         * (payload + 2 appended status bytes) is in the FIFO. */
        bool full_packet_in_fifo =
            (uint16_t)bytes_in_fifo >= (uint16_t)remaining + 2;
        uint8_t readable = full_packet_in_fifo ? bytes_in_fifo
                                                : (uint8_t)(bytes_in_fifo - 1);
        uint8_t bytes_to_read = CC1101_MIN(remaining, readable);

        cc1101_read_reg_burst(r, CC1101_REG_FIFO, data + data_read, bytes_to_read);
        data_read += bytes_to_read;
    }

    if (wait_for_bytes_in_fifo(r, 2) == 0) {
        return abort_receive(r);
    }

    r->rssi = cc1101_read_reg(r, CC1101_REG_FIFO);
    uint8_t v = cc1101_read_reg(r, CC1101_REG_FIFO);
    r->lqi = v & 0x7f;
    bool crc_ok = (v >> 7) & 1;

    set_state(r, CC1101_STATE_IDLE);
    flush_rx_buffer(r);

    if (read != NULL) {
        *read = data_length;
    }
    return crc_ok ? CC1101_STATUS_OK : CC1101_STATUS_CRC_MISMATCH;
}

/* -------------------------------------------------------------------------- */
/*  Non-blocking transmit                                                     */
/* -------------------------------------------------------------------------- */

cc1101_status_t cc1101_start_transmit(cc1101_t *r, const uint8_t *data,
                                       size_t length, uint8_t addr)
{
    size_t cur_pkt_len = length;

    if (r->addrFilterMode != CC1101_ADDR_FILTER_MODE_NONE) {
        cur_pkt_len++;
    }

    if (cur_pkt_len > 255) {
        return CC1101_STATUS_LENGTH_TOO_BIG;
    }

    if (r->pktLenMode == CC1101_PKT_LEN_MODE_FIXED) {
        if (cur_pkt_len < r->pktLen) {
            return CC1101_STATUS_LENGTH_TOO_SMALL;
        }
        if (cur_pkt_len > r->pktLen) {
            return CC1101_STATUS_LENGTH_TOO_BIG;
        }
    }

    /* The whole packet (length byte + address byte + payload) must fit in the
     * TX FIFO; a non-blocking transmit cannot top the FIFO up. Use the blocking
     * transmit() for longer payloads. */
    size_t fifo_bytes = cur_pkt_len +
                        (r->pktLenMode == CC1101_PKT_LEN_MODE_VARIABLE ? 1 : 0);
    if (fifo_bytes > CC1101_FIFO_SIZE) {
        return CC1101_STATUS_LENGTH_TOO_BIG;
    }

    set_state(r, CC1101_STATE_IDLE);
    flush_tx_buffer(r);

    if (r->pktLenMode == CC1101_PKT_LEN_MODE_VARIABLE) {
        cc1101_write_reg(r, CC1101_REG_FIFO, (uint8_t)cur_pkt_len);
    }
    if (r->addrFilterMode != CC1101_ADDR_FILTER_MODE_NONE) {
        cc1101_write_reg(r, CC1101_REG_FIFO, addr);
    }

    cc1101_write_reg_burst(r, CC1101_REG_FIFO, data, length);

    if (r->receiveActionPin != r->transmitActionPin &&
        is_gdo_pin_configured(r, r->receiveActionPin)) {
        set_gdo_config(r, r->receiveActionPin, CC1101_GDO_CFG_CONSTANT_LOW);
    }
    if (is_gdo_pin_configured(r, r->transmitActionPin)) {
        set_gdo_config(r, r->transmitActionPin, CC1101_GDO_CFG_SYNC_WORD);
    }

    set_state(r, CC1101_STATE_TX);
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_set_transmit_action(cc1101_t *r, void (*func)(void),
                                            cc1101_gdo_pin_t pin)
{
    if (!is_gdo_pin_configured(r, pin)) {
        return CC1101_STATUS_INVALID_PARAM;
    }
    /* Check if the sync word is disabled. */
    if ((cc1101_read_reg_field(r, CC1101_REG_MDMCFG2, 2, 0) & 0x03) == 0) {
        return CC1101_STATUS_BAD_STATE;
    }
    r->transmitActionPin = pin;
    set_gdo_config(r, pin, CC1101_GDO_CFG_SYNC_WORD);

    r->tx_action = func;
    r->tx_action_gpio = gdo_to_gpio(r, pin);
    cc1101_port_attach_interrupt(r, r->tx_action_gpio, false); /* falling edge */
    return CC1101_STATUS_OK;
}

void cc1101_clear_transmit_action(cc1101_t *r)
{
    if (!is_gdo_pin_configured(r, r->transmitActionPin)) {
        return;
    }
    cc1101_port_detach_interrupt(r, r->tx_action_gpio);
    r->tx_action = NULL;
    r->tx_action_gpio = CC1101_PIN_UNUSED;
}

cc1101_status_t cc1101_finish_transmit(cc1101_t *r)
{
    bool underflow = tx_fifo_underflowed(r);
    set_state(r, CC1101_STATE_IDLE);
    flush_tx_buffer(r);
    return underflow ? CC1101_STATUS_TXFIFO_UNDERFLOW : CC1101_STATUS_OK;
}

/* -------------------------------------------------------------------------- */
/*  Non-blocking receive                                                      */
/* -------------------------------------------------------------------------- */

cc1101_status_t cc1101_start_receive(cc1101_t *r, uint8_t addr)
{
    cc1101_write_reg(r, CC1101_REG_ADDR, addr);

    set_state(r, CC1101_STATE_IDLE);
    flush_rx_buffer(r);

    if (r->transmitActionPin != r->receiveActionPin &&
        is_gdo_pin_configured(r, r->transmitActionPin)) {
        set_gdo_config(r, r->transmitActionPin, CC1101_GDO_CFG_CONSTANT_LOW);
    }
    if (is_gdo_pin_configured(r, r->receiveActionPin)) {
        set_gdo_config(r, r->receiveActionPin, CC1101_GDO_CFG_RX_FIFO_THR);
    }

    set_state(r, CC1101_STATE_RX);
    return CC1101_STATUS_OK;
}

cc1101_status_t cc1101_set_receive_action(cc1101_t *r, void (*func)(void),
                                           cc1101_gdo_pin_t pin)
{
    if (!is_gdo_pin_configured(r, pin)) {
        return CC1101_STATUS_INVALID_PARAM;
    }
    r->receiveActionPin = pin;
    set_gdo_config(r, pin, CC1101_GDO_CFG_RX_FIFO_THR);

    r->rx_action = func;
    r->rx_action_gpio = gdo_to_gpio(r, pin);
    cc1101_port_attach_interrupt(r, r->rx_action_gpio, true); /* rising edge */
    return CC1101_STATUS_OK;
}

void cc1101_clear_receive_action(cc1101_t *r)
{
    if (!is_gdo_pin_configured(r, r->receiveActionPin)) {
        return;
    }
    cc1101_port_detach_interrupt(r, r->rx_action_gpio);
    r->rx_action = NULL;
    r->rx_action_gpio = CC1101_PIN_UNUSED;
}

/* -------------------------------------------------------------------------- */
/*  Chip identification                                                       */
/* -------------------------------------------------------------------------- */

uint8_t cc1101_get_chip_part_number(cc1101_t *r)
{
    return cc1101_read_reg(r, CC1101_REG_PARTNUM);
}

uint8_t cc1101_get_chip_version(cc1101_t *r)
{
    return cc1101_read_reg(r, CC1101_REG_VERSION);
}

/* -------------------------------------------------------------------------- */
/*  Direct register access                                                    */
/* -------------------------------------------------------------------------- */

uint8_t cc1101_read_reg_field(cc1101_t *r, uint8_t addr, uint8_t hi, uint8_t lo)
{
    return (cc1101_read_reg(r, addr) >> lo) & ((1 << (hi - lo + 1)) - 1);
}

uint8_t cc1101_read_reg(cc1101_t *r, uint8_t addr)
{
    uint8_t header = (uint8_t)(CC1101_READ | (addr & 0x3F));

    if (is_status_reg(addr)) {
        header |= CC1101_BURST;   /* status registers: burst bit on read */
    }

    chip_select(r);
    wait_ready(r);

    save_status(r, spi_xfer(r, header));
    uint8_t data = spi_xfer(r, 0x00);

    chip_deselect(r);
    return data;
}

void cc1101_read_reg_burst(cc1101_t *r, uint8_t addr, uint8_t *buff, size_t size)
{
    if (is_status_reg(addr)) {
        return;   /* status registers cannot be burst-read */
    }

    uint8_t header = (uint8_t)(CC1101_READ | CC1101_BURST | (addr & 0x3F));

    chip_select(r);
    wait_ready(r);

    save_status(r, spi_xfer(r, header));
    for (size_t i = 0; i < size; i++) {
        buff[i] = spi_xfer(r, 0x00);
    }

    chip_deselect(r);
}

void cc1101_write_reg_field(cc1101_t *r, uint8_t addr, uint8_t data,
                             uint8_t hi, uint8_t lo)
{
    data <<= lo;
    uint8_t current = cc1101_read_reg(r, addr);
    uint8_t mask = (uint8_t)(((1 << (hi - lo + 1)) - 1) << lo);
    data = (uint8_t)((current & ~mask) | (data & mask));
    cc1101_write_reg(r, addr, data);
}

void cc1101_write_reg(cc1101_t *r, uint8_t addr, uint8_t data)
{
    if (is_status_reg(addr)) {
        return;   /* status registers are read-only */
    }

    uint8_t header = (uint8_t)(CC1101_WRITE | (addr & 0x3F));

    chip_select(r);
    wait_ready(r);

    save_status(r, spi_xfer(r, header));
    save_status(r, spi_xfer(r, data));

    chip_deselect(r);
}

void cc1101_write_reg_burst(cc1101_t *r, uint8_t addr, const uint8_t *data,
                             size_t size)
{
    if (is_status_reg(addr)) {
        return;   /* status registers are read-only */
    }

    uint8_t header = (uint8_t)(CC1101_WRITE | CC1101_BURST | (addr & 0x3F));

    chip_select(r);
    wait_ready(r);

    save_status(r, spi_xfer(r, header));
    for (size_t i = 0; i < size; i++) {
        save_status(r, spi_xfer(r, data[i]));
    }

    chip_deselect(r);
}
