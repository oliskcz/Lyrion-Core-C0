/**
 * @file  aes128.h
 * @brief AES-128 encrypt and decrypt core (no mode wrappers).
 *
 * Based on the Tiny-AES-c project (https://github.com/kokke/tiny-AES-c,
 * CC0 / public domain). Both forward (encrypt) and inverse (decrypt)
 * ciphers are provided. CCM only uses the forward direction, but the
 * inverse is included for general-purpose use.
 *
 * The code is pure C with byte-level operations and compiles unchanged on
 * ARM (STM32) and AVR (ATmega328P), both little-endian.
 *
 * @copyright CC0 / public domain (original: kokke/tiny-AES-c)
 */
#ifndef AES128_H
#define AES128_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** AES-128 key schedule context. Holds 11 round keys (176 bytes).
 *  Created once by aes128_init() and reused for every block. */
typedef struct {
    uint8_t round_key[176];  /* 11 * 16 bytes */
} aes128_ctx_t;

/** Expand a 16-byte key into the 11 round keys stored in @p ctx. */
void aes128_init(aes128_ctx_t *ctx, const uint8_t key[16]);

/** Encrypt one 16-byte block. @p in and @p out may alias. */
void aes128_encrypt_block(const aes128_ctx_t *ctx,
                           const uint8_t in[16], uint8_t out[16]);

/** Decrypt one 16-byte block. @p in and @p out may alias. */
void aes128_decrypt_block(const aes128_ctx_t *ctx,
                           const uint8_t in[16], uint8_t out[16]);

#ifdef __cplusplus
}
#endif
#endif /* AES128_H */
