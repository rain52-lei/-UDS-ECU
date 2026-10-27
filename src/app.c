#include "app.h"
#include "app_config.h"
#include "keys.h"
#include "timebase.h"
#include "adc_input.h"
#include "temperature.h"
#include "faults.h"
#include "ecu_state.h"
#include "pwm_output.h"
#include "control.h"
#include "serial_port.h"
#include "serial_command.h"
#include "can_protocol.h"
#include "can_port.h"
#include "remote_control.h"
#include "isotp.h"
#include "uds.h"
#include "scheduler_guard.h"
#include "watchdog_port.h"
#include <stdio.h>
#include <string.h>

volatile uint32_t g_jobs_10ms, g_skipped_periods, g_job_100ms;
volatile bool g_enable_request;
volatile uint16_t g_adc_raw;
volatile bool g_adc_valid;
volatile uint32_t g_adc_errors;
volatile int16_t g_temperature_dC, g_temperature_filter_dC;
volatile bool g_sensor_valid, g_sensor_fault, g_overtemp_latched;
volatile ecu_state_t g_ecu_state = ECU_OFF;
volatile uint16_t g_cooling_permille;
volatile status_t g_pwm_status = STATUS_SUCCESS;
volatile int16_t g_target_dC = 450;
volatile status_t g_telemetry_tx_status = STATUS_SUCCESS;
volatile status_t g_can_status_tx_status = STATUS_SUCCESS;
volatile uint32_t g_can_status_tx_count, g_can_status_tx_busy;
volatile bool g_can_command_decode_ok;
volatile uint32_t g_can_command_valid_count, g_can_command_invalid_count;
volatile uint8_t g_can_decoded_counter;
volatile int16_t g_can_decoded_target_dC;
volatile bool g_can_decoded_enable;
volatile bool g_remote_mode, g_can_command_timeout, g_can_bus_off;
volatile uint32_t g_can_command_accepted, g_can_command_rejected, g_can_command_missing;
volatile bool g_scheduler_fault;
volatile uint32_t g_isotp_errors, g_isotp_timeouts, g_uds_requests;
volatile uint8_t g_uds_session;
volatile bool g_can_command_test_request, g_can_command_test_bad_crc;
volatile status_t g_can_command_test_tx_status;
volatile uint8_t g_can_command_test_counter;
volatile bool g_diag_test_request;
uint8_t g_can_status_payload[CAN_STATUS_LENGTH];

static uint32_t s_last_control_ms, s_last_telemetry_ms;
static bool s_local_enabled;
static remote_control_t s_remote;
static scheduler_guard_t s_guard;
static diag_store_t s_store;
static uds_t s_uds;
static isotp_t s_tp;
static uint8_t s_diag_request[TG_TP_CAPACITY];
static uint8_t s_diag_response[TG_TP_CAPACITY];

static bool diag_send(void *user, const uint8_t data[8])
{
    (void)user;
    return can_port_send_diagnostic(data) == STATUS_SUCCESS;
}

static bool set_target(void *user, int16_t target)
{
    (void)user;
    if (g_enable_request || s_remote.remote || target < TG_TARGET_MIN_DC || target > TG_TARGET_MAX_DC) {
        return false;
    }
    g_target_dC = target;
    control_reset();
    return true;
}

static bool set_remote(void *user, bool remote)
{
    (void)user;
    if (g_enable_request) { return false; }
    remote_control_set_mode(&s_remote, remote);
    s_local_enabled = false;
    g_remote_mode = remote;
    control_reset();
    return true;
}

static bool clear_overtemp(void *user)
{
    (void)user;
    if (!faults_clear_overtemp(g_sensor_valid && !g_sensor_fault,
                               g_temperature_dC, g_enable_request)) { return false; }
    g_overtemp_latched = false;
    diag_store_update(&s_store, DIAG_OVERTEMP, false, g_temperature_dC,
                       g_cooling_permille, timebase_ms());
    return true;
}

static bool reset_control(void *user)
{
    (void)user;
    if (g_enable_request) { return false; }
    control_reset();
    return true;
}

