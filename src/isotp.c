#include "isotp.h"
#include <stddef.h>
#include <string.h>

#define TP_RX_BLOCK 8U
#define TP_RX_STMIN 5U

static void abort_rx(isotp_t *tp)
{
    tp->rx_size = 0U;
    tp->rx_pos = 0U;
    tp->fc_pending = 0U;
    ++tp->errors;
}

static void abort_tx(isotp_t *tp)
{
    tp->tx_state = 0U;
    tp->tx_size = 0U;
    ++tp->errors;
}

void isotp_init(isotp_t *tp, isotp_send_fn send, void *user)
{
    memset(tp, 0, sizeof(*tp));
    tp->send = send;
    tp->user = user;
}

bool isotp_idle(const isotp_t *tp)
{
    return tp->tx_state == 0U && tp->rx_size == 0U &&
           !tp->ready && tp->fc_pending == 0U;
}

static void receive_fc(isotp_t *tp, const uint8_t *p, uint8_t length, uint32_t now)
{
    uint8_t status = p[0] & 0x0FU;
    if (tp->tx_state != 2U || length < 3U) {
        ++tp->errors;
        return;
    }
    if (status == 1U) {
        if (++tp->waits > 3U) {
            abort_tx(tp);
        } else {
            tp->tx_at = now;
        }
        return;
    }
    if (status != 0U || (p[2] > 0x7FU && (p[2] < 0xF1U || p[2] > 0xF9U))) {
        abort_tx(tp);
        return;
    }
    tp->stmin_ms = p[2] <= 0x7FU ? p[2] : 1U;
    tp->tx_block = p[1];
    tp->tx_sent = 0U;
    tp->waits = 0U;
    tp->tx_state = 3U;
    tp->tx_at = now;
    tp->next_at = now;
}

void isotp_receive(isotp_t *tp, const uint8_t *p, uint8_t length, uint32_t now)
{
    uint8_t type, count;
    uint16_t size, left;
    if (p == NULL || length == 0U || length > 8U) {
        ++tp->errors;
        return;
    }
    /* 即使调用者没有先poll，也不能接受已经超时的CF/FC。 */
    if (tp->rx_size && now - tp->rx_at >= TG_TP_TIMEOUT_MS) {
        abort_rx(tp);
        ++tp->timeouts;
    }
    if (tp->tx_state && now - tp->tx_at >= TG_TP_TIMEOUT_MS) {
        abort_tx(tp);
        ++tp->timeouts;
    }
    type = p[0] >> 4U;
    if (type == 3U) {
        receive_fc(tp, p, length, now);
        return;
    }
    /* 一个请求/响应会话；不覆盖尚未交付的请求或正在发送的响应。 */
    if (tp->ready || tp->tx_state != 0U) {
        ++tp->errors;
        return;
    }
    if (type == 0U) {
        count = p[0] & 0x0FU;
        if (count == 0U || count > 7U || length < count + 1U) {
            abort_rx(tp);
            return;
        }
        tp->fc_pending = 0U;
        tp->rx_pos = count;
        tp->rx_size = 0U;
        memcpy(tp->rx, p + 1U, count);
        tp->ready = true;
        ++tp->received;
    } else if (type == 1U) {
        if (length != 8U) {
            abort_rx(tp);
            return;
        }
        size = (uint16_t)(((uint16_t)(p[0] & 0x0FU) << 8U) | p[1]);
        if (size <= 7U || size > TG_TP_CAPACITY) {
            abort_rx(tp);
            if (size > TG_TP_CAPACITY) {
                tp->fc_pending = 0x32U; /* 缓冲不足，发送Overflow。 */
                tp->rx_at = now;
            }
            return;
        }
        memcpy(tp->rx, p + 2U, 6U);
        tp->rx_size = size;
        tp->rx_pos = 6U;
        tp->rx_sn = 1U;
        tp->rx_block = 0U;
        tp->rx_at = now;
        tp->fc_pending = 0x30U;
    } else if (type == 2U) {
        if (tp->rx_size == 0U || tp->fc_pending ||
            (p[0] & 0x0FU) != tp->rx_sn) {
            abort_rx(tp);
            return;
        }
        left = (uint16_t)(tp->rx_size - tp->rx_pos);
        count = left > 7U ? 7U : (uint8_t)left;
        if (length < count + 1U) {
            abort_rx(tp);
            return;
        }
        memcpy(tp->rx + tp->rx_pos, p + 1U, count);
        tp->rx_pos = (uint16_t)(tp->rx_pos + count);
        tp->rx_sn = (uint8_t)((tp->rx_sn + 1U) & 0x0FU);
        tp->rx_at = now;
        if (tp->rx_pos == tp->rx_size) {
            tp->rx_size = 0U;
            tp->ready = true;
            ++tp->received;
        } else if (++tp->rx_block == TP_RX_BLOCK) {
            tp->rx_block = 0U;
            tp->fc_pending = 0x30U;
        }
    } else {
        abort_rx(tp);
    }
}

