#ifndef THERMOGUARD_ISOTP_H
#define THERMOGUARD_ISOTP_H

#include "app_config.h"
#include <stdbool.h>
#include <stdint.h>

/* send返回true只表示帧已交给驱动；false时状态机保留原帧重试。 */
typedef bool (*isotp_send_fn)(void *user, const uint8_t data[8]);
typedef struct {
    isotp_send_fn send;
    void *user;
    uint8_t rx[TG_TP_CAPACITY];
    uint8_t tx[TG_TP_CAPACITY];
    uint16_t rx_size, rx_pos, tx_size, tx_pos;
    uint8_t rx_sn, tx_sn, rx_block, tx_block, tx_sent, waits;
    uint8_t tx_state; /* 0空闲，1首帧待发送，2等待FC，3发送CF。 */
    uint8_t fc_pending;
    uint8_t stmin_ms;
    bool ready;
    uint32_t rx_at, tx_at, next_at;
    uint32_t errors, timeouts, received, sent;
} isotp_t;

void isotp_init(isotp_t *tp, isotp_send_fn send, void *user);
void isotp_receive(isotp_t *tp, const uint8_t *data, uint8_t length, uint32_t now);
void isotp_poll(isotp_t *tp, uint32_t now);
bool isotp_take(isotp_t *tp, uint8_t *data, uint16_t capacity, uint16_t *length);
bool isotp_reply(isotp_t *tp, const uint8_t *data, uint16_t length, uint32_t now);
bool isotp_idle(const isotp_t *tp);

#endif
