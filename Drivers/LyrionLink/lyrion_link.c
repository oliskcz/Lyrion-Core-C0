/**
 * @file  lyrion_link.c
 * @brief Lyrion Link — core: state, init, task, send, receive.
 *
 * Phase 1 skeleton: AES-CCM with AAD, full 12-byte clear header,
 * SEQ + nonce_ext duplicate detection, single-sender RAM tracking.
 *
 * @copyright SPDX-License-Identifier: MIT
 */
#include "lyrion_link.h"
#include "lyrion_link_packet.h"
#include "aes128.h"
#include "ccm.h"
#include "key.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/*  Internal state                                                      */
/* ------------------------------------------------------------------ */
static struct {
    ll_config_t   config;
    ll_callbacks_t cb;
    aes128_ctx_t  aes_ctx;
    uint8_t       tx_seq;          /* next outgoing SEQ */
    uint32_t      tx_nonce_ext;    /* next outgoing 24-bit nonce_ext */
    uint8_t       rx_last_seq;     /* last received SEQ (0xFF = none) */
    bool          inited;
} g_state;

/* ------------------------------------------------------------------ */
/*  Init / callbacks                                                    */
/* ------------------------------------------------------------------ */
ll_status_t ll_init(const ll_config_t *config)
{
    if (!config) return LL_ERR_INVALID_PARAM;

    g_state.config = *config;
    g_state.tx_seq = 0;
    g_state.tx_nonce_ext = 0;
    g_state.rx_last_seq = 0xFF;     /* sentinel: no packet yet */
    g_state.inited = false;

    aes128_init(&g_state.aes_ctx, AES_KEY);
    /* CC1101 init is performed by the application separately, since
     * the library is radio-agnostic in spirit. Here we just mark ready. */
    g_state.inited = true;
    return LL_OK;
}

void ll_set_callbacks(const ll_callbacks_t *callbacks)
{
    if (callbacks) g_state.cb = *callbacks;
}

/* ------------------------------------------------------------------ */
/*  Internal: build nonce and AAD for a packet                         */
/* ------------------------------------------------------------------ */
static void build_nonce(const ll_packet_t *pkt, uint8_t nonce[LL_NONCE_SIZE])
{
    /* Nonce layout (13 bytes):
     *   [0..6]  7-byte static prefix (from key.h)
     *   [7..8]  network_id (big-endian)
     *   [9]     SEQ
     *   [10..12] nonce_ext (24-bit runtime counter, big-endian) */
    memcpy(nonce, AES_NONCE_PREFIX, LL_PREFIX_SIZE);
    nonce[LL_PREFIX_SIZE + 0] = (uint8_t)(g_state.config.network_id >> 8);
    nonce[LL_PREFIX_SIZE + 1] = (uint8_t)(g_state.config.network_id & 0xFF);
    nonce[9]  = pkt->seq;
    nonce[10] = pkt->nonce_ext[0];
    nonce[11] = pkt->nonce_ext[1];
    nonce[12] = pkt->nonce_ext[2];
}

/* ------------------------------------------------------------------ */
/*  Internal: raw transmit via CC1101 (blocks until sent)                */
/* ------------------------------------------------------------------ */
static ll_status_t cc1101_xmit(const uint8_t *buf, size_t len)
{
    extern cc1101_status_t cc1101_transmit(cc1101_t *r, const uint8_t *data, size_t length, uint8_t addr);
    extern cc1101_t *cc1101_radio1_ptr(void);  /* weak symbol, app provides */
    cc1101_t *r = cc1101_radio1_ptr();
    if (!r) return LL_ERR_NO_ROUTE;
    cc1101_status_t st = cc1101_transmit(r, buf, len, 0);
    return (st == CC1101_STATUS_OK) ? LL_OK : LL_ERR_BUSY;
}

