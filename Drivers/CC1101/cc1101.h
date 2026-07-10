/**
 * @file  cc1101.h
 * @brief C driver for the Texas Instruments CC1101 sub-1 GHz RF transceiver.
 *
 * Port of the Arduino CC1101 library by Mateusz Furga
 * (https://github.com/mfurga/CC1101) to C for STM32 HAL.
 *
 * The original C++ `Radio` class is replaced by a `cc1101_t` instance struct
 * passed as the first argument to every function, enabling multiple independent
 * radios on a shared or separate SPI buses. The full public API and behaviour
 * of the original library is preserved.
 *
 * Hardware-abstraction glue (SPI transfer, GPIO, delay, EXTI callbacks) lives
 * in @ref cc1101_port.h / cc1101_port.c so the protocol logic in this file
 * stays portable.
 *
 * @copyright Copyright (c) 2023 Mateusz Furga (original Arduino library, MIT)
 * @copyright SPDX-License-Identifier: MIT
 */

#ifndef CC1101_H
#define CC1101_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================== */
/*  Constants                                                                 */
/* ========================================================================== */

#define CC1101_PIN_UNUSED       0xFFU   /* marks an unconnected GDO pin       */

#define CC1101_SPI_MAX_FREQ     6500000UL   /* 6.5 MHz max SPI clock         */
#define CC1101_SPI_TIMEOUT_MS   100         /* HAL_SPI transmit/receive to   */

#define CC1101_FIFO_SIZE        64      /* 64 B TX/RX FIFO                    */
#ifndef CC1101_CRYSTAL_FREQ
#define CC1101_CRYSTAL_FREQ     26      /* 26 MHz reference crystal (MHz)     */
#endif

#ifndef CC1101_RECV_TIMEOUT_MS
#define CC1101_RECV_TIMEOUT_MS  250     /* 250 ms (default 5 s; shortened for OLED loop) */
#endif
#ifndef CC1101_XMIT_TIMEOUT_MS
#define CC1101_XMIT_TIMEOUT_MS  5000    /* 5 s blocking transmit timeout      */
#endif
#ifndef CC1101_FIFO_BYTES_MAX_READS
#define CC1101_FIFO_BYTES_MAX_READS 8    /* errata RX/TXBYTES re-read cap     */
#endif

#define CC1101_WRITE            0x00U
#define CC1101_READ             0x80U
#define CC1101_BURST            0x40U

#define CC1101_PARTNUM          0x00U
#define CC1101_VERSION          0x14U
#define CC1101_VERSION_LEGACY   0x04U