static void poll_can_command(uint32_t now)
{
    can_port_frame_t frame;
    can_command_t command;
    unsigned budget;
    for (budget = 0U; budget < 4U && can_port_take_command_frame(&frame); ++budget) {
        g_can_command_decode_ok = frame.id == CAN_COMMAND_ID &&
            can_protocol_decode_command(frame.data, frame.length, &command);
        if (!g_can_command_decode_ok) {
            ++g_can_command_invalid_count;
            continue;
        }
        ++g_can_command_valid_count;
        g_can_decoded_counter = command.counter;
        g_can_decoded_target_dC = command.target_dC;
        g_can_decoded_enable = command.enable_request;
        (void)remote_control_accept(&s_remote, &command, now);
    }
    g_can_command_accepted = s_remote.accepted;
    g_can_command_rejected = s_remote.rejected;
    g_can_command_missing = s_remote.missing;
}

static uds_snapshot_t snapshot(uint32_t now)
{
    uds_snapshot_t s;
    memset(&s, 0, sizeof(s));
    s.now = now;
    s.temperature_dC = g_temperature_dC;
    s.target_dC = g_target_dC;
    s.pwm = g_cooling_permille;
    s.state = (uint8_t)g_ecu_state;
    s.enable = g_enable_request;
    s.sensor_valid = g_sensor_valid;
    s.sensor_fault = g_sensor_fault;
    s.overtemp = g_overtemp_latched;
    s.remote = s_remote.remote;
    s.comm_timeout = g_can_command_timeout;
    s.bus_off = g_can_bus_off;
    s.scheduler_fault = g_scheduler_fault;
    s.pwm_fault = g_pwm_status != STATUS_SUCCESS;
    return s;
}

static void poll_diagnostic(uint32_t now)
{
    can_port_frame_t frame;
    uint16_t length;
    unsigned budget;
    size_t response_length;
    uds_snapshot_t s;
    uds_poll(&s_uds, now);
    for (budget = 0U; budget < 8U && can_port_take_diagnostic_frame(&frame); ++budget) {
        isotp_receive(&s_tp, frame.data, frame.length, now);
    }
    if (isotp_take(&s_tp, s_diag_request, sizeof(s_diag_request), &length)) {
        s = snapshot(now);
        response_length = uds_process(&s_uds, &s, s_diag_request, length,
                                       s_diag_response, sizeof(s_diag_response));
        if (response_length != 0U) {
            (void)isotp_reply(&s_tp, s_diag_response, (uint16_t)response_length, now);
        }
    }
    isotp_poll(&s_tp, now);
    g_isotp_errors = s_tp.errors;
    g_isotp_timeouts = s_tp.timeouts;
    g_uds_requests = s_uds.requests;
    g_uds_session = s_uds.session;
}

static void poll_test(void)
{
#if TG_CAN_LOOPBACK
    if (g_can_command_test_request) {
        uint8_t data[8] = {1U, 0U, 1U, 0xC2U, 1U, 0U, 0U, 0x34U};
        data[1] = g_can_command_test_counter;
        if (data[1] != 0U) { data[7] = can_protocol_crc8_sae_j1850(data, 7U); }
        if (g_can_command_test_bad_crc) { data[7] ^= 1U; }
        g_can_command_test_tx_status = can_port_send(CAN_COMMAND_ID, data, 8U);
        if (g_can_command_test_tx_status == STATUS_SUCCESS) {
            g_can_command_test_request = false;
            ++g_can_command_test_counter;
        }
    }
    if (g_diag_test_request) {
        /* 单帧读取目标温度，不需要额外测试端发送流控。 */
        static const uint8_t request[8] = {3U, 0x22U, 0xF1U, 0xA3U, 0U, 0U, 0U, 0U};
        if (can_port_send(TG_DIAG_REQUEST_ID, request, 8U) == STATUS_SUCCESS) {
            g_diag_test_request = false;
        }
    }
#endif
}

