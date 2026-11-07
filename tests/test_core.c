#include "can_protocol.h"
#include "remote_control.h"
#include "diag_store.h"
#include "isotp.h"
#include "uds.h"
#include "scheduler_guard.h"
#include "faults.h"
#include "control.h"
#include "temperature.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(EXIT_FAILURE); } } while (0)

typedef struct { bool busy; uint8_t frames[512][8]; unsigned count; } bus_t;
static bool emit(void *user, const uint8_t p[8])
{
    bus_t *bus = user;
    if (bus->busy) { return false; }
    assert(bus->count < 512U);
    memcpy(bus->frames[bus->count++], p, 8U);
    return true;
}

static void test_command(void)
{
    uint8_t p[8] = {1U, 0U, 1U, 0xC2U, 1U, 0U, 0U, 0x34U};
    uint8_t status[8];
    can_command_t command;
    remote_control_t remote;
    unsigned i;
    CHECK(can_protocol_crc8_sae_j1850((const uint8_t *)"123456789",9U) == 0x4BU);
    CHECK(can_protocol_decode_command(p, 8U, &command));
    CHECK(command.target_dC == 450 && command.enable_request && command.counter == 0U);
    for (i = 0U; i < 64U; ++i) {
        p[i / 8U] ^= (uint8_t)(1U << (i % 8U));
        CHECK(!can_protocol_decode_command(p, 8U, &command));
        p[i / 8U] ^= (uint8_t)(1U << (i % 8U));
    }
    CHECK(!can_protocol_decode_command(NULL, 8U, &command));
    CHECK(!can_protocol_decode_command(p, 7U, &command));
    CHECK(!can_protocol_decode_command(p, 8U, NULL));
    p[2] = 0xFFU; p[3] = 0x9CU; p[7] = can_protocol_crc8_sae_j1850(p, 7U);
    CHECK(can_protocol_decode_command(p, 8U, &command) && command.target_dC == -100);
    can_protocol_pack_status(-100, 450, 1000U, ECU_FAILSAFE, true, true, true, true, status);
    CHECK(status[0] == 0xFFU && status[1] == 0x9CU && status[7] == 15U);

    remote_control_init(&remote);
    remote_control_set_mode(&remote, true);
    CHECK(remote_control_timed_out(&remote, 0U));
    command.target_dC = 450; command.counter = 254U; command.enable_request = true;
    CHECK(remote_control_accept(&remote, &command, UINT32_MAX - 100U));
    CHECK(remote_control_enabled(&remote, UINT32_MAX - 99U));
    CHECK(!remote_control_accept(&remote, &command, 0U));
    command.counter = 255U; CHECK(remote_control_accept(&remote, &command, 1U));
    command.counter = 0U; CHECK(remote_control_accept(&remote, &command, 2U));
    command.counter = 2U; CHECK(remote_control_accept(&remote, &command, 3U));
    CHECK(remote.missing == 1U);
    command.counter = 1U; CHECK(!remote_control_accept(&remote, &command, 4U));
    CHECK(!remote_control_timed_out(&remote, 502U));
    CHECK(remote_control_timed_out(&remote, 503U));
    CHECK(remote_control_accept(&remote, &command, 503U));
    remote_control_stop(&remote);
    command.counter++; CHECK(remote_control_accept(&remote, &command, 504U));
    CHECK(!remote_control_enabled(&remote, 504U));
    command.enable_request = false; command.counter++;
    CHECK(remote_control_accept(&remote, &command, 505U));
    command.enable_request = true; command.counter++;
    CHECK(remote_control_accept(&remote, &command, 506U));
    CHECK(remote_control_enabled(&remote, 506U));
    command.target_dC = 199; command.counter++;
    CHECK(!remote_control_accept(&remote, &command, 507U));
    remote_control_set_mode(&remote, false);
    CHECK(!remote_control_enabled(&remote, 508U));
}