/* --- Configuration registers ---------------------------------------------- */
#define CC1101_REG_IOCFG2       0x00  /* GDO2 output pin configuration */
#define CC1101_REG_IOCFG1       0x01  /* GDO1 output pin configuration */
#define CC1101_REG_IOCFG0       0x02  /* GDO0 output pin configuration */
#define CC1101_REG_FIFOTHR      0x03  /* RX FIFO and TX FIFO thresholds */
#define CC1101_REG_SYNC1        0x04  /* Sync word, high byte */
#define CC1101_REG_SYNC0        0x05  /* Sync word, low byte */
#define CC1101_REG_PKTLEN       0x06  /* Packet length */
#define CC1101_REG_PKTCTRL1     0x07  /* Packet automation control */
#define CC1101_REG_PKTCTRL0     0x08  /* Packet automation control */
#define CC1101_REG_ADDR         0x09  /* Device address */
#define CC1101_REG_CHANNR       0x0a  /* Channel number */
#define CC1101_REG_FSCTRL1      0x0b  /* Frequency synthesizer control */
#define CC1101_REG_FSCTRL0      0x0c  /* Frequency synthesizer control */
#define CC1101_REG_FREQ2        0x0d  /* Frequency control word, high byte */
#define CC1101_REG_FREQ1        0x0e  /* Frequency control word, middle byte */
#define CC1101_REG_FREQ0        0x0f  /* Frequency control word, low byte */
#define CC1101_REG_MDMCFG4      0x10  /* Modem configuration */
#define CC1101_REG_MDMCFG3      0x11  /* Modem configuration */
#define CC1101_REG_MDMCFG2      0x12  /* Modem configuration */
#define CC1101_REG_MDMCFG1      0x13  /* Modem configuration */
#define CC1101_REG_MDMCFG0      0x14  /* Modem configuration */
#define CC1101_REG_DEVIATN      0x15  /* Modem deviation setting */
#define CC1101_REG_MCSM2        0x16  /* Main radio control state machine config */
#define CC1101_REG_MCSM1        0x17  /* Main radio control state machine config */
#define CC1101_REG_MCSM0        0x18  /* Main radio control state machine config */
#define CC1101_REG_FOCCFG       0x19  /* Frequency offset compensation config */
#define CC1101_REG_BSCFG        0x1a  /* Bit synchronization configuration */
#define CC1101_REG_AGCCTRL2     0x1b  /* AGC control */
#define CC1101_REG_AGCCTRL1     0x1c  /* AGC control */
#define CC1101_REG_AGCCTRL0     0x1d  /* AGC control */
#define CC1101_REG_WOREVT1      0x1e  /* WOR event timeout, high byte */
#define CC1101_REG_WOREVT0      0x1f  /* WOR event timeout, low byte */
#define CC1101_REG_WORCTRL      0x20  /* Wake on radio control */
#define CC1101_REG_FREND1       0x21  /* Front end RX configuration */
#define CC1101_REG_FREND0       0x22  /* Front end TX configuration */
#define CC1101_REG_FSCAL3       0x23  /* Frequency synthesizer calibration */
#define CC1101_REG_FSCAL2       0x24  /* Frequency synthesizer calibration */
#define CC1101_REG_FSCAL1       0x25  /* Frequency synthesizer calibration */
#define CC1101_REG_FSCAL0       0x26  /* Frequency synthesizer calibration */
#define CC1101_REG_RCCTRL1      0x27  /* RC oscillator configuration */
#define CC1101_REG_RCCTRL0      0x28  /* RC oscillator configuration */
#define CC1101_REG_FSTEST       0x29  /* Frequency synthesizer calibration control */
#define CC1101_REG_PTEST        0x2a  /* Production test */
#define CC1101_REG_AGCTEST      0x2b  /* AGC test */
#define CC1101_REG_TEST2        0x2c  /* Various test settings */
#define CC1101_REG_TEST1        0x2d  /* Various test settings */
#define CC1101_REG_TEST0        0x2e  /* Various test settings */

#define CC1101_REG_PATABLE      0x3e  /* PA power control */
#define CC1101_REG_FIFO         0x3f  /* TX and RX FIFO */

/* --- Command strobes ------------------------------------------------------ */
#define CC1101_CMD_RES          0x30  /* Reset chip */
#define CC1101_CMD_FSTXON       0x31  /* Enable and calibrate freq synthesizer */
#define CC1101_CMD_XOFF         0x32  /* Turn off crystal oscillator */
#define CC1101_CMD_CAL          0x33  /* Calibrate freq synthesizer and turn off */
#define CC1101_CMD_RX           0x34  /* Enable RX */
#define CC1101_CMD_TX           0x35  /* Enable TX */
#define CC1101_CMD_IDLE         0x36  /* Enable IDLE */
#define CC1101_CMD_WOR          0x38  /* Start automatic RX polling (WOR) */
#define CC1101_CMD_PWD          0x39  /* Enter power down mode */
#define CC1101_CMD_FRX          0x3a  /* Flush the RX FIFO buffer */
#define CC1101_CMD_FTX          0x3b  /* Flush the TX FIFO buffer */
#define CC1101_CMD_WORRST       0x3c  /* Reset WOR timer */
#define CC1101_CMD_NOP          0x3d  /* No operation */

/* --- Status registers (read-only, burst bit on read) ---------------------- */
#define CC1101_REG_PARTNUM          0x30  /* Chip part number */
#define CC1101_REG_VERSION          0x31  /* Chip version number */
#define CC1101_REG_FREQEST          0x32  /* Frequency offset estimate */
#define CC1101_REG_LQI              0x33  /* Link quality estimate */
#define CC1101_REG_RSSI             0x34  /* Received signal strength */
#define CC1101_REG_MARCSTATE        0x35  /* Main radio control state machine */
#define CC1101_REG_WORTIME1         0x36  /* WOR time, high byte */
#define CC1101_REG_WORTIME0         0x37  /* WOR time, low byte */
#define CC1101_REG_PKTSTATUS        0x38  /* Current GDOx status + packet status */
#define CC1101_REG_VCO_VC_DAC       0x39  /* Current setting from PLL calibration */
#define CC1101_REG_TXBYTES          0x3a  /* Underflow + bytes in TX FIFO */
#define CC1101_REG_RXBYTES          0x3b  /* Overflow + bytes in RX FIFO */
#define CC1101_REG_RCCTRL1_STATUS   0x3c  /* Last RC oscillator calibration result */
#define CC1101_REG_RCCTRL0_STATUS   0x3d  /* Last RC oscillator calibration result */

