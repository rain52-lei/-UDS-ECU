#ifndef TEST_LPUART_DRIVER_H
#define TEST_LPUART_DRIVER_H
#include "status.h"
#include <stdint.h>
typedef enum { UART_EVENT_RX_FULL, UART_EVENT_ERROR } uart_event_t;
typedef void (*uart_callback_t)(void *, uart_event_t, void *);
status_t LPUART_DRV_Init(uint32_t instance, void *state, const void *config);
status_t LPUART_DRV_SetRxBuffer(uint32_t instance, uint8_t *buffer, uint32_t length);
uart_callback_t LPUART_DRV_InstallRxCallback(uint32_t instance, uart_callback_t callback, void *user);
status_t LPUART_DRV_ReceiveData(uint32_t instance, uint8_t *buffer, uint32_t length);
status_t LPUART_DRV_AbortReceivingData(uint32_t instance);
status_t LPUART_DRV_SendData(uint32_t instance, const uint8_t *buffer, uint32_t length);
status_t LPUART_DRV_GetTransmitStatus(uint32_t instance, uint32_t *remaining);
#endif