static void test_tp(void)
{
    isotp_t tp;
    bus_t bus = {0};
    uint8_t data[256], result[256];
    uint8_t frame[8] = {0};
    uint16_t length;
    unsigned i, pos, sn, block;
    for (i = 0U; i < sizeof(data); ++i) { data[i] = (uint8_t)i; }
    isotp_init(&tp, emit, &bus);
    CHECK(isotp_reply(&tp, data, 3U, 0U));
    bus.busy = true; isotp_poll(&tp, 1U);
    CHECK(bus.count == 0U && tp.tx_state == 1U);
    bus.busy = false; isotp_poll(&tp, 2U);
    CHECK(bus.count == 1U && bus.frames[0][0] == 3U && isotp_idle(&tp));

    CHECK(isotp_reply(&tp, data, 256U, 10U));
    isotp_poll(&tp, 10U);
    CHECK(bus.frames[1][0] == 0x11U && bus.frames[1][1] == 0U);
    frame[0] = 0x30U; frame[1] = 2U; frame[2] = 5U;
    isotp_receive(&tp, frame, 8U, 11U);
    isotp_poll(&tp, 16U); CHECK(bus.count == 2U);
    isotp_poll(&tp, 17U); CHECK(bus.count == 3U);
    bus.busy = true; isotp_poll(&tp, 23U); CHECK(tp.tx_pos == 13U);
    bus.busy = false; isotp_poll(&tp, 24U); CHECK(tp.tx_pos == 20U && tp.tx_state == 2U);
    isotp_poll(&tp, 1024U); CHECK(isotp_idle(&tp) && tp.timeouts == 1U);

    /* 256字节接收，覆盖块流控、序号15到0回绕和尾帧长度。 */
    isotp_init(&tp, emit, &bus);
    frame[0] = 0x11U; frame[1] = 0U; memcpy(frame + 2U, data, 6U);
    isotp_receive(&tp, frame, 8U, 0U);
    bus.busy = true; isotp_poll(&tp, 1U); CHECK(tp.fc_pending == 0x30U);
    bus.busy = false; isotp_poll(&tp, 2U); CHECK(tp.fc_pending == 0U);
    pos = 6U; sn = 1U; block = 0U;
    while (pos < 256U) {
        unsigned count = 256U - pos > 7U ? 7U : 256U - pos;
        frame[0] = (uint8_t)(0x20U | sn);
        memcpy(frame + 1U, data + pos, count);
        isotp_receive(&tp, frame, (uint8_t)(count + 1U), 10U + pos);
        pos += count; sn = (sn + 1U) & 15U;
        if (++block == 8U) { isotp_poll(&tp, 10U + pos); block = 0U; }
    }
    CHECK(tp.ready && isotp_take(&tp, result, sizeof(result), &length));
    CHECK(length == 256U && memcmp(data, result, 256U) == 0);

    frame[0] = 0x10U; frame[1] = 20U;
    isotp_receive(&tp, frame, 8U, 1000U); isotp_poll(&tp, 1000U);
    frame[0] = 0x22U; isotp_receive(&tp, frame, 8U, 1001U);
    CHECK(!tp.ready && tp.rx_size == 0U);
    frame[0] = 0x11U; frame[1] = 1U;
    isotp_receive(&tp, frame, 8U, 1002U); isotp_poll(&tp, 1002U);
    CHECK(bus.frames[bus.count - 1U][0] == 0x32U);
    frame[0] = 0x10U; frame[1] = 20U;
    isotp_receive(&tp, frame, 8U, UINT32_MAX - 20U);
    isotp_poll(&tp, 1000U); CHECK(tp.rx_size == 0U && tp.timeouts >= 1U);
}

static void test_tp_pair(void)
{
    isotp_t sender, receiver;
    bus_t forward = {0}, backward = {0};
    uint8_t data[256], copy[256];
    uint16_t length;
    unsigned i, fpos = 0U, bpos = 0U;
    uint32_t start = UINT32_MAX - 100U;
    for (i = 0U; i < 256U; ++i) { data[i] = (uint8_t)(i ^ 0xA5U); }
    isotp_init(&sender, emit, &forward);
    isotp_init(&receiver, emit, &backward);
    CHECK(isotp_reply(&sender, data, 256U, start));
    for (i = 0U; i < 5000U && !receiver.ready; ++i) {
        uint32_t now = start + i;
        forward.busy = i % 11U == 0U;
        backward.busy = i % 13U == 0U;
        isotp_poll(&sender, now);
        while (fpos < forward.count) { isotp_receive(&receiver, forward.frames[fpos++], 8U, now); }
        isotp_poll(&receiver, now);
        while (bpos < backward.count) { isotp_receive(&sender, backward.frames[bpos++], 8U, now); }
    }
    CHECK(receiver.ready && sender.sent == 1U);
    CHECK(isotp_take(&receiver, copy, sizeof(copy), &length));
    CHECK(length == 256U && memcmp(data, copy, 256U) == 0);
    CHECK(sender.errors == 0U && receiver.errors == 0U);
    CHECK(backward.count == 5U); /* 250/7=36个CF，需要五个8帧块。 */
}

