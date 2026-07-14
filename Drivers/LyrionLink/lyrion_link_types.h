/**
 * @file  lyrion_link_types.h
 * @brief Lyrion Link — public types, message IDs, flags, error codes,
 *        callback typedefs.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef LYRION_LINK_TYPES_H
#define LYRION_LINK_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/*  Protocol version                                                    */
/* ------------------------------------------------------------------ */
#define LL_VERSION                0x01

/* ------------------------------------------------------------------ */
/*  Maximum packet size                                                 */
/* ------------------------------------------------------------------ */
/* 12 bytes clear header + 46 bytes payload + 4 bytes MAC = 62 bytes
 * (CC1101 64-byte FIFO has 1 byte for internal length, 1 byte spare). */
#define LL_HEADER_SIZE            12
#define LL_MAX_PAYLOAD            46
#define LL_MAC_SIZE                4
#define LL_NONCE_SIZE             13
#define LL_PREFIX_SIZE             7
#define LL_KEY_SIZE               16
#define LL_NETWORK_ID_SIZE         2
#define LL_ADDRESS_SIZE            2
#define LL_SEQ_SIZE                1
#define LL_NONCE_EXT_SIZE          3

/* ------------------------------------------------------------------ */
/*  Flags byte (clear header)                                           */
/* ------------------------------------------------------------------ */
#define LL_FLAG_ACK_REQUIRED      0x01  /* Sender wants an ACK */
#define LL_FLAG_STREAM            0x02  /* Streaming packet (voice etc.) */
#define LL_FLAG_RESERVED          0xFC  /* bits 2-7 reserved, must be 0 */

/* ------------------------------------------------------------------ */
/*  Message types                                                       */
/* ------------------------------------------------------------------ */
typedef enum {
    LL_TYPE_TEXT          = 0x01,   /* UTF-8 text message */
    LL_TYPE_COMMAND       = 0x02,   /* JSON command, app or built-in */
    LL_TYPE_RESPONSE      = 0x03,   /* JSON response to a command */
    LL_TYPE_BEACON        = 0x04,   /* JSON node info, periodic broadcast */
    LL_TYPE_ACK           = 0x05,   /* counter(4) + packetid(2) */
    LL_TYPE_NACK          = 0x06,   /* counter(4) + packetid(2) + reason(1) */
    LL_TYPE_FILE_META     = 0x10,   /* JSON file metadata */
    LL_TYPE_FILE_CHUNK    = 0x11,   /* chunk_index(2) + data */
    LL_TYPE_FILE_END      = 0x12,   /* JSON end-of-file */
    LL_TYPE_VOICE_FRAME   = 0x20,   /* reserved, codec TBD */
    LL_TYPE_PING          = 0xF0,   /* nonce(4) */
    LL_TYPE_PONG          = 0xF1,   /* echoed_nonce(4) + rssi(1) */
} ll_type_t;

/* ------------------------------------------------------------------ */
/*  Status codes                                                        */
/* ------------------------------------------------------------------ */
typedef enum {
    LL_OK                  =  0,
    LL_ERR_BUSY            = -1,  /* CSMA/CA failed after retries */
    LL_ERR_INVALID_PARAM   = -2,  /* bad argument */
    LL_ERR_NO_ROUTE        = -3,  /* dest not in address book */
    LL_ERR_BUFFER_FULL     = -4,  /* outgoing TX queue is full */
    LL_ERR_CRC             = -5,  /* CC1101 hardware CRC-16 failed */
    LL_ERR_MAC             = -6,  /* AES-CCM MAC verification failed */
    LL_ERR_DUPLICATE       = -7,  /* same SEQ (retransmit) */
    LL_ERR_TIMEOUT         = -8,  /* ACK timeout */
    LL_ERR_SHORT           = -9,  /* packet too short */
} ll_status_t;

/* ------------------------------------------------------------------ */
/*  Configuration structure                                             */
/* ------------------------------------------------------------------ */
typedef struct {
    uint16_t address;           /* Our 16-bit node address */
    uint16_t network_id;        /* 16-bit network identifier */
    uint8_t  band;              /* 0=315, 1=433, 2=868, 3=915 MHz */
    uint32_t channel_hz;        /* Frequency in Hz */
    uint8_t  data_rate_index;   /* CC1101 data rate index */
    uint8_t  output_power_index;/* CC1101 PATABLE index 0-7 */
    uint8_t  beacon_interval_s; /* 0 = disabled */
    bool     mesh_enabled;      /* Relay packets for other nodes (Pro) */
} ll_config_t;

/* ------------------------------------------------------------------ */
/*  Callback typedefs                                                  */
/* ------------------------------------------------------------------ */
typedef void (*ll_on_text_received_cb)(uint16_t src, const char *text,
                                       size_t len, int8_t rssi, uint8_t lqi);

typedef void (*ll_on_command_received_cb)(uint16_t src, const char *json_cmd,
                                          size_t len, const char *req_id);

typedef void (*ll_on_response_received_cb)(uint16_t src, const char *json_resp,
                                           size_t len);

typedef void (*ll_on_beacon_received_cb)(uint16_t src, const char *json_beacon,
                                         size_t len, int8_t rssi);

typedef void (*ll_on_send_complete_cb)(uint16_t dest, uint8_t type,
                                       ll_status_t status);

typedef void (*ll_on_file_meta_received_cb)(uint16_t src, const char *filename,
                                           size_t total_size,
                                           const uint8_t *sha256);

typedef void (*ll_on_file_complete_cb)(uint16_t src, const char *filename,
                                       const uint8_t *data, size_t len);

typedef struct {
    ll_on_text_received_cb     on_text_received;
    ll_on_command_received_cb  on_command_received;
    ll_on_response_received_cb on_response_received;
    ll_on_beacon_received_cb   on_beacon_received;
    ll_on_send_complete_cb     on_send_complete;
    ll_on_file_meta_received_cb on_file_meta_received;
    ll_on_file_complete_cb     on_file_complete;
} ll_callbacks_t;

#ifdef __cplusplus
}
#endif
#endif /* LYRION_LINK_TYPES_H */
