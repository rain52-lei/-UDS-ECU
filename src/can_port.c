#include "can_port.h"
#include "app_config.h"
#include "can_protocol.h"
#include "can_pal1.h"
#include "flexcan_driver.h"
#include "pins_driver.h"
#include <stddef.h>
#include <string.h>

#define RX_TEST 0U
#define TX_APP 1U
#define RX_COMMAND 2U
#define RX_DIAGNOSTIC 3U
#define TX_DIAGNOSTIC 4U
#define RX_QUEUE_CAPACITY 16U
#define TEST_ID 0x123U

typedef struct {
    can_message_t message;
    can_port_frame_t frames[RX_QUEUE_CAPACITY];
    uint8_t head, tail, count;
    uint8_t mailbox;
    uint32_t id;
    bool armed;
} can_rx_channel_t;

static can_rx_channel_t s_command;
static can_rx_channel_t s_diagnostic;
static can_user_config_t s_runtime_config;
static can_message_t s_test_rx;
static can_message_t s_app_tx;
static can_message_t s_diag_tx;
static bool s_test_started;
static const can_message_t s_test_tx = {
    .cs = 0U, .id = TEST_ID,
    .data = {0x10U, 0x11U, 0x12U, 0x13U, 0x14U, 0x15U, 0x16U, 0x17U},
    .length = 8U
};
static const can_buff_config_t s_buffer_config = {
    .enableBRS = false, .enableFD = false, .fdPadding = 0U,
    .idType = CAN_MSG_ID_STD, .isRemote = false
};

/* 板厂综合例程确认：PTE4=CAN0_RX，PTE5=CAN0_TX，均为ALT5。 */
static const pin_settings_config_t s_can_pins[2] = {
    {.base = PORTE, .pinPortIdx = 4U, .pullConfig = PORT_INTERNAL_PULL_NOT_ENABLED,
     .passiveFilter = false, .driveSelect = PORT_LOW_DRIVE_STRENGTH,
     .mux = PORT_MUX_ALT5, .pinLock = false, .intConfig = PORT_DMA_INT_DISABLED,
     .clearIntFlag = false, .gpioBase = NULL, .digitalFilter = false},
    {.base = PORTE, .pinPortIdx = 5U, .pullConfig = PORT_INTERNAL_PULL_NOT_ENABLED,
     .passiveFilter = false, .driveSelect = PORT_LOW_DRIVE_STRENGTH,
     .mux = PORT_MUX_ALT5, .pinLock = false, .intConfig = PORT_DMA_INT_DISABLED,
     .clearIntFlag = false, .gpioBase = NULL, .digitalFilter = false}
};

volatile status_t g_can_rx_status = STATUS_BUSY;
volatile status_t g_can_tx_status = STATUS_BUSY;
volatile bool g_can_loopback_done;
volatile bool g_can_loopback_ok;
volatile uint32_t g_can_rx_id;
volatile uint8_t g_can_rx_length;
volatile uint8_t g_can_rx_data[8];
volatile status_t g_can_command_rx_status;
volatile uint32_t g_can_command_rx_count;
volatile uint32_t g_can_command_rx_id;
volatile uint8_t g_can_command_rx_length;
volatile uint8_t g_can_command_rx_data[8];
volatile uint32_t g_can_command_rx_dropped;
volatile status_t g_can_diag_rx_status;
volatile uint32_t g_can_diag_rx_count;
volatile uint32_t g_can_diag_rx_dropped;
volatile uint32_t g_can_receive_errors;
volatile uint32_t g_can_error_status;

static status_t configure_rx(can_rx_channel_t *channel, uint8_t mailbox, uint32_t id)
{
    status_t status;
    memset(channel, 0, sizeof(*channel));
    channel->mailbox = mailbox;
    channel->id = id;
    status = CAN_ConfigRxBuff(&can_pal1_instance, mailbox, &s_buffer_config, id);
    if (status == STATUS_SUCCESS) {
        /* 标准ID完整比较，禁止诊断帧和控制帧相互串入。 */
        status = CAN_SetRxFilter(&can_pal1_instance, CAN_MSG_ID_STD, mailbox, 0x7FFU);
    }
    return status;
}

