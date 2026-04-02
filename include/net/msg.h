#ifndef INCLUDE_MSG_H
#define INCLUDE_MSG_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define MSG_MAX_LEN UINT16_MAX
#define MSG_MAX_TAG UINT16_MAX
#define MSG_HDR_SIZE (sizeof(uint16_t/*tag*/) + sizeof(uint16_t/*len*/))

struct TlvMsg {
    bool is_important;
    uint16_t tag;
    uint16_t len;
    const uint8_t *val;
};

int msg_encode_to_buf(const struct TlvMsg *msg,
                      uint8_t *buf, size_t *buf_size, size_t buf_max_size);

#endif // INCLUDE_MSG_H
