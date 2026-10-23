#include "app.h"
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
#include <stdio.h>

#define APP_CONTROL_PERIOD_MS 10U
#define APP_TELEMETRY_PERIOD_MS 100U

/* Debug counters. No other module writes these variables. */
volatile uint32_t g_jobs_10ms = 0U;
volatile uint32_t g_skipped_periods = 0U;
volatile bool g_enable_request = false;
volatile uint16_t g_adc_raw = 0U;
volatile bool g_adc_valid = false;
volatile uint32_t g_adc_errors = 0U;
volatile int16_t g_temperature_dC = 0;
volatile int16_t g_temperature_filter_dC = 0;
volatile bool g_sensor_valid = false;
volatile bool g_sensor_fault = false;
volatile ecu_state_t g_ecu_state = ECU_OFF;
volatile bool g_overtemp_latched = false;
volatile uint16_t g_cooling_permille = 0U;
volatile status_t g_pwm_status = STATUS_SUCCESS;
volatile int16_t g_target_dC = 450;
volatile uint32_t g_job_100ms = 0U;
volatile status_t g_telemetry_tx_status = STATUS_SUCCESS;
volatile status_t g_can_status_tx_status = STATUS_SUCCESS;
volatile uint32_t g_can_status_tx_count = 0U;
volatile uint32_t g_can_status_tx_busy = 0U;

static uint32_t s_last_control_ms;
static uint32_t s_last_telemerty_ms;

uint8_t g_can_status_payload[CAN_STATUS_LENGTH] = {0U};

static void app_10ms(void)
{
	keys_events_t events = keys_sample_10ms();

	serial_command_events_t serial_events = serial_command_take_events();

	if(events.key2_pressed || serial_events.disable_requested){
		g_enable_request = false;
	}else if(serial_events.enable_requested){
		g_enable_request = true;
	}else if(events.key1_pressed){
		g_enable_request = !g_enable_request;
	}

	if(serial_events.enable_requested || serial_events.disable_requested){
		serial_command_set_reply(g_enable_request);
	}

	uint16_t raw;

	g_adc_valid = adc_input_read(&raw);

	if(g_adc_valid){
		g_adc_raw = raw;
	}else{
		++g_adc_errors;
	}

    g_sensor_valid = g_adc_valid && temperature_raw_valid(g_adc_raw);
    g_sensor_fault = faults_sensor_update(g_sensor_valid);

    if(g_sensor_valid){
    	g_temperature_dC = temperature_from_adc(g_adc_raw);
    	g_temperature_filter_dC = temperature_filter_update(g_temperature_dC);
    }else{
    	temperature_filter_reset();
    }

    g_overtemp_latched = faults_overtemp_update(g_sensor_valid, g_temperature_dC);
    g_ecu_state = ecu_state_evaluate(g_enable_request, g_sensor_fault, g_overtemp_latched);

    g_cooling_permille = control_step(g_ecu_state, g_temperature_filter_dC, g_target_dC);

    g_pwm_status = pwm_output_set_permille(g_cooling_permille);

    ++g_jobs_10ms;
}

static void app_100ms(void)
{
	static char message[96];

	can_protocol_pack_status(g_temperature_filter_dC,
	                         g_target_dC,
	                         g_cooling_permille,
	                         g_ecu_state,
	                         g_sensor_valid,
	                         g_sensor_fault,
	                         g_overtemp_latched,
	                         g_enable_request,
	                         g_can_status_payload);

	g_can_status_tx_status = can_port_send(CAN_STATUS_ID, g_can_status_payload, CAN_STATUS_LENGTH);

	if(g_can_status_tx_status == STATUS_SUCCESS){
		++g_can_status_tx_count;
	}else if(g_can_status_tx_status == STATUS_BUSY){
		++g_can_status_tx_busy;
	}

	g_telemetry_tx_status = serial_port_get_tx_status();
	if(g_telemetry_tx_status != STATUS_SUCCESS){
		return ;
	}

	int length = snprintf(message, sizeof(message), "T_dC = %d,SET_dC = %d,STATE = %u,PWM_pm = %u,VALID = %u\r\n"
			                                     ,  (int)g_temperature_filter_dC,
												    (int)g_target_dC,
													(unsigned)g_ecu_state,
													(unsigned)g_cooling_permille,
													(unsigned)g_sensor_valid);

	if(length < 0 || (size_t)length >= sizeof(message)){
		g_telemetry_tx_status = STATUS_ERROR;
		return ;
	}

	g_telemetry_tx_status = serial_port_send((const uint8_t*)message, (uint32_t)length);
}

void app_init(void)
{
    s_last_control_ms = timebase_ms();
    g_jobs_10ms = 0U;
    g_skipped_periods = 0U;
    g_enable_request = false;
    g_adc_raw = 0U;
    g_adc_valid = false;
    g_adc_errors = 0U;
    g_temperature_dC = 0;
    temperature_filter_reset();
    g_sensor_valid = false;
    faults_init();
    g_sensor_fault = false;
    g_ecu_state = ECU_OFF;
    g_overtemp_latched = false;
    g_pwm_status = STATUS_SUCCESS;
    g_cooling_permille = 0U;
    g_target_dC = 450;
    control_reset();
    s_last_telemerty_ms = s_last_control_ms;
    g_job_100ms = 0U;
    g_telemetry_tx_status = STATUS_SUCCESS;
    serial_command_init();
    g_can_status_tx_status = STATUS_SUCCESS;
    g_can_status_tx_count = 0U;
    g_can_status_tx_busy = 0U;
}

void app_poll(void)
{
    uint32_t now = timebase_ms();
    uint32_t elapsed = now - s_last_control_ms;

    if (elapsed >= APP_CONTROL_PERIOD_MS) {
        uint32_t periods = elapsed / APP_CONTROL_PERIOD_MS;

        g_skipped_periods += periods - 1U;
        s_last_control_ms += periods * APP_CONTROL_PERIOD_MS;
        app_10ms();
    }

    elapsed = now - s_last_telemerty_ms;

    if(elapsed >= APP_TELEMETRY_PERIOD_MS){
        uint32_t periods = elapsed / APP_TELEMETRY_PERIOD_MS;

        s_last_telemerty_ms += periods * APP_TELEMETRY_PERIOD_MS;
        ++g_job_100ms;
        app_100ms();
    }

    serial_command_poll();
    serial_command_poll_reply();
}