status_t can_port_init(void)
{
    status_t status;
    s_runtime_config = can_pal1_Config0;
    s_runtime_config.mode = TG_CAN_LOOPBACK ? CAN_LOOPBACK_MODE : CAN_NORMAL_MODE;
    s_test_started = false;
    g_can_loopback_done = false;
    g_can_loopback_ok = false;
    g_can_command_rx_count = 0U;
    g_can_diag_rx_count = 0U;
    g_can_command_rx_dropped = 0U;
    g_can_diag_rx_dropped = 0U;
    g_can_receive_errors = 0U;
    status = PINS_DRV_Init(2U, s_can_pins);
    if (status != STATUS_SUCCESS) { return status; }
    status = CAN_Init(&can_pal1_instance, &s_runtime_config);
    if (status != STATUS_SUCCESS) { return status; }
    status = CAN_ConfigRxBuff(&can_pal1_instance, RX_TEST, &s_buffer_config, TEST_ID);
    if (status != STATUS_SUCCESS) { return status; }
    status = CAN_SetRxFilter(&can_pal1_instance, CAN_MSG_ID_STD, RX_TEST, 0x7FFU);
    if (status != STATUS_SUCCESS) { return status; }
    status = CAN_ConfigTxBuff(&can_pal1_instance, TX_APP, &s_buffer_config);
    if (status != STATUS_SUCCESS) { return status; }
    status = CAN_ConfigTxBuff(&can_pal1_instance, TX_DIAGNOSTIC, &s_buffer_config);
    if (status != STATUS_SUCCESS) { return status; }
    status = configure_rx(&s_command, RX_COMMAND, CAN_COMMAND_ID);
    if (status != STATUS_SUCCESS) { return status; }
    return configure_rx(&s_diagnostic, RX_DIAGNOSTIC, TG_DIAG_REQUEST_ID);
}

status_t can_port_start_loopback_test(void)
{
    status_t status;
    if (!TG_CAN_LOOPBACK) {
        return STATUS_SUCCESS;
    }
    status = CAN_Receive(&can_pal1_instance, RX_TEST, &s_test_rx);
    if (status != STATUS_SUCCESS) { return status; }
    g_can_rx_status = STATUS_BUSY;
    status = CAN_Send(&can_pal1_instance, TX_APP, &s_test_tx);
    if (status != STATUS_SUCCESS) { return status; }
    s_test_started = true;
    g_can_tx_status = STATUS_BUSY;
    return STATUS_SUCCESS;
}

bool can_port_loopback_ready(void)
{
    return !TG_CAN_LOOPBACK || (g_can_loopback_done && g_can_loopback_ok);
}

static status_t arm(can_rx_channel_t *channel)
{
    status_t status = CAN_Receive(&can_pal1_instance, channel->mailbox, &channel->message);
    channel->armed = status == STATUS_SUCCESS;
    return status;
}

status_t can_port_start_command_receive(void)
{
    status_t status = arm(&s_command);
    g_can_command_rx_status = status == STATUS_SUCCESS ? STATUS_BUSY : status;
    if (status != STATUS_SUCCESS) { return status; }
    status = arm(&s_diagnostic);
    g_can_diag_rx_status = status == STATUS_SUCCESS ? STATUS_BUSY : status;
    return status;
}

static status_t poll_rx(can_rx_channel_t *channel, volatile uint32_t *received,
                         volatile uint32_t *dropped)
{
    status_t status;
    can_port_frame_t frame;
    if (!channel->armed) {
        status = arm(channel);
        return status == STATUS_SUCCESS ? STATUS_BUSY : status;
    }
    status = CAN_GetTransferStatus(&can_pal1_instance, channel->mailbox);
    if (status == STATUS_BUSY) { return status; }
    channel->armed = false;
    if (status != STATUS_SUCCESS) {
        ++g_can_receive_errors;
        (void)CAN_AbortTransfer(&can_pal1_instance, channel->mailbox);
        return status;
    }
    if (channel->message.id != channel->id || channel->message.length > 8U ||
        (channel->message.cs & (CAN_WMBn_CS_IDE_MASK | CAN_WMBn_CS_RTR_MASK)) != 0U) {
        ++g_can_receive_errors;
    } else {
        memset(&frame, 0, sizeof(frame));
        frame.id = channel->message.id;
        frame.length = (uint8_t)channel->message.length;
        memcpy(frame.data, channel->message.data, frame.length);
        ++*received;
        if (channel->count < RX_QUEUE_CAPACITY) {
            channel->frames[channel->head] = frame;
            channel->head = (uint8_t)((channel->head + 1U) % RX_QUEUE_CAPACITY);
            ++channel->count;
        } else {
            ++*dropped;
        }
        if (channel == &s_command) {
            unsigned i;
            g_can_command_rx_id = frame.id;
            g_can_command_rx_length = frame.length;
            for (i = 0U; i < 8U; ++i) { g_can_command_rx_data[i] = frame.data[i]; }
        }
    }
    status = arm(channel);
    return status == STATUS_SUCCESS ? STATUS_BUSY : status;
}