bool isotp_take(isotp_t *tp, uint8_t *data, uint16_t capacity, uint16_t *length)
{
    if (!tp->ready || data == NULL || length == NULL || capacity < tp->rx_pos) {
        return false;
    }
    memcpy(data, tp->rx, tp->rx_pos);
    *length = tp->rx_pos;
    tp->ready = false;
    tp->rx_pos = 0U;
    return true;
}

bool isotp_reply(isotp_t *tp, const uint8_t *data, uint16_t length, uint32_t now)
{
    if (!isotp_idle(tp) || data == NULL || length == 0U || length > TG_TP_CAPACITY) {
        return false;
    }
    memcpy(tp->tx, data, length);
    tp->tx_size = length;
    tp->tx_pos = 0U;
    tp->tx_sn = 1U;
    tp->waits = 0U;
    tp->tx_at = now;
    tp->tx_state = 1U;
    return true;
}

void isotp_poll(isotp_t *tp, uint32_t now)
{
    uint8_t p[8] = {0U};
    uint8_t count;
    uint16_t left;
    if ((tp->rx_size || tp->fc_pending) && now - tp->rx_at >= TG_TP_TIMEOUT_MS) {
        abort_rx(tp);
        ++tp->timeouts;
    }
    if (tp->tx_state && now - tp->tx_at >= TG_TP_TIMEOUT_MS) {
        abort_tx(tp);
        ++tp->timeouts;
    }
    if (tp->send == NULL) {
        return;
    }
    if (tp->fc_pending) {
        p[0] = tp->fc_pending;
        if (p[0] == 0x30U) {
            p[1] = TP_RX_BLOCK;
            p[2] = TP_RX_STMIN;
        }
        if (tp->send(tp->user, p)) {
            tp->fc_pending = 0U;
            tp->rx_at = now;
        }
        return;
    }
    if (tp->tx_state == 1U) {
        if (tp->tx_size <= 7U) {
            p[0] = (uint8_t)tp->tx_size;
            memcpy(p + 1U, tp->tx, tp->tx_size);
        } else {
            p[0] = (uint8_t)(0x10U | (tp->tx_size >> 8U));
            p[1] = (uint8_t)tp->tx_size;
            memcpy(p + 2U, tp->tx, 6U);
        }
        if (tp->send(tp->user, p)) {
            tp->tx_at = now;
            if (tp->tx_size <= 7U) {
                tp->tx_state = 0U;
                tp->tx_size = 0U;
                ++tp->sent;
            } else {
                tp->tx_pos = 6U;
                tp->tx_state = 2U;
            }
        }
        return;
    }
    if (tp->tx_state != 3U || now - tp->next_at < (uint32_t)tp->stmin_ms + 1U) {
        return;
    }
    left = (uint16_t)(tp->tx_size - tp->tx_pos);
    count = left > 7U ? 7U : (uint8_t)left;
    p[0] = (uint8_t)(0x20U | tp->tx_sn);
    memcpy(p + 1U, tp->tx + tp->tx_pos, count);
    if (!tp->send(tp->user, p)) {
        return;
    }
    tp->tx_pos = (uint16_t)(tp->tx_pos + count);
    tp->tx_sn = (uint8_t)((tp->tx_sn + 1U) & 0x0FU);
    /* 毫秒分辨率下保守多等1ms，覆盖500k总线上一帧的发送时间。 */
    tp->next_at = now;
    tp->tx_at = now;
    if (tp->tx_pos == tp->tx_size) {
        tp->tx_state = 0U;
        tp->tx_size = 0U;
        ++tp->sent;
    } else if (tp->tx_block && ++tp->tx_sent >= tp->tx_block) {
        tp->tx_state = 2U;
    }
}
