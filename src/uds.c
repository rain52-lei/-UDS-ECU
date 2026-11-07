#include "uds.h"
#include <string.h>

static const char s_software_version[] = "ThermoGuard-SDK-2.0";

static void be16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)(value >> 8U);
    p[1] = (uint8_t)value;
}

static void be32(uint8_t *p, uint32_t value)
{
    p[0] = (uint8_t)(value >> 24U);
    p[1] = (uint8_t)(value >> 16U);
    p[2] = (uint8_t)(value >> 8U);
    p[3] = (uint8_t)value;
}

static size_t negative(uds_t *uds, uint8_t *r, uint8_t sid, uint8_t nrc)
{
    ++uds->negative_responses;
    r[0] = 0x7FU;
    r[1] = sid;
    r[2] = nrc;
    return 3U;
}

void uds_init(uds_t *uds, diag_store_t *store, const uds_actions_t *actions)
{
    memset(uds, 0, sizeof(*uds));
    uds->session = 1U;
    uds->store = store;
    if (actions != NULL) {
        uds->actions = *actions;
    }
}

void uds_poll(uds_t *uds, uint32_t now)
{
    if (uds->session != 1U && now - uds->last_request_ms >= TG_SESSION_TIMEOUT_MS) {
        uds->session = 1U;
    }
}

static bool stopped_healthy(const uds_snapshot_t *s)
{
    return !s->enable && (s->state == 0U || s->state == 3U) && s->sensor_valid &&
           !s->sensor_fault && !s->overtemp && !s->bus_off &&
           !s->scheduler_fault && !s->pwm_fault;
}

static size_t read_did(uds_t *uds, const uds_snapshot_t *s,
                       const uint8_t *q, uint8_t *r)
{
    uint16_t did = (uint16_t)(((uint16_t)q[1] << 8U) | q[2]);
    size_t used = 3U;
    unsigned i;
    r[0] = 0x62U;
    r[1] = q[1];
    r[2] = q[2];
    switch (did) {
    case 0xF190U:
        memcpy(r + 3U, "TGSDK144000000001", 17U);
        return 20U;
    case 0xF180U:
        /* 长度随版本内容更新，不发送字符串终止符。 */
        memcpy(r + 3U, s_software_version, sizeof(s_software_version) - 1U);
        return 3U + sizeof(s_software_version) - 1U;
    case 0xF1A0U:
        be16(r + 3U, (uint16_t)s->temperature_dC);
        be16(r + 5U, (uint16_t)s->target_dC);
        be16(r + 7U, s->pwm);
        r[9] = s->state;
        r[10] = (uint8_t)((s->enable ? 1U : 0U) |
                         (s->sensor_valid ? 2U : 0U) |
                         (s->sensor_fault ? 4U : 0U) |
                         (s->overtemp ? 8U : 0U) |
                         (s->remote ? 16U : 0U) |
                         (s->comm_timeout ? 32U : 0U) |
                         (s->bus_off ? 64U : 0U) |
                         (s->scheduler_fault ? 128U : 0U));
        be32(r + 11U, s->now);
        return 15U;
    case 0xF1A1U:
        for (i = 0U; i < DIAG_DTC_COUNT; ++i) {
            const diag_dtc_t *d = &uds->store->dtc[i];
            r[used++] = (uint8_t)i;
            r[used++] = diag_dtc_status(d);
            be16(r + used, d->hits);
            used += 2U;
            be16(r + used, (uint16_t)d->freeze_temperature_dC);
            used += 2U;
            be16(r + used, d->freeze_pwm);
            used += 2U;
            be32(r + used, d->first_ms);
            used += 4U;
        }
        return used;
    case 0xF1A2U:
        r[used++] = uds->store->count;
        for (i = 0U; i < uds->store->count; ++i) {
            const diag_event_t *event = diag_store_event(uds->store, (uint8_t)i);
            be32(r + used, event->at_ms);
            used += 4U;
            r[used++] = event->code;
            r[used++] = event->active ? 1U : 0U;
            be16(r + used, (uint16_t)event->temperature_dC);
            used += 2U;
        }
        return used;
    case 0xF1A3U:
        be16(r + 3U, (uint16_t)s->target_dC);
        return 5U;
    case 0xF1A4U:
        r[3] = s->remote ? 1U : 0U;
        return 4U;
    default:
        return negative(uds, r, 0x22U, 0x31U);
    }
}

