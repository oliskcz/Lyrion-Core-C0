/**
 * @file  ccm.c
 * @brief AES-CCM mode implementation (encode + decode).
 *
 * Fixed parameters: L=2, M=4, 13-byte nonce, no AAD.
 * Only uses aes128_encrypt_block() — CCM never needs AES decrypt.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "ccm.h"
#include <string.h>

#define BLK 16  /* AES block size */

/* B0 flags: adata=0 | M'=(M-2)/2=1 << 3 | L-1=1  =>  0x09 */
#define B0_FLAGS  0x09
/* CTR flags: L-1=1  =>  0x01 */
#define A_FLAGS   0x01

/* ------------------------------------------------------------------ */
/*  Block builders                                                     */
/* ------------------------------------------------------------------ */

static void build_b0(uint8_t *blk, const uint8_t nonce[13], size_t pt_len)
{
    blk[0] = B0_FLAGS;
    memcpy(blk + 1, nonce, 13);
    blk[14] = (uint8_t)(pt_len >> 8);
    blk[15] = (uint8_t)(pt_len & 0xFF);
}

static void build_ctr(uint8_t *blk, const uint8_t nonce[13], uint16_t ctr)
{
    blk[0] = A_FLAGS;
    memcpy(blk + 1, nonce, 13);
    blk[14] = (uint8_t)(ctr >> 8);
    blk[15] = (uint8_t)(ctr & 0xFF);
}

/* ------------------------------------------------------------------ */
/*  CCM encode                                                         */
/* ------------------------------------------------------------------ */

size_t ccm_encode(aes128_ctx_t *ctx, const uint8_t nonce[13],
                  const uint8_t *plaintext, size_t pt_len,
                  uint8_t *out)
{
    if (pt_len > 65535)
        return 0;

    uint8_t blk[BLK];
    uint8_t mac[BLK];

    /* --- CBC-MAC over B0 || plaintext (zero-padded blocks) --- */
    build_b0(blk, nonce, pt_len);
    aes128_encrypt_block(ctx, blk, mac);

    size_t off = 0;
    while (off < pt_len)
    {
        size_t chunk = pt_len - off;
        if (chunk > BLK) chunk = BLK;
        memset(blk, 0, BLK);
        memcpy(blk, plaintext + off, chunk);
        for (int i = 0; i < BLK; i++) mac[i] ^= blk[i];
        aes128_encrypt_block(ctx, mac, mac);
        off += chunk;
    }
    /* MAC T = mac[0..3] */

    /* --- CTR encrypt the MAC: U = T XOR S0[0..3] --- */
    build_ctr(blk, nonce, 0);
    aes128_encrypt_block(ctx, blk, blk);        /* blk = S0 */
    uint8_t u[CCM_MAC_SIZE];
    for (int i = 0; i < CCM_MAC_SIZE; i++)
        u[i] = mac[i] ^ blk[i];

    /* --- CTR encrypt the plaintext --- */
    off = 0;
    uint16_t ctr = 1;
    while (off < pt_len)
    {
        build_ctr(blk, nonce, ctr);
        aes128_encrypt_block(ctx, blk, blk);    /* blk = S_ctr */
        size_t chunk = pt_len - off;
        if (chunk > BLK) chunk = BLK;
        for (size_t i = 0; i < chunk; i++)
            out[off + i] = plaintext[off + i] ^ blk[i];
        off += chunk;
        ctr++;
    }

    /* Append encrypted MAC */
    memcpy(out + pt_len, u, CCM_MAC_SIZE);
    return pt_len + CCM_MAC_SIZE;
}

/* ------------------------------------------------------------------ */
/*  CCM decode                                                         */
/* ------------------------------------------------------------------ */

int ccm_decode(aes128_ctx_t *ctx, const uint8_t nonce[13],
               const uint8_t *ciphertext, size_t ct_len,
               uint8_t *out)
{
    if (ct_len < CCM_MAC_SIZE)
        return -1;

    size_t pt_len = ct_len - CCM_MAC_SIZE;
    uint8_t blk[BLK];
    uint8_t mac[BLK];

    /* --- CTR decrypt + recover MAC T from U --- */
    build_ctr(blk, nonce, 0);
    aes128_encrypt_block(ctx, blk, blk);        /* blk = S0 */
    uint8_t t[CCM_MAC_SIZE];
    for (int i = 0; i < CCM_MAC_SIZE; i++)
        t[i] = ciphertext[pt_len + i] ^ blk[i];

    /* CTR decrypt ciphertext -> plaintext */
    size_t off = 0;
    uint16_t ctr = 1;
    while (off < pt_len)
    {
        build_ctr(blk, nonce, ctr);
        aes128_encrypt_block(ctx, blk, blk);
        size_t chunk = pt_len - off;
        if (chunk > BLK) chunk = BLK;
        for (size_t i = 0; i < chunk; i++)
            out[off + i] = ciphertext[off + i] ^ blk[i];
        off += chunk;
        ctr++;
    }

    /* --- Recompute CBC-MAC over B0 || plaintext --- */
    build_b0(blk, nonce, pt_len);
    aes128_encrypt_block(ctx, blk, mac);

    off = 0;
    while (off < pt_len)
    {
        size_t chunk = pt_len - off;
        if (chunk > BLK) chunk = BLK;
        memset(blk, 0, BLK);
        memcpy(blk, out + off, chunk);
        for (int i = 0; i < BLK; i++) mac[i] ^= blk[i];
        aes128_encrypt_block(ctx, mac, mac);
        off += chunk;
    }

    /* Verify MAC */
    if (memcmp(mac, t, CCM_MAC_SIZE) != 0)
        return -1;

    return (int)pt_len;
}
