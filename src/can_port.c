#include "can_port.h"
#include "can_pal1.h"
#include <stddef.h>

#define CAN_RX_BUFFER  0U
#define CAN_TX_BUFFER  1U
#define CAN_TEST_ID    0x123U
#define CAN_CLASSIC_MAX_LENGTH  8U
#define CAN_COMMAND_RX_BUFFER   2U
#define CAN_COMMAND_ID          0x201U

static can_message_t s_can_rx_message;
static can_message_t s_can_app_tx_message;
static can_message_t s_can_command_rx_message;
static bool s_can_command_rx_started = false;

static const can_message_t s_can_tx_message = {
		.cs = 0U,
		.id = CAN_TEST_ID,
		.data = {
				0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U
		},
		.length = 8U
};

static bool s_can_test_started = false;

volatile status_t g_can_rx_status = STATUS_BUSY;
volatile status_t g_can_tx_status = STATUS_BUSY;
volatile bool g_can_loopback_done = false;
volatile bool g_can_loopback_ok = false;
volatile uint32_t g_can_rx_id = 0U;
volatile uint8_t g_can_rx_length = 0U;
volatile uint8_t g_can_rx_data[8] = {0U};
volatile status_t g_can_command_rx_status = STATUS_SUCCESS;
volatile uint32_t g_can_command_rx_count = 0U;
volatile uint32_t g_can_command_rx_id = 0U;
volatile uint8_t g_can_command_rx_length = 0U;
volatile uint8_t g_can_command_rx_data[8] = {0U};

static const can_buff_config_t s_can_buffer_config = {
		.enableBRS = false,
		.enableFD = false,
		.fdPadding = 0U,
		.idType = CAN_MSG_ID_STD,
		.isRemote = false
};

status_t can_port_init(void)
{
    status_t status;

    status = CAN_Init(&can_pal1_instance,
                      &can_pal1_Config0);
    if (status != STATUS_SUCCESS) {
        return status;
    }

    status = CAN_ConfigRxBuff(&can_pal1_instance, CAN_RX_BUFFER, &s_can_buffer_config, CAN_TEST_ID);
    if (status != STATUS_SUCCESS) {
        return status;
    }

    status = CAN_ConfigTxBuff(&can_pal1_instance, CAN_TX_BUFFER, &s_can_buffer_config);
    if (status != STATUS_SUCCESS) {
        return status;
    }

    return CAN_ConfigRxBuff(&can_pal1_instance, CAN_COMMAND_RX_BUFFER, &s_can_buffer_config, CAN_COMMAND_ID);
}

status_t can_port_start_loopback_test(void)
{
    status_t status;
    uint8_t i;

    s_can_test_started = false;
    g_can_loopback_done = false;
    g_can_loopback_ok = false;
    g_can_rx_id = 0U;
    g_can_rx_length = 0U;

    for (i = 0U; i < 8U; i++) {
        g_can_rx_data[i] = 0U;
    }

    status = CAN_Receive(&can_pal1_instance, CAN_RX_BUFFER, &s_can_rx_message);
    if (status != STATUS_SUCCESS) {
        g_can_rx_status = status;
        return status;
    }
    g_can_rx_status = STATUS_BUSY;

    status = CAN_Send(&can_pal1_instance, CAN_TX_BUFFER, &s_can_tx_message);
    if (status != STATUS_SUCCESS) {
        g_can_tx_status = status;
        return status;
    }

    g_can_tx_status = STATUS_BUSY;
    s_can_test_started = true;

    return STATUS_SUCCESS;
}