static void apply_events(void)
{
    keys_events_t keys = keys_sample_10ms();
    serial_command_events_t serial = serial_command_take_events();
    bool stop = keys.key2_pressed || serial.disable_requested;
    if (stop) {
        s_local_enabled = false;
        g_enable_request = false;
        remote_control_stop(&s_remote);
    }
    if (serial.local_requested || serial.remote_requested) {
        bool ok = !g_enable_request && !s_guard.latched;
        if (ok) { ok = set_remote(NULL, !serial.local_requested); }
        serial_command_set_result(ok);
    }
    if (!stop && !s_remote.remote) {
        if (serial.enable_requested) { s_local_enabled = true; }
        else if (keys.key1_pressed) { s_local_enabled = !s_local_enabled; }
    }
    g_enable_request = s_remote.remote ? remote_control_enabled(&s_remote, timebase_ms()) : s_local_enabled;
    if (serial.target_requested) {
        serial_command_set_result(set_target(NULL, serial.target_dC));
    }
    if (serial.enable_requested || serial.disable_requested) {
        serial_command_set_reply(g_enable_request);
    }
}

static void update_store(uint32_t now)
{
    const bool active[DIAG_DTC_COUNT] = {
        g_sensor_fault, g_overtemp_latched, g_can_command_timeout,
        g_can_bus_off, g_scheduler_fault, g_pwm_status != STATUS_SUCCESS
    };
    unsigned i;
    for (i = 0U; i < DIAG_DTC_COUNT; ++i) {
        diag_store_update(&s_store, (diag_fault_t)i, active[i],
                           g_temperature_dC, g_cooling_permille, now);
    }
}

static void app_10ms(void)
{
    uint16_t raw = 0U;
    apply_events();
    g_adc_valid = adc_input_read(&raw);
    if (g_adc_valid) { g_adc_raw = raw; }
    else { ++g_adc_errors; }
    g_sensor_valid = g_adc_valid && temperature_raw_valid(g_adc_raw);
    g_sensor_fault = faults_sensor_update(g_sensor_valid);
    if (g_sensor_valid) {
        g_temperature_dC = temperature_from_adc(g_adc_raw);
        g_temperature_filter_dC = temperature_filter_update(g_temperature_dC);
    } else { temperature_filter_reset(); }
    g_overtemp_latched = faults_overtemp_update(g_sensor_valid, g_temperature_dC);
    g_can_command_timeout = remote_control_timed_out(&s_remote, timebase_ms());
    g_can_bus_off = can_port_bus_off();
    g_remote_mode = s_remote.remote;
    if (s_remote.remote && s_remote.seen) { g_target_dC = s_remote.target_dC; }
    scheduler_guard_check(&s_guard, timebase_ms());
    g_scheduler_fault = s_guard.latched;
    g_ecu_state = ecu_state_evaluate(g_enable_request, g_sensor_fault, g_overtemp_latched);
    if (g_scheduler_fault || g_pwm_status != STATUS_SUCCESS) {
        g_ecu_state = ECU_FAILSAFE;
    } else if (g_ecu_state != ECU_FAILSAFE &&
               (g_can_command_timeout || (s_remote.remote && g_can_bus_off))) {
        g_ecu_state = ECU_DEGRADED;
    }
    g_cooling_permille = control_step(g_ecu_state, g_temperature_filter_dC, g_target_dC);
    g_pwm_status = pwm_output_set_permille(g_cooling_permille);
    scheduler_guard_complete(&s_guard, timebase_ms(), g_pwm_status == STATUS_SUCCESS);
    g_scheduler_fault = s_guard.latched;
    update_store(timebase_ms());
    ++g_jobs_10ms;
}

static void app_100ms(void)
{
    static char message[192];
    int length;
    can_protocol_pack_status(g_temperature_filter_dC, g_target_dC, g_cooling_permille,
        g_ecu_state, g_sensor_valid, g_sensor_fault, g_overtemp_latched,
        g_enable_request, g_can_status_payload);
    g_can_status_tx_status = can_port_send(CAN_STATUS_ID, g_can_status_payload, CAN_STATUS_LENGTH);
    if (g_can_status_tx_status == STATUS_SUCCESS) { ++g_can_status_tx_count; }
    else if (g_can_status_tx_status == STATUS_BUSY) { ++g_can_status_tx_busy; }
    g_telemetry_tx_status = serial_port_get_tx_status();
    if (g_telemetry_tx_status != STATUS_SUCCESS) { return; }
    length = snprintf(message, sizeof(message),
        "T_dC=%d,SET_dC=%d,STATE=%u,PWM_pm=%u,VALID=%u,REMOTE=%u,CAN_TO=%u,BUS_OFF=%u,SCHED=%u,UDS=%u\r\n",
        (int)g_temperature_filter_dC, (int)g_target_dC, (unsigned)g_ecu_state,
        (unsigned)g_cooling_permille, (unsigned)g_sensor_valid, (unsigned)g_remote_mode,
        (unsigned)g_can_command_timeout, (unsigned)g_can_bus_off,
        (unsigned)g_scheduler_fault, (unsigned)g_uds_session);
    if (length < 0 || (size_t)length >= sizeof(message)) {
        g_telemetry_tx_status = STATUS_ERROR;
        return;
    }
    g_telemetry_tx_status = serial_port_send((const uint8_t *)message, (uint32_t)length);
}

