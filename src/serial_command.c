#include "serial_command.h"
#include "serial_port.h"
#include <string.h>

#define COMMAND_BUFFER_SIZE 32U
#define COMMAND_POLL_BUDGET 16U

typedef enum {
	REPLY_NONE = 0,
	REPLY_ON,
	REPLY_OFF,
	REPLY_ERROR
}serial_reply_t;

/* 用于调试观察的文本缓冲区 */
volatile char g_uart_line[COMMAND_BUFFER_SIZE];
volatile uint32_t g_uart_line_count = 0U;
volatile uint32_t g_uart_line_errors = 0U;
volatile status_t g_uart_reply_tx_status = STATUS_SUCCESS;

static uint32_t s_length = 0U;
static bool s_discard = false;
static serial_command_events_t s_events;
static const uint8_t s_reply_on[] = "ACK REQ=1\r\n";
static const uint8_t s_reply_off[] = "ACK REQ=0\r\n";
static const uint8_t s_reply_err[] = "ERROR CMD\r\n";

static serial_reply_t s_reply_type = REPLY_NONE;

static void serial_command_process_line(void)
{
	char line[COMMAND_BUFFER_SIZE];

	for(uint32_t i = 0U;i <= s_length;++i){
		line[i] = g_uart_line[i];
	}

	if(strcmp(line, "ON") == 0){
		s_events.enable_requested = true;
	}else if(strcmp(line, "OFF") == 0){
		s_events.disable_requested = true;
	}else{
		s_reply_type = REPLY_ERROR;
	}
}

void serial_command_init(void)
{
    s_length = 0U;
    s_discard = false;

    s_events.enable_requested = false;
    s_events.disable_requested = false;

    g_uart_line[0] = '\0';
    g_uart_line_count = 0U;
    g_uart_line_errors = 0U;

    s_reply_type = REPLY_NONE;

    g_uart_reply_tx_status = STATUS_SUCCESS;
}

void serial_command_poll(void)
{
    if (serial_port_take_rx_overflow()) {
        /* The old line may have a gap: discard through a fresh LF. */
        s_length = 0U;
        s_discard = true;
        g_uart_line[0] = '\0';
        ++g_uart_line_errors;
    }

    for (uint32_t i = 0U; i < COMMAND_POLL_BUDGET; ++i) {
        uint8_t byte;

        if (!serial_port_try_read(&byte)) {
            break;
        }

        if (byte == '\r') {
            continue;
        }

        if (byte == '\n') {
            if (!s_discard && s_length > 0U) {
                ++g_uart_line_count;
                serial_command_process_line();
            }

            s_length = 0U;
            s_discard = false;
            continue;
        }

        if (s_discard) {
            continue;
        }

        /* 只接受可打印ASCII字符，并保留字符串结束符位置 */
        if (byte < 32U || byte > 126U ||
            s_length >= COMMAND_BUFFER_SIZE - 1U) {
            s_discard = true;
            ++g_uart_line_errors;
            continue;
        }

        g_uart_line[s_length] = (char)byte;
        ++s_length;
        g_uart_line[s_length] = '\0';
    }
}

serial_command_events_t serial_command_take_events(void)
{
    serial_command_events_t events = s_events;

    s_events.enable_requested = false;
    s_events.disable_requested = false;

    return events;
}

void serial_command_set_reply(bool enable_request)
{
    if(enable_request){
    	s_reply_type = REPLY_ON;
    }else {
    	s_reply_type = REPLY_OFF;
    }
}

void serial_command_poll_reply(void)
{
    if (s_reply_type == REPLY_NONE) {
        return;
    }

    g_uart_reply_tx_status = serial_port_get_tx_status();

    if (g_uart_reply_tx_status != STATUS_SUCCESS) {
        return;
    }

    switch(s_reply_type){
        case REPLY_ON:
    	    g_uart_reply_tx_status = serial_port_send(s_reply_on, sizeof(s_reply_on) - 1U);
            break;
        case REPLY_OFF:
    	    g_uart_reply_tx_status = serial_port_send(s_reply_off, sizeof(s_reply_off) - 1U);
            break;
        case REPLY_ERROR:
        	g_uart_reply_tx_status = serial_port_send(s_reply_err, sizeof(s_reply_err) - 1U);
        	break;

        default:
        	return;
    }

    if (g_uart_reply_tx_status == STATUS_SUCCESS) {
        s_reply_type = REPLY_NONE;
    }
}