static void can_port_poll_loopback_test(void)
{
    uint8_t i;
    bool data_ok = true;

    if ((!s_can_test_started) || g_can_loopback_done) {
        return;
    }

    g_can_rx_status = CAN_GetTransferStatus(&can_pal1_instance,
                                            CAN_RX_BUFFER);
    g_can_tx_status = CAN_GetTransferStatus(&can_pal1_instance,
                                            CAN_TX_BUFFER);

    if ((g_can_rx_status == STATUS_BUSY) ||
        (g_can_tx_status == STATUS_BUSY)) {
        return;
    }

    g_can_loopback_done = true;

    if ((g_can_rx_status != STATUS_SUCCESS) ||
        (g_can_tx_status != STATUS_SUCCESS)) {
        return;
    }

    g_can_rx_id = s_can_rx_message.id;
    g_can_rx_length = s_can_rx_message.length;

    for (i = 0U; i < CAN_CLASSIC_MAX_LENGTH; i++) {
        g_can_rx_data[i] = s_can_rx_message.data[i];

        if (s_can_rx_message.data[i] !=
            s_can_tx_message.data[i]) {
            data_ok = false;
        }
    }

    g_can_loopback_ok =
        (s_can_rx_message.id == CAN_TEST_ID) &&
        (s_can_rx_message.length == CAN_CLASSIC_MAX_LENGTH) &&
        data_ok;
}

status_t can_port_send(uint32_t id, const uint8_t *data, uint8_t length)
{
	status_t status;
	uint8_t i;

	if((data == NULL) || (length > CAN_CLASSIC_MAX_LENGTH)){
		return STATUS_ERROR;
	}

	status = CAN_GetTransferStatus(&can_pal1_instance, CAN_TX_BUFFER);

	if(status != STATUS_SUCCESS){
		return status;
	}

	s_can_app_tx_message.cs = 0U;
	s_can_app_tx_message.id = id;
	s_can_app_tx_message.length = length;

	for(i = 0U; i < length; i++){
		s_can_app_tx_message.data[i] = data[i];
	}

	return CAN_Send(&can_pal1_instance, CAN_TX_BUFFER, &s_can_app_tx_message);
}

status_t can_port_start_command_receive(void)
{
    status_t status;
    uint8_t i;

    s_can_command_rx_started = false;
    g_can_command_rx_count = 0U;
    g_can_command_rx_id = 0U;
    g_can_command_rx_length = 0U;

    for (i = 0U; i < CAN_CLASSIC_MAX_LENGTH; i++) {
        g_can_command_rx_data[i] = 0U;
    }

    status = CAN_Receive(&can_pal1_instance, CAN_COMMAND_RX_BUFFER, &s_can_command_rx_message);

    if (status == STATUS_SUCCESS) {
        s_can_command_rx_started = true;
        g_can_command_rx_status = STATUS_BUSY;
    } else {
        g_can_command_rx_status = status;
    }

    return status;
}

static void can_port_poll_command_receive(void)
{
    status_t status;
    uint8_t i;
    uint8_t copy_length;

    if (!s_can_command_rx_started) {
        return;
    }

    status = CAN_GetTransferStatus(&can_pal1_instance,
                                   CAN_COMMAND_RX_BUFFER);
    g_can_command_rx_status = status;

    if (status == STATUS_BUSY) {
        return;
    }

    if (status != STATUS_SUCCESS) {
        s_can_command_rx_started = false;
        return;
    }

    g_can_command_rx_id = s_can_command_rx_message.id;

    copy_length = s_can_command_rx_message.length;
    if (copy_length > CAN_CLASSIC_MAX_LENGTH) {
        copy_length = CAN_CLASSIC_MAX_LENGTH;
    }

    g_can_command_rx_length = copy_length;

    for (i = 0U; i < CAN_CLASSIC_MAX_LENGTH; i++) {
        g_can_command_rx_data[i] = 0U;
    }

    for (i = 0U; i < copy_length; i++) {
        g_can_command_rx_data[i] =
            s_can_command_rx_message.data[i];
    }

    ++g_can_command_rx_count;

    status = CAN_Receive(&can_pal1_instance,
                         CAN_COMMAND_RX_BUFFER,
                         &s_can_command_rx_message);

    if (status == STATUS_SUCCESS) {
        g_can_command_rx_status = STATUS_BUSY;
    } else {
        g_can_command_rx_status = status;
        s_can_command_rx_started = false;
    }
}

void can_port_poll(void)
{
    can_port_poll_loopback_test();
    can_port_poll_command_receive();
}