/* ========================================================================== */
/*  Enumerations (mirror the Arduino enum values exactly)                    */
/* ========================================================================== */

/** Operation status returned by most API functions. */
typedef enum {
    CC1101_STATUS_OK = 0,
    CC1101_STATUS_INVALID_PARAM,
    CC1101_STATUS_CHIP_NOT_FOUND,
    CC1101_STATUS_BAD_STATE,
    CC1101_STATUS_LENGTH_TOO_SMALL,
    CC1101_STATUS_LENGTH_TOO_BIG,
    CC1101_STATUS_CRC_MISMATCH,
    CC1101_STATUS_TXFIFO_UNDERFLOW,
    CC1101_STATUS_RXFIFO_OVERFLOW,
    CC1101_STATUS_TIMEOUT
} cc1101_status_t;

/** Main radio control state machine state (MARCSTATE[6:4]). */
typedef enum {
    CC1101_STATE_IDLE             = 0,
    CC1101_STATE_RX               = 1,
    CC1101_STATE_TX               = 2,
    CC1101_STATE_FSTXON           = 3,
    CC1101_STATE_CALIBRATE        = 4,
    CC1101_STATE_SETTLING         = 5,
    CC1101_STATE_RXFIFO_OVERFLOW  = 6,
    CC1101_STATE_TXFIFO_UNDERFLOW = 7,
} cc1101_state_t;

/** Modulation scheme (MDMCFG2.MOD_FORMAT). */
typedef enum {
    CC1101_MOD_2FSK    = 0,
    CC1101_MOD_GFSK    = 1,
    CC1101_MOD_ASK_OOK = 3,
    CC1101_MOD_4FSK    = 4,
    CC1101_MOD_MSK     = 7
} cc1101_modulation_t;

/** Sync word detection mode (MDMCFG2.SYNC_MODE). */
typedef enum {
    CC1101_SYNC_MODE_NO_PREAMBLE    = 0,
    CC1101_SYNC_MODE_15_16          = 1,
    CC1101_SYNC_MODE_16_16          = 2,
    CC1101_SYNC_MODE_30_32          = 3,
    CC1101_SYNC_MODE_NO_PREAMBLE_CS = 4,
    CC1101_SYNC_MODE_15_16_CS       = 5,
    CC1101_SYNC_MODE_16_16_CS       = 6,
    CC1101_SYNC_MODE_30_32_CS       = 7,
} cc1101_sync_mode_t;

/** Packet length handling (PKTCTRL0.LENGTH_CONFIG). */
typedef enum {
    CC1101_PKT_LEN_MODE_FIXED    = 0,
    CC1101_PKT_LEN_MODE_VARIABLE = 1,
} cc1101_pkt_len_mode_t;

/** Address filtering mode (PKTCTRL1.ADR_CHK). */
typedef enum {
    CC1101_ADDR_FILTER_MODE_NONE          = 0,
    CC1101_ADDR_FILTER_MODE_CHECK         = 1,
    CC1101_ADDR_FILTER_MODE_CHECK_BC_0    = 2,
    CC1101_ADDR_FILTER_MODE_CHECK_BC_0_255 = 3
} cc1101_addr_filter_mode_t;

/** Selects which GDO pin an action is attached to. */
typedef enum {
    CC1101_GDO0 = 0,
    CC1101_GDO2 = 2,
} cc1101_gdo_pin_t;

/** GDO output signal configurations (IOCFGx.GDO_CFG). */
typedef enum {
    CC1101_GDO_CFG_RX_FIFO_THR  = 0x01, /* RX FIFO >= threshold or end of packet */
    CC1101_GDO_CFG_SYNC_WORD    = 0x06, /* asserts on sync, de-asserts at EOP */
    CC1101_GDO_CFG_HIGH_Z       = 0x2e, /* high impedance (3-state) */
    CC1101_GDO_CFG_CONSTANT_LOW = 0x2f, /* hardwired to 0 (no edges) */
} cc1101_gdo_config_t;