void app_init(void)
{
    const uds_actions_t actions = {set_target, set_remote, clear_overtemp, reset_control, NULL};
    uint32_t now = timebase_ms();
    s_last_control_ms = now;
    s_last_telemetry_ms = now;
    s_local_enabled = false;
    g_jobs_10ms = g_skipped_periods = g_job_100ms = 0U;
    g_adc_raw = 0U;
    g_adc_valid = g_sensor_valid = g_sensor_fault = false;
    g_adc_errors = 0U;
    g_temperature_dC = g_temperature_filter_dC = 0;
    g_enable_request = g_overtemp_latched = false;
    g_cooling_permille = 0U;
    g_target_dC = 450;
    g_ecu_state = ECU_OFF;
    g_pwm_status = g_telemetry_tx_status = g_can_status_tx_status = STATUS_SUCCESS;
    g_can_status_tx_count = g_can_status_tx_busy = 0U;
    g_can_command_decode_ok = g_can_decoded_enable = false;
    g_can_command_valid_count = g_can_command_invalid_count = 0U;
    g_can_decoded_counter = 0U;
    g_can_decoded_target_dC = 0;
    g_remote_mode = g_can_command_timeout = g_can_bus_off = g_scheduler_fault = false;
    g_can_command_accepted = g_can_command_rejected = g_can_command_missing = 0U;
    g_isotp_errors = g_isotp_timeouts = g_uds_requests = 0U;
    g_uds_session = 1U;
    g_can_command_test_request = g_can_command_test_bad_crc = g_diag_test_request = false;
    g_can_command_test_tx_status = STATUS_SUCCESS;
    g_can_command_test_counter = 0U;
    temperature_filter_reset();
    faults_init();
    control_reset();
    serial_command_init();
    remote_control_init(&s_remote);
    scheduler_guard_init(&s_guard, now);
    diag_store_init(&s_store);
    uds_init(&s_uds, &s_store, &actions);
    isotp_init(&s_tp, diag_send, NULL);
}

void app_poll(void)
{
    uint32_t now = timebase_ms();
    uint32_t elapsed;
    serial_port_poll();
    serial_command_poll();
    poll_can_command(now);
    scheduler_guard_check(&s_guard, now);
    elapsed = now - s_last_control_ms;
    if (elapsed >= TG_CONTROL_PERIOD_MS) {
        uint32_t periods = elapsed / TG_CONTROL_PERIOD_MS;
        g_skipped_periods += periods - 1U;
        s_last_control_ms += periods * TG_CONTROL_PERIOD_MS;
        app_10ms();
    }
    poll_diagnostic(timebase_ms());
    poll_test();
    /* 回复优先于周期遥测，避免串口忙时把ACK长期挤掉。 */
    serial_command_poll_reply();
    elapsed = now - s_last_telemetry_ms;
    if (elapsed >= TG_TELEMETRY_PERIOD_MS) {
        s_last_telemetry_ms += (elapsed / TG_TELEMETRY_PERIOD_MS) * TG_TELEMETRY_PERIOD_MS;
        ++g_job_100ms;
        app_100ms();
    }
    /* 只有一次新的、及时完成且PWM提交成功的控制任务才能领取喂狗资格。 */
    if (scheduler_guard_take_feed(&s_guard, timebase_ms())) { watchdog_port_refresh(); }
}
