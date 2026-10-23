#include "serial_port.h"
#include "lpuart1.h"
#include "lpuart_driver.h"
#include "interrupt_manager.h"
#include <stddef.h>

#define SERIAL_RX_QUEUE_SIZE     64U

static void serial_port_store_rx_byte(uint8_t data);

static volatile uint8_t s_rx_queue[SERIAL_RX_QUEUE_SIZE];
static volatile uint32_t s_rx_head = 0U;
static volatile uint32_t s_rx_tail = 0U;
static volatile bool s_rx_overflowed = false;

static uint8_t s_rx_byte;

volatile uint8_t g_uart_rx_last = 0U;
volatile uint32_t g_uart_rx_count = 0U;
volatile uint32_t g_uart_rx_overflows = 0U;
volatile uint32_t g_uart_rx_recoveries = 0U;

status_t serial_port_init(void)
{
    return LPUART_DRV_Init(INST_LPUART1,
                           &lpuart1_State,
                           &lpuart1_InitConfig0);
}

status_t serial_port_send(const uint8_t *data, uint32_t length)
{
    return LPUART_DRV_SendData(INST_LPUART1, data, length);
}

status_t serial_port_get_tx_status(void)
{
	return LPUART_DRV_GetTransmitStatus(INST_LPUART1, NULL);
}

static void serial_port_rx_callback(void *driverState, uart_event_t event, void *userData)
{
	(void)driverState;
	(void)userData;

	if(event == UART_EVENT_RX_FULL){
		g_uart_rx_last = s_rx_byte;
		++g_uart_rx_count;

		serial_port_store_rx_byte(s_rx_byte);

		(void)LPUART_DRV_SetRxBuffer(INST_LPUART1, &s_rx_byte, 1U);
	}
}


status_t serial_port_start_receive(void)
{
	(void)LPUART_DRV_InstallRxCallback(INST_LPUART1, serial_port_rx_callback, NULL);

	return LPUART_DRV_ReceiveData(INST_LPUART1, &s_rx_byte, 1U);
}

bool serial_port_try_read(uint8_t *data)
{
	uint32_t tail;

	if(data == NULL || s_rx_overflowed){
		return false;
	}

	tail = s_rx_tail;

	if(tail == s_rx_head){
		return false;
	}

	*data = s_rx_queue[tail];
	++tail;

	if(tail >= SERIAL_RX_QUEUE_SIZE){
		tail = 0U;
	}

    s_rx_tail = tail;
    return true;
}

bool serial_port_take_rx_overflow(void)
{
    /* Only the RX callback sets this flag; only the main loop clears it. */
    if (!s_rx_overflowed) {
        return false;
    }

    /* Keep the flush and event acknowledgement together. */
    INT_SYS_DisableIRQGlobal();
    s_rx_tail = s_rx_head;
    s_rx_overflowed = false;
    ++g_uart_rx_recoveries;
    INT_SYS_EnableIRQGlobal();

    return true;
}

static void serial_port_store_rx_byte(uint8_t data)
{
    /* Once bytes are lost, wait for the main loop to discard the old queue. */
    if (s_rx_overflowed) {
        ++g_uart_rx_overflows;
        return;
    }

    uint32_t next_head = s_rx_head + 1U;

    if (next_head >= SERIAL_RX_QUEUE_SIZE) {
        next_head = 0U;
    }

    if (next_head == s_rx_tail) {
        s_rx_overflowed = true;
        ++g_uart_rx_overflows;
        return;
    }

    s_rx_queue[s_rx_head] = data;
    s_rx_head = next_head;
}