static int16_t target_written;
static bool mode_written, routine_called;
static bool write_target(void *u, int16_t v) { (void)u; target_written = v; return true; }
static bool write_mode(void *u, bool v) { (void)u; mode_written = v; return true; }
static bool routine(void *u) { (void)u; routine_called = true; return true; }

static void test_uds(void)
{
    uds_t uds;
    diag_store_t store;
    uds_snapshot_t snap = {0};
    const uds_actions_t actions = {write_target, write_mode, routine, routine, NULL};
    uint8_t q[8] = {0}, r[256];
    size_t length;
    unsigned i;
    diag_store_init(&store); uds_init(&uds, &store, &actions);
    snap.sensor_valid = true; snap.target_dC = 450; snap.temperature_dC = 300;
    q[0] = 0x22U; q[1] = 0xF1U; q[2] = 0x90U;
    length = uds_process(&uds, &snap, q, 3U, r, sizeof(r));
    CHECK(length == 20U && memcmp(r + 3U, "TGSDK144000000001", 17U) == 0);
    {
        static const uint8_t expected_version[] = "\x62\xF1\x80" "ThermoGuard-SDK-2.0";
        q[2] = 0x80U;
        memset(r, 0xA5, sizeof(r));
        length = uds_process(&uds, &snap, q, 3U, r, sizeof(r));
        CHECK(length == sizeof(expected_version) - 1U);
        CHECK(memcmp(r, expected_version, sizeof(expected_version) - 1U) == 0);
        CHECK(r[length] == 0xA5U); /* 响应不携带字符串终止符，也不改写后续字节。 */
    }
    q[0] = 0x2EU; q[2] = 0xA3U; q[3] = 1U; q[4] = 0xC2U;
    CHECK(uds_process(&uds, &snap, q, 5U, r, sizeof(r)) == 3U && r[2] == 0x7FU);
    q[0] = 0x10U; q[1] = 3U;
    CHECK(uds_process(&uds, &snap, q, 2U, r, sizeof(r)) == 6U && uds.session == 3U);
    q[0] = 0x2EU; q[1] = 0xF1U; q[2] = 0xA3U;
    CHECK(uds_process(&uds, &snap, q, 5U, r, sizeof(r)) == 3U && r[0] == 0x6EU && target_written == 450);
    snap.enable = true;
    CHECK(uds_process(&uds, &snap, q, 5U, r, sizeof(r)) == 3U && r[2] == 0x22U);
    snap.enable = false; q[3] = 0U; q[4] = 1U;
    CHECK(uds_process(&uds, &snap, q, 5U, r, sizeof(r)) == 3U && r[2] == 0x31U);
    q[2] = 0xA4U; q[3] = 1U;
    CHECK(uds_process(&uds, &snap, q, 4U, r, sizeof(r)) == 3U && r[0] == 0x6EU && mode_written);
    q[0] = 0x31U; q[1] = 1U; q[2] = 0xFFU; q[3] = 0U;
    CHECK(uds_process(&uds, &snap, q, 4U, r, sizeof(r)) == 4U && routine_called);
    snap.temperature_dC = 900;
    CHECK(uds_process(&uds, &snap, q, 4U, r, sizeof(r)) == 3U && r[2] == 0x22U);
    snap.temperature_dC = 300;
    diag_store_update(&store, DIAG_SENSOR, true, -100, 1000U, 100U);
    diag_store_update(&store, DIAG_SENSOR, true, 500, 0U, 101U);
    CHECK(store.dtc[0].hits == 1U && store.dtc[0].freeze_temperature_dC == -100);
    q[0] = 0x19U; q[1] = 2U; q[2] = 0xFFU;
    CHECK(uds_process(&uds, &snap, q, 3U, r, sizeof(r)) == 7U && r[6] == 9U);
    q[1] = 4U; q[2] = 0xC1U; q[3] = 0U; q[4] = 1U; q[5] = 1U;
    CHECK(uds_process(&uds, &snap, q, 6U, r, sizeof(r)) == 22U && r[10] == 0xFFU && r[11] == 0x9CU);
    q[0] = 0x14U; q[1] = q[2] = q[3] = 0xFFU;
    CHECK(uds_process(&uds, &snap, q, 4U, r, sizeof(r)) == 3U && r[2] == 0x22U);
    diag_store_update(&store, DIAG_SENSOR, false, 300, 0U, 200U);
    CHECK(uds_process(&uds, &snap, q, 4U, r, sizeof(r)) == 1U && r[0] == 0x54U);
    for (i = 0U; i < 100U; ++i) {
        diag_store_update(&store, DIAG_BUS_OFF, (i % 2U) == 0U, 300, 0U, i);
    }
    CHECK(store.count == 24U && diag_store_event(&store, 0U)->at_ms == 76U);
    q[0] = 0x22U; q[1] = 0xF1U; q[2] = 0xA2U;
    CHECK(uds_process(&uds, &snap, q, 3U, r, sizeof(r)) == 196U && r[3] == 24U);
    q[0] = 0x3EU; q[1] = 0x80U;
    CHECK(uds_process(&uds, &snap, q, 2U, r, sizeof(r)) == 0U);
    q[1] = 0x81U;
    CHECK(uds_process(&uds, &snap, q, 2U, r, sizeof(r)) == 3U && r[2] == 0x12U);
    uds_poll(&uds, 5000U); CHECK(uds.session == 1U);
    q[0] = 0x99U;
    CHECK(uds_process(&uds, &snap, q, 1U, r, sizeof(r)) == 3U && r[2] == 0x11U);
    CHECK(uds_process(&uds, &snap, q, 1U, r, 2U) == 0U);
}