/* ========================================================================== */
/*  Types                                                                      */
/* ========================================================================== */

/* Forward declaration of HAL SPI handle. The STM32 HAL defines this as
 * `typedef struct __SPI_HandleTypeDef { ... } SPI_HandleTypeDef;` (with a
 * struct tag), so the forward declaration below is compatible. GPIO port
 * pointers are stored as `void *` because the CMSIS `GPIO_TypeDef` is an
 * anonymous-struct typedef that cannot be forward-declared; the port layer
 * (cc1101_port.c) casts them back to `GPIO_TypeDef *`. This keeps cc1101.h
 * free of any vendor-specific header. */
struct __SPI_HandleTypeDef;
typedef struct __SPI_HandleTypeDef SPI_HandleTypeDef;

/**
 * @brief Hardware binding for a CC1101 radio instance.
 *
 * Populated once and passed to cc1101_init(). All pins are STM32 GPIO pin
 * numbers (GPIO_PIN_x) and ports. Set @c gdo0_pin / @c gdo2_pin to
 * @ref CC1101_PIN_UNUSED if the corresponding GDO line is not connected.
 *
 * @note The @c *_port fields are opaque pointers (void *). Pass the board's
 *       `GPIOx` macros (e.g. @c GPIOA, @c GPIOB) — the port layer casts them
 *       to the real `GPIO_TypeDef *`.
 */
typedef struct {
    SPI_HandleTypeDef *spi;       /**< SPI peripheral handle (e.g. &hspi1)   */
    void   *cs_port;              /**< Chip-select GPIO port (e.g. GPIOA)     */
    uint16_t cs_pin;              /**< Chip-select GPIO pin                   */
    void   *miso_port;            /**< MISO GPIO port (for waitReady polling) */
    uint16_t miso_pin;            /**< MISO GPIO pin                          */
    void   *gdo0_port;            /**< GDO0 GPIO port (or NULL if unused)     */
    uint16_t gdo0_pin;            /**< GDO0 GPIO pin (or CC1101_PIN_UNUSED)   */
    void   *gdo2_port;            /**< GDO2 GPIO port (or NULL if unused)     */
    uint16_t gdo2_pin;            /**< GDO2 GPIO pin (or CC1101_PIN_UNUSED)   */
} cc1101_config_t;

/**
 * @brief A CC1101 radio instance (replaces the C++ `Radio` class).
 *
 * One struct per physical radio. All API functions take a pointer to this
 * struct as their first argument.
 */
typedef struct {
    /* hardware binding */
    SPI_HandleTypeDef *spi;
    void   *cs_port;              uint16_t cs_pin;
    void   *miso_port;            uint16_t miso_pin;
    void   *gdo0_port;            uint16_t gdo0_pin;
    void   *gdo2_port;            uint16_t gdo2_pin;

    /* runtime state */
    cc1101_state_t          currentState;
    cc1101_modulation_t     mod;
    cc1101_pkt_len_mode_t   pktLenMode;
    cc1101_addr_filter_mode_t addrFilterMode;
    float                   freq;
    float                   drate;
    int8_t                  power;
    uint8_t                 pktLen;
    uint8_t                 rssi;
    uint8_t                 lqi;
    bool                    manchester;
    bool                    fec;

    /* interrupt-action bookkeeping (set by set_*_action) */
    cc1101_gdo_pin_t transmitActionPin;
    cc1101_gdo_pin_t receiveActionPin;
    uint16_t         tx_action_gpio;   /**< resolved GPIO pin for TX dispatch */
    uint16_t         rx_action_gpio;   /**< resolved GPIO pin for RX dispatch */
    void           (*tx_action)(void); /**< user TX-complete callback         */
    void           (*rx_action)(void); /**< user RX-ready callback            */
    bool                    registered;/**< present in the port registry       */
} cc1101_t;

/* ========================================================================== */
/*  Public API                                                                 */
/* ========================================================================== */

/**
 * @brief  Initialise the radio (hardware reset, chip detect, default config).
 * @param  r       Radio instance.
 * @param  cfg      Hardware binding (SPI, CS, MISO, GDO0/GDO2 pins).
 * @param  mod     Modulation (default CC1101_MOD_ASK_OOK).
 * @param  freq    Frequency in MHz (default 433.5).
 * @param  drate   Data rate in kBaud (default 4.0).
 * @retval CC1101_STATUS_OK on success, CC1101_STATUS_CHIP_NOT_FOUND if the
 *         part/version does not match, or a status from setFrequency/setDataRate.
 */