static size_t read_dtc(uds_t *uds, const uint8_t *q, size_t n, uint8_t *r)
{
    unsigned i;
    size_t used;
    uint8_t sub;
    bool suppress;
    if (n < 2U) {
        return negative(uds, r, 0x19U, 0x13U);
    }
    sub = q[1] & 0x7FU;
    suppress = (q[1] & 0x80U) != 0U;
    r[0] = 0x59U;
    r[1] = sub;
    if (sub == 2U) {
        if (n != 3U) {
            return negative(uds, r, 0x19U, 0x13U);
        }
        r[2] = 0x09U;
        used = 3U;
        for (i = 0U; i < DIAG_DTC_COUNT; ++i) {
            uint8_t status = diag_dtc_status(&uds->store->dtc[i]);
            if ((status & q[2]) != 0U) {
                uint32_t code = diag_dtc_code((diag_fault_t)i);
                r[used++] = (uint8_t)(code >> 16U);
                r[used++] = (uint8_t)(code >> 8U);
                r[used++] = (uint8_t)code;
                r[used++] = status;
            }
        }
        return suppress ? 0U : used;
    }
    if (sub == 4U) {
        uint32_t code;
        if (n != 6U) {
            return negative(uds, r, 0x19U, 0x13U);
        }
        code = ((uint32_t)q[2] << 16U) | ((uint32_t)q[3] << 8U) | q[4];
        for (i = 0U; i < DIAG_DTC_COUNT; ++i) {
            const diag_dtc_t *d = &uds->store->dtc[i];
            if (code == diag_dtc_code((diag_fault_t)i) && d->stored &&
                (q[5] == 1U || q[5] == 0xFFU)) {
                memcpy(r + 2U, q + 2U, 3U);
                r[5] = diag_dtc_status(d);
                r[6] = 1U; /* snapshotRecordNumber */
                r[7] = 3U; /* 三个DID记录 */
                be16(r + 8U, 0xF1B0U);
                be16(r + 10U, (uint16_t)d->freeze_temperature_dC);
                be16(r + 12U, 0xF1B1U);
                be16(r + 14U, d->freeze_pwm);
                be16(r + 16U, 0xF1B2U);
                be32(r + 18U, d->first_ms);
                return suppress ? 0U : 22U;
            }
        }
        return negative(uds, r, 0x19U, 0x31U);
    }
    return negative(uds, r, 0x19U, 0x12U);
}