static void poll_test(void)
{
    unsigned i;
    if (!s_test_started || g_can_loopback_done) { return; }
    g_can_rx_status = CAN_GetTransferStatus(&can_pal1_instance, RX_TEST);
    g_can_tx_status = CAN_GetTransferStatus(&can_pal1_instance, TX_APP);
    if (g_can_rx_status == STATUS_BUSY || g_can_tx_status == STATUS_BUSY) { return; }
    g_can_loopback_done = true;
    g_can_loopback_ok = g_can_rx_status == STATUS_SUCCESS &&
        g_can_tx_status == STATUS_SUCCESS && s_test_rx.id == TEST_ID && s_test_rx.length == 8U;
    g_can_rx_id = s_test_rx.id;
    g_can_rx_length = (uint8_t)s_test_rx.length;
    for (i = 0U; i < 8U; ++i) {
        g_can_rx_data[i] = s_test_rx.data[i];
        if (s_test_rx.data[i] != s_test_tx.data[i]) { g_can_loopback_ok = false; }
    }
}

bool can_port_bus_off(void)
{
    g_can_error_status = FLEXCAN_DRV_GetErrorStatus(0U);
    return (g_can_error_status & CAN_ESR1_FLTCONF_MASK) >= CAN_ESR1_FLTCONF(2U);
}

void can_port_poll(void)
{
    poll_test();
    g_can_command_rx_status = poll_rx(&s_command, &g_can_command_rx_count,
                                     &g_can_command_rx_dropped);
    g_can_diag_rx_status = poll_rx(&s_diagnostic, &g_can_diag_rx_count,
                                  &g_can_diag_rx_dropped);
}

static status_t send(uint8_t mailbox, can_message_t *message,
                      uint32_t id, const uint8_t *data, uint8_t length)
{
    status_t status;
    if (data == NULL || length > 8U || id > 0x7FFU) { return STATUS_ERROR; }
    if (!can_port_loopback_ready() || can_port_bus_off()) { return STATUS_BUSY; }
    status = CAN_GetTransferStatus(&can_pal1_instance, mailbox);
    if (status != STATUS_SUCCESS) { return status; }
    memset(message, 0, sizeof(*message));
    message->id = id;
    message->length = length;
    memcpy(message->data, data, length);
    return CAN_Send(&can_pal1_instance, mailbox, message);
}

status_t can_port_send(uint32_t id, const uint8_t *data, uint8_t length)
{
    return send(TX_APP, &s_app_tx, id, data, length);
}

status_t can_port_send_diagnostic(const uint8_t data[8])
{
    return send(TX_DIAGNOSTIC, &s_diag_tx, TG_DIAG_RESPONSE_ID, data, 8U);
}

static bool take(can_rx_channel_t *channel, can_port_frame_t *frame)
{
    if (frame == NULL || channel->count == 0U) { return false; }
    *frame = channel->frames[channel->tail];
    channel->tail = (uint8_t)((channel->tail + 1U) % RX_QUEUE_CAPACITY);
    --channel->count;
    return true;
}

bool can_port_take_command_frame(can_port_frame_t *frame)
{
    return take(&s_command, frame);
}

bool can_port_take_diagnostic_frame(can_port_frame_t *frame)
{
    return take(&s_diagnostic, frame);
}
