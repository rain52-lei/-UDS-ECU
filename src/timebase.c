#include "timebase.h"
#include "interrupt_manager.h"
#include "lpit1.h"

/* Kept visible for debugger observation. Written only by the LPIT ISR. */
volatile uint32_t g_ms = 0U;

static void lpit_ch0_isr(void)
{
    LPIT_DRV_ClearInterruptFlagTimerChannels(INST_LPIT1, 1U);
    ++g_ms;
}

status_t timebase_init(void)
{
    status_t status;

    LPIT_DRV_Init(INST_LPIT1, &lpit1_InitConfig);
    g_ms = 0U;
    INT_SYS_InstallHandler(LPIT0_Ch0_IRQn, lpit_ch0_isr, (isr_t *)0);

    status = LPIT_DRV_InitChannel(INST_LPIT1, 0U, &lpit1_ChnConfig0);
    if (status != STATUS_SUCCESS) {
        return status;
    }

    LPIT_DRV_StartTimerChannels(INST_LPIT1, 1U);
    return STATUS_SUCCESS;
}

uint32_t timebase_ms(void)
{
    return g_ms;
}