size_t uds_process(uds_t *uds, const uds_snapshot_t *s,
                   const uint8_t *q, size_t n, uint8_t *r, size_t capacity)
{
    uint8_t sid, sub;
    uint16_t value;
    bool ok, suppress;
    /* 固定容量契约保证所有DID/事件响应都能有界写入。 */
    if (uds == NULL || s == NULL || q == NULL || r == NULL ||
        n == 0U || n > TG_TP_CAPACITY || capacity < TG_TP_CAPACITY || uds->store == NULL) {
        return 0U;
    }
    uds_poll(uds, s->now);
    uds->last_request_ms = s->now;
    ++uds->requests;
    sid = q[0];
    switch (sid) {
    case 0x10U:
        if (n != 2U) {
            return negative(uds, r, sid, 0x13U);
        }
        sub = q[1] & 0x7FU;
        if (sub != 1U && sub != 3U) {
            return negative(uds, r, sid, 0x12U);
        }
        uds->session = sub;
        r[0] = 0x50U;
        r[1] = sub;
        be16(r + 2U, 50U);
        be16(r + 4U, 500U); /* P2*=500*10ms */
        return (q[1] & 0x80U) ? 0U : 6U;
    case 0x3EU:
        if (n != 2U) {
            return negative(uds, r, sid, 0x13U);
        }
        if ((q[1] & 0x7FU) != 0U) {
            return negative(uds, r, sid, 0x12U);
        }
        r[0] = 0x7EU;
        r[1] = 0U;
        return (q[1] & 0x80U) ? 0U : 2U;
    case 0x22U:
        if (n != 3U) {
            return negative(uds, r, sid, 0x13U);
        }
        return read_did(uds, s, q, r);
    case 0x19U:
        return read_dtc(uds, q, n, r);
    case 0x14U:
        if (n != 4U) {
            return negative(uds, r, sid, 0x13U);
        }
        if (q[1] != 0xFFU || q[2] != 0xFFU || q[3] != 0xFFU) {
            return negative(uds, r, sid, 0x31U);
        }
        if (!diag_store_clear(uds->store, s->enable)) {
            return negative(uds, r, sid, 0x22U);
        }
        r[0] = 0x54U;
        return 1U;
    case 0x2EU:
        if (n < 3U) {
            return negative(uds, r, sid, 0x13U);
        }
        if (q[1] != 0xF1U || (q[2] != 0xA3U && q[2] != 0xA4U)) {
            return negative(uds, r, sid, 0x31U);
        }
        if ((q[2] == 0xA3U && n != 5U) || (q[2] == 0xA4U && n != 4U)) {
            return negative(uds, r, sid, 0x13U);
        }
        if (uds->session != 3U) {
            return negative(uds, r, sid, 0x7FU);
        }
        if (!stopped_healthy(s) || (s->remote && q[2] == 0xA3U)) {
            return negative(uds, r, sid, 0x22U);
        }
        if (q[2] == 0xA3U) {
            value = (uint16_t)(((uint16_t)q[3] << 8U) | q[4]);
            if (value < TG_TARGET_MIN_DC || value > TG_TARGET_MAX_DC) {
                return negative(uds, r, sid, 0x31U);
            }
            ok = uds->actions.set_target != NULL &&
                 uds->actions.set_target(uds->actions.user, (int16_t)value);
        } else {
            if (q[3] > 1U) {
                return negative(uds, r, sid, 0x31U);
            }
            ok = uds->actions.set_remote != NULL &&
                 uds->actions.set_remote(uds->actions.user, q[3] != 0U);
        }
        if (!ok) {
            return negative(uds, r, sid, 0x22U);
        }
        r[0] = 0x6EU;
        r[1] = q[1];
        r[2] = q[2];
        return 3U;
    case 0x31U:
        if (n != 4U) {
            return negative(uds, r, sid, 0x13U);
        }
        sub = q[1] & 0x7FU;
        suppress = (q[1] & 0x80U) != 0U;
        if (sub != 1U) {
            return negative(uds, r, sid, 0x12U);
        }
        if (q[2] != 0xFFU || q[3] > 1U) {
            return negative(uds, r, sid, 0x31U);
        }
        if (uds->session != 3U) {
            return negative(uds, r, sid, 0x7FU);
        }
        if (s->enable || !s->sensor_valid || s->sensor_fault ||
            s->temperature_dC >= 900 || s->bus_off || s->scheduler_fault || s->pwm_fault) {
            return negative(uds, r, sid, 0x22U);
        }
        if (q[3] == 0U) {
            ok = uds->actions.clear_overtemp != NULL &&
                 uds->actions.clear_overtemp(uds->actions.user);
        } else {
            ok = uds->actions.reset_control != NULL &&
                 uds->actions.reset_control(uds->actions.user);
        }
        if (!ok) {
            return negative(uds, r, sid, 0x22U);
        }
        r[0] = 0x71U;
        r[1] = 1U;
        r[2] = q[2];
        r[3] = q[3];
        return suppress ? 0U : 4U;
    default:
        return negative(uds, r, sid, 0x11U);
    }
}
