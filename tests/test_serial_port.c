#include "serial_port.h"
#include "lpuart_driver.h"
#include <assert.h>
#include <stdio.h>
#include <stddef.h>

int lpuart1_State, lpuart1_InitConfig0;
static uart_callback_t callback;
static uint8_t *rx;
static unsigned starts, aborts, irq_depth;
static bool fail_buffer, fail_start;

status_t LPUART_DRV_Init(uint32_t instance, void *state, const void *config)
{ (void)instance; (void)state; (void)config; return STATUS_SUCCESS; }
status_t LPUART_DRV_SetRxBuffer(uint32_t instance, uint8_t *buffer, uint32_t length)
{ (void)instance; assert(length==1U); rx=buffer; return fail_buffer ? STATUS_ERROR : STATUS_SUCCESS; }
uart_callback_t LPUART_DRV_InstallRxCallback(uint32_t instance, uart_callback_t cb, void *user)
{ (void)instance; (void)user; callback=cb; return NULL; }
status_t LPUART_DRV_ReceiveData(uint32_t instance, uint8_t *buffer, uint32_t length)
{ (void)instance; assert(length==1U); rx=buffer; ++starts; return fail_start ? STATUS_ERROR : STATUS_SUCCESS; }
status_t LPUART_DRV_AbortReceivingData(uint32_t instance)
{ (void)instance; ++aborts; return STATUS_SUCCESS; }
status_t LPUART_DRV_SendData(uint32_t instance, const uint8_t *buffer, uint32_t length)
{ (void)instance; assert(buffer!=NULL && length!=0U); return STATUS_SUCCESS; }
status_t LPUART_DRV_GetTransmitStatus(uint32_t instance, uint32_t *remaining)
{ (void)instance; (void)remaining; return STATUS_SUCCESS; }
void INT_SYS_DisableIRQGlobal(void) { ++irq_depth; }
void INT_SYS_EnableIRQGlobal(void) { assert(irq_depth!=0U); --irq_depth; }
static void receive(uint8_t c) { *rx=c; callback(NULL,UART_EVENT_RX_FULL,NULL); }

int main(void)
{
    uint8_t byte;
    unsigned i;
    assert(serial_port_init()==STATUS_SUCCESS);
    assert(serial_port_start_receive()==STATUS_SUCCESS);
    receive('A'); receive('B');
    assert(serial_port_try_read(&byte) && byte=='A');
    assert(serial_port_try_read(&byte) && byte=='B');
    assert(!serial_port_try_read(&byte));
    for(i=0U;i<128U;++i) { receive((uint8_t)i); }
    assert(!serial_port_try_read(&byte));
    assert(serial_port_take_rx_overflow() && !serial_port_take_rx_overflow());
    assert(!serial_port_try_read(&byte) && irq_depth==0U);
    receive('C'); assert(serial_port_try_read(&byte) && byte=='C');
    callback(NULL,UART_EVENT_ERROR,NULL);
    assert(serial_port_take_rx_overflow());
    fail_start=true; serial_port_poll(); assert(aborts==1U);
    fail_start=false; serial_port_poll(); assert(aborts==2U && starts==3U);
    serial_port_poll(); assert(aborts==2U);
    fail_buffer=true; receive('D'); assert(serial_port_take_rx_overflow());
    fail_buffer=false; serial_port_poll(); assert(aborts==3U);
    assert(serial_port_send(NULL,1U)==STATUS_ERROR);
    assert(serial_port_send((const uint8_t *)"A",0U)==STATUS_ERROR);
    puts("PASS: serial driver adapter queue overflow, error recovery and restart retry");
    return 0;
}