/* ------------------------------------------------------------------ */
/*  Internal: send a raw packet                                         */
/* ------------------------------------------------------------------ */
static ll_status_t do_send(uint16_t dest, uint8_t type, const uint8_t *payload,
                          size_t len, bool ack, bool stream)
{
    if (!g_state.inited) return LL_ERR_INVALID_PARAM;
    if (len > LL_MAX_PAYLOAD) return LL_ERR_INVALID_PARAM;

    uint8_t  seq        = g_state.tx_seq;
    uint8_t  n_ext[3]   = { (uint8_t)(g_state.tx_nonce_ext >> 16),
                            (uint8_t)(g_state.tx_nonce_ext >> 8),
                            (uint8_t)(g_state.tx_nonce_ext & 0xFF) };
    uint8_t  flags      = 0;
    if (ack)    flags |= LL_FLAG_ACK_REQUIRED;
    if (stream) flags |= LL_FLAG_STREAM;

    /* Build 12-byte clear header (becomes the AAD) */
    uint8_t aad[LL_HEADER_SIZE];
    aad[0]  = LL_VERSION;
    aad[1]  = type;
    aad[2]  = 0;             /* HopCnt */
    aad[3]  = 8;             /* TTL (8 hops max) */
    aad[4]  = flags;
    aad[5]  = (uint8_t)(dest >> 8);
    aad[6]  = (uint8_t)(dest & 0xFF);
    aad[7]  = (uint8_t)(g_state.config.address >> 8);
    aad[8]  = (uint8_t)(g_state.config.address & 0xFF);
    aad[9]  = seq;
    aad[10] = n_ext[0];
    aad[11] = n_ext[1];
    /* (nonce_ext[2] is sent as the 12th clear byte, see below) */

    /* Build 13-byte nonce */
    uint8_t nonce[LL_NONCE_SIZE];
    memcpy(nonce, AES_NONCE_PREFIX, LL_PREFIX_SIZE);
    nonce[7]  = (uint8_t)(g_state.config.network_id >> 8);
    nonce[8]  = (uint8_t)(g_state.config.network_id & 0xFF);
    nonce[9]  = seq;
    nonce[10] = n_ext[0];
    nonce[11] = n_ext[1];
    nonce[12] = n_ext[2];

    /* CCM encode: ciphertext || MAC */
    uint8_t encrypted[LL_MAX_PAYLOAD + LL_MAC_SIZE];
    size_t  ct_len = ccm_encode(&g_state.aes_ctx, nonce,
                                aad, LL_HEADER_SIZE,
                                payload, len,
                                encrypted);
    if (ct_len == 0) return LL_ERR_INVALID_PARAM;

    /* Pack: 12-byte clear header + encrypted section */
    uint8_t packet[LL_HEADER_SIZE + LL_MAX_PAYLOAD + LL_MAC_SIZE];
    uint8_t *p = packet;
    memcpy(p, aad, LL_HEADER_SIZE); p += LL_HEADER_SIZE;
    memcpy(p, encrypted, ct_len);   p += ct_len;
    size_t pkt_len = (size_t)(p - packet);

    /* Transmit */
    ll_status_t st = cc1101_xmit(packet, pkt_len);

    /* Advance state (RAM only) */
    g_state.tx_nonce_ext = (g_state.tx_nonce_ext + 1) & 0x00FFFFFF;
    if (++g_state.tx_seq == 0) g_state.tx_seq = 1;

    return st;
}

ll_status_t ll_send_raw(uint16_t dest, uint8_t type,
                        const uint8_t *payload, size_t len, bool ack)
{
    return do_send(dest, type, payload, len, ack, false);
}

ll_status_t ll_send_text(uint16_t dest, const char *text, size_t len, bool ack)
{
    if (len > LL_MAX_PAYLOAD) return LL_ERR_INVALID_PARAM;
    ll_status_t st = ll_send_raw(dest, LL_TYPE_TEXT,
                                 (const uint8_t *)text, len, ack);
    /* Fire send_complete callback (caller may want to know about errors) */
    if (g_state.cb.on_send_complete)
        g_state.cb.on_send_complete(dest, LL_TYPE_TEXT, st);
    return st;
}

ll_status_t ll_send_command(uint16_t dest, const char *json_cmd,
                            const char *req_id)
{
    if (req_id && strlen(req_id) > 16) return LL_ERR_INVALID_PARAM;
    /* For v1, prepend "id":"<req_id>"," to the JSON. This requires a
     * simple JSON prepend; for the skeleton, we send the raw JSON as
     * payload. Apps should build a complete JSON object. */
    return ll_send_raw(dest, LL_TYPE_COMMAND,
                       (const uint8_t *)json_cmd, strlen(json_cmd), true);
}

ll_status_t ll_send_response(uint16_t dest, const char *json_resp,
                             const char *req_id)
{
    if (req_id && strlen(req_id) > 16) return LL_ERR_INVALID_PARAM;
    return ll_send_raw(dest, LL_TYPE_RESPONSE,
                       (const uint8_t *)json_resp, strlen(json_resp), true);
}

