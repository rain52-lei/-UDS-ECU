/* 使用真实app/协议/命令解析器，只替换ADC、PWM、时间和物理收发接口。 */
#include "app.h"
#include "app_config.h"
#include "can_port.h"
#include "can_protocol.h"
#include "keys.h"
#include "serial_port.h"
#include "watchdog_port.h"
#include "ecu_state.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

extern volatile bool g_enable_request, g_remote_mode, g_can_command_timeout;
extern volatile bool g_scheduler_fault, g_overtemp_latched;
extern volatile uint32_t g_can_command_accepted, g_can_command_rejected;
extern volatile uint32_t g_can_command_invalid_count, g_uds_requests;
extern volatile int16_t g_target_dC;
extern volatile ecu_state_t g_ecu_state;
extern volatile uint16_t g_cooling_permille;
extern volatile bool g_can_command_test_request;
extern volatile bool g_can_command_test_bad_crc;
extern volatile uint32_t g_can_command_valid_count;

volatile uint32_t g_reset_cause, g_watchdog_feeds;
volatile bool g_watchdog_test_stop;
static uint32_t now;
static uint16_t adc = 1737U;
static bool bus_off;
static status_t pwm_result = STATUS_SUCCESS;
static keys_events_t keys;
static can_port_frame_t command_queue[32], diag_queue[32];
static unsigned command_head, command_tail, diag_head, diag_tail;
static uint8_t responses[256][8];
static unsigned response_count;
static uint8_t uart[1024];
static unsigned uart_head, uart_tail;
static char last_reply[256];
static unsigned assertions;
#define CHECK(x) do { ++assertions; if (!(x)) { fprintf(stderr,"APP FAIL line %d: %s\n",__LINE__,#x); assert(x); } } while (0)

uint32_t timebase_ms(void) { return now; }
bool adc_input_read(uint16_t *value) { *value = adc; return true; }
status_t pwm_output_set_permille(uint16_t duty) { assert(duty <= 1000U); return pwm_result; }
keys_events_t keys_sample_10ms(void) { keys_events_t result = keys; memset(&keys,0,sizeof(keys)); return result; }
void watchdog_port_refresh(void) { if (!g_watchdog_test_stop) { ++g_watchdog_feeds; } }
bool can_port_bus_off(void) { return bus_off; }
bool can_port_take_command_frame(can_port_frame_t *f)
{
    if (command_tail == command_head) { return false; }
    *f = command_queue[command_tail++ % 32U]; return true;
}
bool can_port_take_diagnostic_frame(can_port_frame_t *f)
{
    if (diag_tail == diag_head) { return false; }
    *f = diag_queue[diag_tail++ % 32U]; return true;
}
status_t can_port_send(uint32_t id, const uint8_t *data, uint8_t length)
{
    if (id == CAN_COMMAND_ID) {
        can_port_frame_t *f = &command_queue[command_head++ % 32U];
        f->id = id; f->length = length; memcpy(f->data,data,length);
    }
    return STATUS_SUCCESS;
}
status_t can_port_send_diagnostic(const uint8_t data[8])
{
    assert(response_count < 256U);
    memcpy(responses[response_count++],data,8U); return STATUS_SUCCESS;
}
bool serial_port_take_rx_overflow(void) { return false; }
void serial_port_poll(void) { }
bool serial_port_try_read(uint8_t *p)
{
    if (uart_tail == uart_head) { return false; }
    *p = uart[uart_tail++ % sizeof(uart)]; return true;
}
status_t serial_port_get_tx_status(void) { return STATUS_SUCCESS; }
status_t serial_port_send(const uint8_t *data, uint32_t length)
{
    assert(length < sizeof(last_reply)); memcpy(last_reply,data,length); last_reply[length] = '\0';
    return STATUS_SUCCESS;
}

static void run(unsigned ms)
{
    unsigned i; for (i=0U;i<ms;++i) { ++now; app_poll(); }
}
static void serial(const char *text)
{
    while (*text) { uart[uart_head++ % sizeof(uart)] = (uint8_t)*text++; }
    run(12U);
}
static void command(uint8_t counter, bool enable)
{
    uint8_t p[8] = {1U,counter,1U,0xC2U,0U,0U,0U,0U};
    p[4] = enable ? 1U : 0U; p[7] = can_protocol_crc8_sae_j1850(p,7U);
    (void)can_port_send(CAN_COMMAND_ID,p,8U); run(12U);
}
static void diag(const uint8_t *q, unsigned length)
{
    can_port_frame_t *f = &diag_queue[diag_head++ % 32U];
    memset(f,0,sizeof(*f)); f->id = TG_DIAG_REQUEST_ID; f->length=8U;
    f->data[0]=(uint8_t)length; memcpy(f->data+1U,q,length); run(2U);
}
static void reset(void)
{
    now=0U; adc=1737U; bus_off=false; pwm_result=STATUS_SUCCESS;
    command_head=command_tail=diag_head=diag_tail=0U;
    response_count=uart_head=uart_tail=0U;
    memset(&keys,0,sizeof(keys)); g_watchdog_feeds=0U;
    app_init(); run(20U);
}

