#include "serial_command.h"
#include "serial_port.h"
#include "app_config.h"
#include <string.h>

#define COMMAND_BUFFER_SIZE 32U
#define COMMAND_POLL_BUDGET 16U
typedef enum { REPLY_NONE, REPLY_ON, REPLY_OFF, REPLY_ERROR, REPLY_OK } serial_reply_t;

volatile char g_uart_line[COMMAND_BUFFER_SIZE];
volatile uint32_t g_uart_line_count;
volatile uint32_t g_uart_line_errors;
volatile status_t g_uart_reply_tx_status;
static uint32_t s_length;
static bool s_discard;
static serial_command_events_t s_events;
static serial_reply_t s_reply;
static const uint8_t s_reply_on[] = "ACK REQ=1\r\n";
static const uint8_t s_reply_off[] = "ACK REQ=0\r\n";
static const uint8_t s_reply_err[] = "ERROR CMD\r\n";
static const uint8_t s_reply_ok[] = "OK\r\n";

static void process_line(void)
{
    char line[COMMAND_BUFFER_SIZE];
    uint32_t i;
    for (i = 0U; i <= s_length; ++i) { line[i] = g_uart_line[i]; }
    if (strcmp(line, "ON") == 0) { s_events.enable_requested = true; }
    else if (strcmp(line, "OFF") == 0) { s_events.disable_requested = true; }
    else if (strcmp(line, "LOCAL") == 0) { s_events.local_requested = true; }
    else if (strcmp(line, "REMOTE") == 0) { s_events.remote_requested = true; }
    else if (strncmp(line, "SET ", 4U) == 0) {
        uint32_t target = 0U;
        if (s_length <= 4U || s_length > 8U) { s_reply = REPLY_ERROR; return; }
        for (i = 4U; i < s_length; ++i) {
            if (line[i] < '0' || line[i] > '9') { s_reply = REPLY_ERROR; return; }
            target = target * 10U + (uint32_t)(line[i] - '0');
        }
        if (target < TG_TARGET_MIN_DC || target > TG_TARGET_MAX_DC) {
            s_reply = REPLY_ERROR;
            return;
        }
        s_events.target_requested = true;
        s_events.target_dC = (int16_t)target;
    } else { s_reply = REPLY_ERROR; }
}

void serial_command_init(void)
{
    s_length = 0U;
    s_discard = false;
    memset(&s_events, 0, sizeof(s_events));
    g_uart_line[0] = '\0';
    g_uart_line_count = 0U;
    g_uart_line_errors = 0U;
    s_reply = REPLY_NONE;
    g_uart_reply_tx_status = STATUS_SUCCESS;
}

void serial_command_poll(void)
{
    uint32_t i;
    if (serial_port_take_rx_overflow()) {
        s_length = 0U;
        s_discard = true;
        g_uart_line[0] = '\0';
        ++g_uart_line_errors;
    }
    for (i = 0U; i < COMMAND_POLL_BUDGET; ++i) {
        uint8_t byte;
        if (!serial_port_try_read(&byte)) { break; }
        if (byte == '\r') { continue; }
        if (byte == '\n') {
            if (!s_discard && s_length > 0U) {
                ++g_uart_line_count;
                process_line();
            }
            s_length = 0U;
            s_discard = false;
            continue;
        }
        if (s_discard) { continue; }
        /* 出现非打印字符或超长行后，丢弃到下一个换行重新同步。 */
        if (byte < 32U || byte > 126U || s_length >= COMMAND_BUFFER_SIZE - 1U) {
            s_discard = true;
            ++g_uart_line_errors;
            continue;
        }
        g_uart_line[s_length++] = (char)byte;
        g_uart_line[s_length] = '\0';
    }
}

serial_command_events_t serial_command_take_events(void)
{
    serial_command_events_t events = s_events;
    memset(&s_events, 0, sizeof(s_events));
    return events;
}

void serial_command_set_reply(bool enabled)
{
    s_reply = enabled ? REPLY_ON : REPLY_OFF;
}

void serial_command_set_result(bool success)
{
    s_reply = success ? REPLY_OK : REPLY_ERROR;
}

void serial_command_poll_reply(void)
{
    const uint8_t *data;
    uint32_t length;
    if (s_reply == REPLY_NONE) { return; }
    g_uart_reply_tx_status = serial_port_get_tx_status();
    if (g_uart_reply_tx_status != STATUS_SUCCESS) { return; }
    switch (s_reply) {
    case REPLY_ON: data = s_reply_on; length = sizeof(s_reply_on) - 1U; break;
    case REPLY_OFF: data = s_reply_off; length = sizeof(s_reply_off) - 1U; break;
    case REPLY_OK: data = s_reply_ok; length = sizeof(s_reply_ok) - 1U; break;
    default: data = s_reply_err; length = sizeof(s_reply_err) - 1U; break;
    }
    g_uart_reply_tx_status = serial_port_send(data, length);
    if (g_uart_reply_tx_status == STATUS_SUCCESS) { s_reply = REPLY_NONE; }
}