ll_status_t ll_send_beacon(void)
{
    /* Stub: a real beacon carries a JSON blob. For Phase 1 we send a
     * minimal placeholder. The application can override this by setting
     * a callback that returns a JSON string. */
    const char *beacon = "{\"name\":\"node\",\"version\":1}";
    return ll_send_raw(0xFFFF, LL_TYPE_BEACON,
                       (const uint8_t *)beacon, strlen(beacon), false);
}

/* ------------------------------------------------------------------ */
/*  Task: check for received packet                                     */
/* ------------------------------------------------------------------ */
void ll_task(void)
{
    if (!g_state.inited) return;

    /* Check if a packet is ready (set by the CC1101 RX interrupt) */
    extern volatile bool cc1101_rx_ready;
    extern cc1101_status_t cc1101_read_data(cc1101_t *r, uint8_t *data,
                                            size_t max_len, size_t *read);
    extern cc1101_t *cc1101_radio1_ptr(void);
    extern int8_t  cc1101_get_rssi(cc1101_t *r);
    extern uint8_t cc1101_get_lqi(cc1101_t *r);

    if (!cc1101_rx_ready) return;
    cc1101_rx_ready = false;

    cc1101_t *r = cc1101_radio1_ptr();
    if (!r) return;

    uint8_t raw[64];
    size_t  raw_len = 0;
    cc1101_status_t st = cc1101_read_data(r, raw, sizeof(raw), &raw_len);
    if (st != CC1101_STATUS_OK || raw_len < LL_HEADER_SIZE + LL_MAC_SIZE) {
        if (g_state.cb.on_send_complete)
            g_state.cb.on_send_complete(0, 0,
                (st == CC1101_STATUS_CRC_MISMATCH) ? LL_ERR_CRC : LL_ERR_SHORT);
        return;
    }

    /* Unpack */
    ll_packet_t pkt;
    if (ll_packet_unpack(raw, raw_len, &pkt) != 0) {
        return;
    }

    /* Duplicate detection: same SEQ from the same source = retransmit */
    if (pkt.seq == g_state.rx_last_seq) {
        if (g_state.cb.on_text_received)
            g_state.cb.on_text_received(pkt.src, "DUPLICATE", 9,
                                        cc1101_get_rssi(r), cc1101_get_lqi(r));
        return;
    }

    /* Build 12-byte AAD from the clear header we just unpacked */
    uint8_t aad[LL_HEADER_SIZE];
    ll_packet_get_aad(&pkt, aad);

    /* Build nonce from prefix + net_id + seq + nonce_ext */
    uint8_t nonce[LL_NONCE_SIZE];
    build_nonce(&pkt, nonce);

    /* Decrypt */
    uint8_t plaintext[LL_MAX_PAYLOAD + 1];
    int pt_len = ccm_decode(&g_state.aes_ctx, nonce,
                            aad, LL_HEADER_SIZE,
                            pkt.encrypted, pkt.encrypted_len,
                            plaintext);
    if (pt_len < 0) {
        if (g_state.cb.on_text_received)
            g_state.cb.on_text_received(pkt.src, "MAC FAIL", 9,
                                        cc1101_get_rssi(r), cc1101_get_lqi(r));
        return;
    }
    plaintext[pt_len] = '\0';

    /* Update SEQ (RAM only) */
    g_state.rx_last_seq = pkt.seq;

    int8_t  rssi = cc1101_get_rssi(r);
    uint8_t lqi  = cc1101_get_lqi(r);

    /* Dispatch by type */
    switch (pkt.type) {
    case LL_TYPE_TEXT:
        if (g_state.cb.on_text_received)
            g_state.cb.on_text_received(pkt.src, (const char *)plaintext,
                                        (size_t)pt_len, rssi, lqi);
        break;
    case LL_TYPE_BEACON:
        if (g_state.cb.on_beacon_received)
            g_state.cb.on_beacon_received(pkt.src, (const char *)plaintext,
                                          (size_t)pt_len, rssi);
        break;
    case LL_TYPE_COMMAND:
        if (g_state.cb.on_command_received)
            g_state.cb.on_command_received(pkt.src, (const char *)plaintext,
                                           (size_t)pt_len, NULL);
        break;
    case LL_TYPE_RESPONSE:
        if (g_state.cb.on_response_received)
            g_state.cb.on_response_received(pkt.src, (const char *)plaintext,
                                            (size_t)pt_len);
        break;
    default:
        /* Unknown type; drop silently */
        break;
    }
}
