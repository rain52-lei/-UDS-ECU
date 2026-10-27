#include "serial_port.h"
#include "lpuart1.h"
#include "lpuart_driver.h"
#include "interrupt_manager.h"
#include <stddef.h>

#define SERIAL_RX_QUEUE_SIZE 128U
static volatile uint8_t s_queue[SERIAL_RX_QUEUE_SIZE];
static volatile uint32_t s_head, s_tail;
static volatile bool s_overflow, s_restart;
static uint8_t s_rx_byte;

volatile uint8_t g_uart_rx_last;
volatile uint32_t g_uart_rx_count, g_uart_rx_overflows, g_uart_rx_recoveries;
volatile uint32_t g_uart_rx_errors, g_uart_rx_restarts;

static void store_byte(uint8_t data)
{
    uint32_t next = (s_head + 1U) % SERIAL_RX_QUEUE_SIZE;
    if (s_overflow || next == s_tail) {
        s_overflow = true;
        ++g_uart_rx_overflows;
        return;
    }
    s_queue[s_head] = data;
    s_head = next;
}

static void rx_callback(void *state, uart_event_t event, void *user)
{
    (void)state;
    (void)user;
    if (event == UART_EVENT_RX_FULL) {
        g_uart_rx_last = s_rx_byte;
        ++g_uart_rx_count;
        store_byte(s_rx_byte);
        if (LPUART_DRV_SetRxBuffer(INST_LPUART1, &s_rx_byte, 1U) != STATUS_SUCCESS) {
            s_restart = true;
            s_overflow = true;
            ++g_uart_rx_errors;
        }
    } else if (event == UART_EVENT_ERROR) {
        /* 丢字节后整行不再可信：先标记重同步，再由主循环重新挂接。 */
        s_overflow = true;
        s_restart = true;
        ++g_uart_rx_errors;
    }
}

status_t serial_port_init(void)
{
    s_head = s_tail = 0U;
    s_overflow = s_restart = false;
    g_uart_rx_count = g_uart_rx_overflows = g_uart_rx_recoveries = 0U;
    g_uart_rx_errors = g_uart_rx_restarts = 0U;
    return LPUART_DRV_Init(INST_LPUART1, &lpuart1_State, &lpuart1_InitConfig0);
}

status_t serial_port_start_receive(void)
{
    (void)LPUART_DRV_InstallRxCallback(INST_LPUART1, rx_callback, NULL);
    return LPUART_DRV_ReceiveData(INST_LPUART1, &s_rx_byte, 1U);
}

void serial_port_poll(void)
{
    if (!s_restart) { return; }
    (void)LPUART_DRV_AbortReceivingData(INST_LPUART1);
    s_restart = false;
    if (LPUART_DRV_ReceiveData(INST_LPUART1, &s_rx_byte, 1U) == STATUS_SUCCESS) {
        ++g_uart_rx_restarts;
    } else { s_restart = true; }
}

status_t serial_port_send(const uint8_t *data, uint32_t length)
{
    if (data == NULL || length == 0U) { return STATUS_ERROR; }
    return LPUART_DRV_SendData(INST_LPUART1, data, length);
}

status_t serial_port_get_tx_status(void)
{
    return LPUART_DRV_GetTransmitStatus(INST_LPUART1, NULL);
}

bool serial_port_try_read(uint8_t *data)
{
    uint32_t tail = s_tail;
    if (data == NULL || s_overflow || tail == s_head) { return false; }
    *data = s_queue[tail];
    s_tail = (tail + 1U) % SERIAL_RX_QUEUE_SIZE;
    return true;
}

bool serial_port_take_rx_overflow(void)
{
    if (!s_overflow) { return false; }
    INT_SYS_DisableIRQGlobal();
    s_tail = s_head;
    s_overflow = false;
    ++g_uart_rx_recoveries;
    INT_SYS_EnableIRQGlobal();
    return true;
}
