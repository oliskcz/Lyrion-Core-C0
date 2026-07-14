/**
 * @file  lyrion_link.h
 * @brief Lyrion Link — public API.
 *
 * The application calls ll_init() once at startup, registers callbacks
 * via ll_set_callbacks(), then calls ll_task() in the main loop. The
 * library handles CC1101 radio, AES-CCM encryption, SEQ deduplication,
 * and message dispatch.
 *
 * All state is RAM-only. After a reboot, the receiver's per-sender
 * duplicate-tracking table is empty, so the first packet from each
 * sender sets a new baseline. Cross-boot replay is not protected
 * (by design for v1).
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef LYRION_LINK_H
#define LYRION_LINK_H

#include "lyrion_link_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialise the Lyrion Link library.
 * @param  config  Configuration (address, network_id, radio settings).
 * @return LL_OK on success.
 */
ll_status_t ll_init(const ll_config_t *config);

/**
 * @brief  Set event callbacks. Pass NULL for events you don't care about.
 */
void ll_set_callbacks(const ll_callbacks_t *callbacks);

/**
 * @brief  Main loop tick. Call from the application's main while(1) loop.
 * Handles received packets, beacon timer, retransmits, CSMA backoff.
 */
void ll_task(void);

/**
 * @brief  Send a text message.
 * @param  dest  16-bit destination (0xFFFF = broadcast).
 * @param  text  UTF-8 text, max LL_MAX_PAYLOAD bytes for single-packet.
 * @param  len   Text length.
 * @param  ack   true to request an ACK, false for fire-and-forget.
 * @return LL_OK if queued, LL_ERR_BUSY if CSMA failed, etc.
 */
ll_status_t ll_send_text(uint16_t dest, const char *text, size_t len, bool ack);

/**
 * @brief  Send a JSON command to a node.
 * @param  dest     16-bit destination.
 * @param  json_cmd JSON command string, max LL_MAX_PAYLOAD bytes.
 * @param  req_id   Correlation ID (max 16 chars, or NULL).
 *                 The response will echo this ID.
 * @return LL_OK on success, error otherwise.
 */
ll_status_t ll_send_command(uint16_t dest, const char *json_cmd,
                            const char *req_id);

/**
 * @brief  Send a JSON response to a command (called by the application
 *         in response to an on_command_received callback).
 */
ll_status_t ll_send_response(uint16_t dest, const char *json_resp,
                             const char *req_id);

/**
 * @brief  Send a beacon packet (broadcast node info, JSON).
 */
ll_status_t ll_send_beacon(void);

/**
 * @brief  Send raw bytes (advanced). Builds a packet with the given type
 *         and payload, encrypts with AES-CCM using SEQ + nonce_ext.
 * @param  dest    Destination address.
 * @param  type    Message type (ll_type_t).
 * @param  payload Raw payload (will be encrypted).
 * @param  len     Payload length (0..LL_MAX_PAYLOAD).
 * @param  ack     true to request an ACK.
 * @return LL_OK on success, error otherwise.
 */
ll_status_t ll_send_raw(uint16_t dest, uint8_t type,
                        const uint8_t *payload, size_t len, bool ack);

#ifdef __cplusplus
}
#endif
#endif /* LYRION_LINK_H */
