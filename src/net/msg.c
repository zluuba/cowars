#include "msg.h"
#include "util.h"

#include <netinet/in.h>

int msg_encode_to_buf(const struct TlvMsg *msg, uint8_t *buf, size_t *buf_size, size_t buf_max_size) {
    logenter();
    size_t msg_size = msg->len + MSG_HDR_SIZE;
    size_t available_size = buf_max_size - *buf_size;
    if (msg_size > available_size) {
        loge("Failed to add message to buffer: not space space: msg len:%zu available:%zu",
             msg_size, available_size);
        return -1;
    }

    // encode tag
    uint16_t tmp = htons(msg->tag);
    memcpy(buf + *buf_size, &tmp, sizeof(tmp));
    *buf_size += sizeof(tmp);
    // encode len
    tmp = htons(msg->len);
    memcpy(buf + *buf_size, &tmp, sizeof(tmp));
    *buf_size += sizeof(tmp);
    // encode val
    memcpy(buf + *buf_size, msg->val, msg->len);
    *buf_size += msg->len;
    return 0;
}
