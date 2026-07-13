/**
 * @file  ccm.h
 * @brief AES-CCM (Counter with CBC-MAC) mode — encrypt + authenticate.
 *
 * RFC 3610 style CCM with these fixed parameters:
 *   L = 2   (2-byte length field, messages up to 65535 bytes)
 *   M = 4   (4-byte MAC tag)
 *   Nonce = 13 bytes (9-byte static prefix + 4-byte packet counter)
 *   AAD = none
 *
 * Only requires the AES-128 *encrypt* direction (used for both CBC-MAC
 * and CTR). Works with the aes128_encrypt-only core.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef CCM_H
#define CCM_H

#include "aes128.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CCM_MAC_SIZE      4    /* bytes */
#define CCM_NONCE_SIZE    13   /* 9-byte prefix + 4-byte counter */
#define CCM_COUNTER_SIZE  4    /* bytes sent in clear per packet */
#define CCM_PREFIX_SIZE   9    /* static nonce prefix from key.h */

/**
 * @brief  Encrypt + authenticate (CCM encode).
 * @param  ctx       AES-128 context (initialised by aes128_init).
 * @param  nonce     13-byte CCM nonce.
 * @param  plaintext Input data.
 * @param  pt_len    Plaintext length (0..65535).
 * @param  out       Output buffer: ciphertext || encrypted-MAC.
 *                   Must hold at least pt_len + CCM_MAC_SIZE bytes.
 * @retval Total output length (pt_len + 4), or 0 on error (pt_len > 65535).
 */
size_t ccm_encode(aes128_ctx_t *ctx, const uint8_t nonce[13],
                  const uint8_t *plaintext, size_t pt_len,
                  uint8_t *out);

/**
 * @brief  Decrypt + verify MAC (CCM decode).
 * @param  ctx        AES-128 context.
 * @param  nonce      13-byte CCM nonce.
 * @param  ciphertext Input: ciphertext || encrypted-MAC.
 * @param  ct_len     Total input length (must be >= CCM_MAC_SIZE).
 * @param  out        Output buffer for decrypted plaintext.
 *                    Must hold at least ct_len - CCM_MAC_SIZE bytes.
 * @retval Plaintext length on success, -1 on MAC failure or short input.
 */
int ccm_decode(aes128_ctx_t *ctx, const uint8_t nonce[13],
               const uint8_t *ciphertext, size_t ct_len,
               uint8_t *out);

#ifdef __cplusplus
}
#endif
#endif /* CCM_H */
