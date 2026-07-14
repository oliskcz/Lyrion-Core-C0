/**
 * @file  ccm.c
 * @brief AES-CCM mode implementation (encode + decode).
 *
 * Fixed parameters: L=2, M=4, 13-byte nonce, optional AAD.
 * Only uses aes128_encrypt_block() — CCM never needs AES decrypt.
 *
 * AAD: when aad_len > 0, the adata bit is set in B0 and the AAD is
 * processed in the CBC-MAC per RFC 3610 §2.2:
 *   B0 || encoded(aad_len) || AAD || padded(AAD) || P1 || ... || Pn
 * where encoded(aad_len) is a 2-byte big-endian length field.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "ccm.h"
#include <string.h>

#define BLK 16  /* AES block size */

/* B0 flags: adata=0 | M'=(M-2)/2=1 << 3 | L-1=1  =>  0x09
 *           adata=1 | M'=1              | L-1=1  =>  0x49 */
#define B0_FLAGS_NO_AAD  0x09
#define B0_FLAGS_AAD     0x49
/* CTR flags: L-1=1  =>  0x01 */
#define A_FLAGS          0x01

/* ------------------------------------------------------------------ */
/*  Block builders                                                     */
/* ------------------------------------------------------------------ */

static void build_b0(uint8_t *blk, uint8_t flags,
                     const uint8_t nonce[13], size_t pt_len)
{
    blk[0] = flags;
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
/*  CBC-MAC over B0 || AAD || plaintext (RFC 3610)                      */
/* ------------------------------------------------------------------ */

static void cbc_mac(aes128_ctx_t *ctx, uint8_t mac[BLK],
                    uint8_t b0_flags, const uint8_t nonce[13],
                    const uint8_t *aad, size_t aad_len,
                    const uint8_t *plaintext, size_t pt_len)
{
    uint8_t blk[BLK];

    /* 1. B0 */
    build_b0(blk, b0_flags, nonce, pt_len);
    for (int i = 0; i < BLK; i++) mac[i] = blk[i];
    aes128_encrypt_block(ctx, mac, mac);

    /* 2. AAD length (2 bytes big-endian) — only when AAD present */
    if (aad_len > 0)
    {
        memset(blk, 0, BLK);
        blk[0] = (uint8_t)(aad_len >> 8);
        blk[1] = (uint8_t)(aad_len & 0xFF);
        for (int i = 0; i < BLK; i++) mac[i] ^= blk[i];
        aes128_encrypt_block(ctx, mac, mac);

        /* 3. AAD itself, zero-padded last block */
        size_t off = 0;
        while (off < aad_len)
        {
            size_t chunk = aad_len - off;
            if (chunk > BLK) chunk = BLK;
            memset(blk, 0, BLK);
            memcpy(blk, aad + off, chunk);
            for (int i = 0; i < BLK; i++) mac[i] ^= blk[i];
            aes128_encrypt_block(ctx, mac, mac);
            off += chunk;
        }
    }

    /* 4. Plaintext, zero-padded last block */
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
}

/* ------------------------------------------------------------------ */
/*  CCM encode                                                         */
/* ------------------------------------------------------------------ */

size_t ccm_encode(aes128_ctx_t *ctx, const uint8_t nonce[13],
                  const uint8_t *aad, size_t aad_len,
                  const uint8_t *plaintext, size_t pt_len,
                  uint8_t *out)
{
    if (pt_len > 65535 || aad_len > CCM_AAD_MAX)
        return 0;

    uint8_t blk[BLK];
    uint8_t mac[BLK];
    uint8_t b0_flags = (aad_len > 0) ? B0_FLAGS_AAD : B0_FLAGS_NO_AAD;

    /* --- CBC-MAC over B0 || AAD || plaintext --- */
    cbc_mac(ctx, mac, b0_flags, nonce, aad, aad_len, plaintext, pt_len);
    /* MAC T = mac[0..3] */

    /* --- CTR encrypt the MAC: U = T XOR S0[0..3] --- */
    build_ctr(blk, nonce, 0);
    aes128_encrypt_block(ctx, blk, blk);        /* blk = S0 */
    uint8_t u[CCM_MAC_SIZE];
    for (int i = 0; i < CCM_MAC_SIZE; i++)
        u[i] = mac[i] ^ blk[i];

    /* --- CTR encrypt the plaintext --- */
    size_t off = 0;
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
               const uint8_t *aad, size_t aad_len,
               const uint8_t *ciphertext, size_t ct_len,
               uint8_t *out)
{
    if (ct_len < CCM_MAC_SIZE || aad_len > CCM_AAD_MAX)
        return -1;

    size_t pt_len = ct_len - CCM_MAC_SIZE;
    uint8_t blk[BLK];
    uint8_t mac[BLK];
    uint8_t b0_flags = (aad_len > 0) ? B0_FLAGS_AAD : B0_FLAGS_NO_AAD;

    /* --- CTR decrypt + recover MAC T from U --- */
    build_ctr(blk, nonce, 0);
    aes128_encrypt_block(ctx, blk, blk);        /* blk = S0 */
    uint8_t t[CCM_MAC_SIZE];
    for (int i = 0; i < CCM_MAC_SIZE; i++)
        t[i] = ciphertext[pt_len + i] ^ blk[i];

    /* --- CTR decrypt ciphertext -> plaintext --- */
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

    /* --- Recompute CBC-MAC over B0 || AAD || plaintext --- */
    cbc_mac(ctx, mac, b0_flags, nonce, aad, aad_len, out, pt_len);

    /* Verify MAC */
    if (memcmp(mac, t, CCM_MAC_SIZE) != 0)
        return -1;

    return (int)pt_len;
}