static void test_protection(void)
{
    scheduler_guard_t guard;
    unsigned i;
    scheduler_guard_init(&guard, UINT32_MAX - 10U);
    CHECK(!scheduler_guard_take_feed(&guard, UINT32_MAX - 9U));
    scheduler_guard_complete(&guard, 0U, true);
    CHECK(scheduler_guard_take_feed(&guard, 1U));
    CHECK(!scheduler_guard_take_feed(&guard, 2U));
    scheduler_guard_check(&guard, 50U);
    CHECK(guard.latched && !scheduler_guard_take_feed(&guard, 51U));
    scheduler_guard_init(&guard, 0U);
    scheduler_guard_complete(&guard, 10U, false); CHECK(guard.latched);
    faults_init();
    CHECK(!faults_sensor_update(false)); CHECK(!faults_sensor_update(false));
    CHECK(faults_sensor_update(false)); CHECK(!faults_sensor_update(true));
    CHECK(!faults_overtemp_update(true, 999)); CHECK(faults_overtemp_update(true, 1000));
    CHECK(!faults_clear_overtemp(true, 900, false));
    CHECK(!faults_clear_overtemp(true, 800, true));
    CHECK(faults_clear_overtemp(true, 899, false));
    CHECK(!faults_overtemp_update(true, 500));
    CHECK(ecu_state_evaluate(false, true, false) == ECU_FAILSAFE);
    control_reset();
    CHECK(control_step(ECU_RUNNING, 1000, 450) == 20U);
    CHECK(control_step(ECU_FAILSAFE, 300, 450) == 1000U);
    CHECK(control_step(ECU_OFF, 1000, 450) == 0U);
    for (i = 0U; i < 10000U; ++i) {
        uint16_t duty = control_step(ECU_RUNNING, (int16_t)(i % 1651U - 400), 450);
        CHECK(duty <= 1000U);
    }
    CHECK(temperature_from_adc(0U) == -400 && temperature_from_adc(4095U) == 1250);
    CHECK(!temperature_raw_valid(31U) && temperature_raw_valid(32U));
}

static void test_fuzz(void)
{
    uint32_t random = 0x144U;
    unsigned i, j;
    isotp_t tp;
    bus_t bus = {0};
    diag_store_t store;
    uds_t uds;
    uds_snapshot_t snap = {0};
    uint8_t p[256], r[258];
    isotp_init(&tp, emit, &bus); diag_store_init(&store); uds_init(&uds, &store, NULL);
    for (i = 0U; i < 100000U; ++i) {
        for (j = 0U; j < 8U; ++j) { random = random * 1664525U + 1013904223U; p[j] = (uint8_t)(random >> 24U); }
        bus.count = 0U;
        isotp_receive(&tp, p, (uint8_t)(random % 10U), i * 11U);
        isotp_poll(&tp, i * 11U);
        if (tp.ready) { uint16_t n; (void)isotp_take(&tp, p, sizeof(p), &n); }
        r[0] = 0xA5U; r[257] = 0x5AU; snap.now = i * 11U;
        (void)uds_process(&uds, &snap, p, random % 9U, r + 1U, 256U);
        CHECK(r[0] == 0xA5U && r[257] == 0x5AU);
        CHECK(tp.rx_pos <= 256U && tp.tx_pos <= 256U);
    }
}

int main(void)
{
    test_command(); test_tp(); test_tp_pair(); test_uds(); test_protection(); test_fuzz();
    printf("PASS: %u assertions; CRC, remote control, ISO-TP, UDS, DTC, scheduler, control; 100000 randomized cases\n", checks);
    return 0;
}
