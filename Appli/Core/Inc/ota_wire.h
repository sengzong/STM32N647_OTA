#ifndef OTA_WIRE_H
#define OTA_WIRE_H

#include <stdint.h>

#define OTA_WIRE_MAGIC 0x3257464Fu
#define OTA_ACK_MAGIC  0x314B414Fu
#define OTA_CONTROL_MAGIC 0x314D434Fu
#define OTA_COMMAND_ROLLBACK 1u

typedef struct {
    uint32_t total;
    uint32_t desc_len;
    uint32_t image_len;
    uint32_t image_offset;
    uint32_t crc;
    uint32_t version;
    uint32_t command;
    uint32_t profile;
} OtaWireView;

static inline uint32_t ota_wire_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* 0: incomplete, 1: complete, -1: invalid. No untrusted length additions. */
static inline int ota_wire_parse(const uint8_t *p, uint32_t received,
                                 uint32_t capacity, OtaWireView *v)
{
    uint32_t header;
    v->total = 0u;
    v->command = 0u;
    v->profile = 0u;
    if (received >= 4u && ota_wire_u32(p) == OTA_CONTROL_MAGIC)
    {
        if (received < 16u) { return 0; }
        if (received != 16u || ota_wire_u32(p + 4u) != OTA_COMMAND_ROLLBACK ||
            ota_wire_u32(p + 8u) != 0u || ota_wire_u32(p + 12u) != 0u) { return -1; }
        v->command = OTA_COMMAND_ROLLBACK;
        v->total = 16u;
        return 1;
    }
    if (received >= 4u && ota_wire_u32(p) != OTA_WIRE_MAGIC) { return -1; }
    if (received < 16u) { return 0; }
    v->version = ota_wire_u32(p + 4u);
    if (v->version < 2u || v->version > 4u) { return -1; }
    header = (v->version == 4u) ? 24u : ((v->version == 3u) ? 20u : 16u);
    v->desc_len = ota_wire_u32(p + 8u);
    v->image_len = ota_wire_u32(p + 12u);
    if (capacity < 64u || v->desc_len > 2000u || v->image_len < 8u ||
        v->image_len > capacity - 64u || header > capacity ||
        v->desc_len > capacity - header ||
        v->image_len > capacity - header - v->desc_len) { return -1; }
    v->image_offset = header + v->desc_len;
    v->total = v->image_offset + v->image_len;
    if (received > v->total) { return -1; }
    if (received < header) { return 0; }
    v->crc = (v->version >= 3u) ? ota_wire_u32(p + 16u) : 0u;
    if (v->version == 4u)
    {
        v->profile = ota_wire_u32(p + 20u);
        if (v->profile > 2u) return -1;
    }
    return (received == v->total) ? 1 : 0;
}

#endif
