/**
 * @file  lyrion_link_packet.h
 * @brief Lyrion Link — on-the-air packet structure and pack/unpack API.
 *
 * Wire layout (12 bytes clear + N bytes payload + 4 bytes MAC = 62 max):
 *
 *   Offset Size  Field
 *   ------ ----- ---------------------------------------------------
 *   0      1     Version (LL_VERSION)
 *   1      1     Type (ll_type_t)
 *   2      1     HopCnt (Pro only)
 *   3      1     TTL (Pro only)
 *   4      1     Flags (LL_FLAG_*)
 *   5-6    2     Dest address (big-endian)
 *   7-8    2     Src address (big-endian)
 *   9      1     SEQ (big-endian; RAM only)
 *   10-12  3     NonceExt (big-endian; 24-bit runtime counter)
 *   ----  ----  (encrypted section follows)
 *   13..   N     ciphertext (N = 0..46)
 *   +N..   4     MAC (encrypted; 4 bytes)
 *
 * The 12-byte clear header is also the AAD for AES-CCM (authenticated
 * but not encrypted).
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#ifndef LYRION_LINK_PACKET_H
#define LYRION_LINK_PACKET_H

#include "lyrion_link_types.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t  version;
    uint8_t  type;
    uint8_t  hop_cnt;
    uint8_t  ttl;
    uint8_t  flags;
    uint16_t dest;
    uint16_t src;
    uint8_t  seq;          /* 8-bit protocol sequence number */
    uint8_t  nonce_ext[3]; /* 24-bit runtime counter for nonce uniqueness */
    const uint8_t *encrypted;  /* ciphertext || MAC (LL_MAX_PAYLOAD + 4 bytes) */
    uint8_t  encrypted_len;    /* 4..50 (MAC size to payload + MAC) */
} ll_packet_t;

/**
 * Pack a packet into a byte buffer for transmission.
 * @param  pkt   The packet to pack.
 * @param  out   Output buffer, must be at least LL_HEADER_SIZE + 50 bytes.
 * @return Total number of bytes written (header + encrypted section), or 0 on error.
 */
size_t ll_packet_pack(const ll_packet_t *pkt, uint8_t *out);

/**
 * Unpack bytes into a packet structure.
 * The encrypted section is NOT copied; pkt->encrypted points into `in`.
 * @param  in       Input buffer (from CC1101).
 * @param  in_len   Total input length (must be >= LL_HEADER_SIZE + LL_MAC_SIZE).
 * @param  pkt      Output packet structure.
 * @return 0 on success, -1 on error.
 */
int ll_packet_unpack(const uint8_t *in, size_t in_len, ll_packet_t *pkt);

/**
 * Extract the 12-byte AAD (clear header) for AES-CCM.
 * @param  pkt  The packet.
 * @param  aad  Output buffer, must be at least LL_HEADER_SIZE bytes.
 */
void ll_packet_get_aad(const ll_packet_t *pkt, uint8_t aad[LL_HEADER_SIZE]);

#ifdef __cplusplus
}
#endif
#endif /* LYRION_LINK_PACKET_H */
