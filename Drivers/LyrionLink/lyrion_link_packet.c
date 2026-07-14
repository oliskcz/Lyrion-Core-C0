/**
 * @file  lyrion_link_packet.c
 * @brief Packet pack/unpack and AAD extraction.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "lyrion_link_packet.h"
#include <string.h>

size_t ll_packet_pack(const ll_packet_t *pkt, uint8_t *out)
{
    if (pkt->encrypted_len < LL_MAC_SIZE ||
        pkt->encrypted_len > LL_MAC_SIZE + LL_MAX_PAYLOAD) {
        return 0;
    }

    uint8_t *p = out;

    *p++ = pkt->version;
    *p++ = pkt->type;
    *p++ = pkt->hop_cnt;
    *p++ = pkt->ttl;
    *p++ = pkt->flags;
    *p++ = (uint8_t)(pkt->dest >> 8);
    *p++ = (uint8_t)(pkt->dest & 0xFF);
    *p++ = (uint8_t)(pkt->src >> 8);
    *p++ = (uint8_t)(pkt->src & 0xFF);
    *p++ = pkt->seq;
    *p++ = pkt->nonce_ext[0];
    *p++ = pkt->nonce_ext[1];
    *p++ = pkt->nonce_ext[2];

    memcpy(p, pkt->encrypted, pkt->encrypted_len);
    p += pkt->encrypted_len;

    return (size_t)(p - out);
}

int ll_packet_unpack(const uint8_t *in, size_t in_len, ll_packet_t *pkt)
{
    if (in_len < LL_HEADER_SIZE + LL_MAC_SIZE) return -1;
    if (in_len >  LL_HEADER_SIZE + LL_MAX_PAYLOAD + LL_MAC_SIZE) return -1;

    const uint8_t *p = in;

    pkt->version   = *p++;
    pkt->type      = *p++;
    pkt->hop_cnt   = *p++;
    pkt->ttl       = *p++;
    pkt->flags     = *p++;
    pkt->dest      = (uint16_t)((p[0] << 8) | p[1]); p += 2;
    pkt->src       = (uint16_t)((p[0] << 8) | p[1]); p += 2;
    pkt->seq       = *p++;
    pkt->nonce_ext[0] = *p++;
    pkt->nonce_ext[1] = *p++;
    pkt->nonce_ext[2] = *p++;

    pkt->encrypted     = p;
    pkt->encrypted_len = (uint8_t)(in_len - LL_HEADER_SIZE);

    return 0;
}

void ll_packet_get_aad(const ll_packet_t *pkt, uint8_t aad[LL_HEADER_SIZE])
{
    uint8_t *p = aad;
    *p++ = pkt->version;
    *p++ = pkt->type;
    *p++ = pkt->hop_cnt;
    *p++ = pkt->ttl;
    *p++ = pkt->flags;
    *p++ = (uint8_t)(pkt->dest >> 8);
    *p++ = (uint8_t)(pkt->dest & 0xFF);
    *p++ = (uint8_t)(pkt->src >> 8);
    *p++ = (uint8_t)(pkt->src & 0xFF);
    *p++ = pkt->seq;
    *p++ = pkt->nonce_ext[0];
    *p++ = pkt->nonce_ext[1];
    *p++ = pkt->nonce_ext[2];
}