cc1101_status_t cc1101_init(cc1101_t *r, const cc1101_config_t *cfg,
                             cc1101_modulation_t mod, float freq, float drate);

uint8_t cc1101_get_chip_part_number(cc1101_t *r);
uint8_t cc1101_get_chip_version(cc1101_t *r);

/* --- Radio configuration -------------------------------------------------- */
void cc1101_set_modulation(cc1101_t *r, cc1101_modulation_t mod);
cc1101_status_t cc1101_set_frequency(cc1101_t *r, float freq);
cc1101_status_t cc1101_set_frequency_deviation(cc1101_t *r, float dev);
void cc1101_set_channel(cc1101_t *r, uint8_t ch);
cc1101_status_t cc1101_set_channel_spacing(cc1101_t *r, float sp);
cc1101_status_t cc1101_set_data_rate(cc1101_t *r, float drate);
cc1101_status_t cc1101_set_rx_bandwidth(cc1101_t *r, float bw);
void cc1101_set_output_power(cc1101_t *r, int8_t power);

/* --- Packet format ------------------------------------------------------- */
void cc1101_set_crc(cc1101_t *r, bool enable);
void cc1101_set_data_whitening(cc1101_t *r, bool enable);
cc1101_status_t cc1101_set_manchester(cc1101_t *r, bool enable);
cc1101_status_t cc1101_set_fec(cc1101_t *r, bool enable);
void cc1101_set_address_filtering_mode(cc1101_t *r, cc1101_addr_filter_mode_t mode);
void cc1101_set_packet_length_mode(cc1101_t *r, cc1101_pkt_len_mode_t mode,
                                    uint8_t length);
void cc1101_set_sync_mode(cc1101_t *r, cc1101_sync_mode_t mode);
cc1101_status_t cc1101_set_preamble_length(cc1101_t *r, uint8_t length);
void cc1101_set_sync_word(cc1101_t *r, uint16_t sync);

/* --- Blocking transmit / receive ----------------------------------------- */
cc1101_status_t cc1101_transmit(cc1101_t *r, const uint8_t *data, size_t length,
                                 uint8_t addr);
cc1101_status_t cc1101_receive(cc1101_t *r, uint8_t *data, size_t length,
                                size_t *read, uint8_t addr);

/* --- Non-blocking transmit ----------------------------------------------- */
cc1101_status_t cc1101_start_transmit(cc1101_t *r, const uint8_t *data,
                                       size_t length, uint8_t addr);
cc1101_status_t cc1101_set_transmit_action(cc1101_t *r, void (*func)(void),
                                            cc1101_gdo_pin_t pin);
void cc1101_clear_transmit_action(cc1101_t *r);
cc1101_status_t cc1101_finish_transmit(cc1101_t *r);

/* --- Non-blocking receive ------------------------------------------------ */
cc1101_status_t cc1101_start_receive(cc1101_t *r, uint8_t addr);
cc1101_status_t cc1101_set_receive_action(cc1101_t *r, void (*func)(void),
                                           cc1101_gdo_pin_t pin);
void cc1101_clear_receive_action(cc1101_t *r);
cc1101_status_t cc1101_read_data(cc1101_t *r, uint8_t *data, size_t length,
                                  size_t *read);

/* --- Packet quality metrics ---------------------------------------------- */
int8_t  cc1101_get_rssi(cc1101_t *r);
uint8_t cc1101_get_lqi(cc1101_t *r);

/* --- Direct register access (use with care) ------------------------------ */
uint8_t cc1101_read_reg(cc1101_t *r, uint8_t addr);
uint8_t cc1101_read_reg_field(cc1101_t *r, uint8_t addr, uint8_t hi, uint8_t lo);
void    cc1101_read_reg_burst(cc1101_t *r, uint8_t addr, uint8_t *buff, size_t size);
void    cc1101_write_reg(cc1101_t *r, uint8_t addr, uint8_t data);
void    cc1101_write_reg_field(cc1101_t *r, uint8_t addr, uint8_t data,
                                uint8_t hi, uint8_t lo);
void    cc1101_write_reg_burst(cc1101_t *r, uint8_t addr, const uint8_t *data,
                                size_t size);

#ifdef __cplusplus
}
#endif

#endif /* CC1101_H */