static void test_arbitration(void)
{
    uint32_t feeds;
    reset(); CHECK(g_ecu_state==ECU_OFF);
    serial("ON\n"); CHECK(g_enable_request && g_ecu_state==ECU_RUNNING);
    serial("OFF\n"); CHECK(!g_enable_request && g_cooling_permille==0U);
    serial("SET 500\n"); CHECK(g_target_dC==500);
    serial("ON\nSET 600\n"); CHECK(g_target_dC==500);
    serial("OFF\n");
    serial("REMOTE\n"); CHECK(g_remote_mode && g_can_command_timeout && g_ecu_state==ECU_DEGRADED);
    command(0U,true); CHECK(g_enable_request && g_target_dC==450 && g_ecu_state==ECU_RUNNING);
    keys.key2_pressed=true; run(12U); CHECK(!g_enable_request);
    command(1U,true); CHECK(!g_enable_request);
    command(2U,false); command(3U,true); CHECK(g_enable_request);
    command(3U,true); CHECK(g_can_command_rejected==1U);
    run(500U); CHECK(g_can_command_timeout && g_ecu_state==ECU_DEGRADED && g_cooling_permille==1000U);
    command(9U,true); CHECK(g_enable_request && !g_can_command_timeout);
    bus_off=true; run(12U); CHECK(g_ecu_state==ECU_DEGRADED);
    serial("OFF\nLOCAL\n"); CHECK(!g_remote_mode && !g_enable_request);
    bus_off=false; run(12U); CHECK(g_ecu_state==ECU_OFF);
    feeds=g_watchdog_feeds;
    now+=50U; app_poll(); CHECK(g_scheduler_fault && g_ecu_state==ECU_FAILSAFE);
    run(100U); CHECK(g_watchdog_feeds==feeds);
}

static void test_diagnostic(void)
{
    static const uint8_t session[]={0x10U,3U};
    static const uint8_t target[]={0x2EU,0xF1U,0xA3U,1U,0xF4U};
    static const uint8_t vin[]={0x22U,0xF1U,0x90U};
    static const uint8_t clear[]={0x31U,1U,0xFFU,0U};
    uint8_t assembled[64];
    can_port_frame_t *fc;
    unsigned start, i, pos;
    reset(); diag(target,sizeof(target));
    CHECK(responses[0][1]==0x7FU && responses[0][3]==0x7FU);
    diag(session,sizeof(session)); diag(target,sizeof(target)); CHECK(g_target_dC==500);
    serial("ON\n"); diag(target,sizeof(target));
    CHECK(responses[response_count-1U][3]==0x22U);
    serial("OFF\n");
    start=response_count; diag(vin,sizeof(vin));
    CHECK(responses[start][0]==0x10U && responses[start][1]==20U);
    memcpy(assembled,responses[start]+2U,6U); pos=6U;
    fc=&diag_queue[diag_head++ % 32U]; memset(fc,0,sizeof(*fc));
    fc->length=8U; fc->id=TG_DIAG_REQUEST_ID; fc->data[0]=0x30U; fc->data[2]=5U;
    run(30U);
    for(i=start+1U;i<response_count;++i) { memcpy(assembled+pos,responses[i]+1U,7U); pos+=7U; }
    CHECK(pos==20U && assembled[0]==0x62U && memcmp(assembled+3U,"TGSDK144000000001",17U)==0);
    adc=3800U; run(12U); CHECK(g_overtemp_latched && g_ecu_state==ECU_FAILSAFE);
    diag(clear,sizeof(clear)); CHECK(responses[response_count-1U][3]==0x22U);
    adc=1737U; run(12U); diag(clear,sizeof(clear)); run(12U);
    CHECK(!g_overtemp_latched && g_ecu_state==ECU_OFF);
    CHECK(g_uds_requests==7U);
    pwm_result=STATUS_ERROR; run(12U); CHECK(g_scheduler_fault);
}

static void test_test_trigger(void)
{
    reset(); g_can_command_test_request=true; run(3U);
    CHECK(!g_can_command_test_request && g_can_command_valid_count==1U && g_can_command_accepted==1U);
    g_can_command_test_bad_crc=true; g_can_command_test_request=true; run(3U);
    CHECK(g_can_command_invalid_count==1U && g_can_command_accepted==1U);
}

int main(void)
{
    test_arbitration(); test_diagnostic(); test_test_trigger();
    printf("PASS: %u application integration assertions (real app + UART parser + CAN/UDS core)\n",assertions);
    return 0;
}
