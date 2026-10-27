#include "watchdog_port.h"
#include "app_config.h"
#include "device_registers.h"

volatile uint32_t g_reset_cause;
volatile uint32_t g_watchdog_feeds;
volatile bool g_watchdog_test_stop;

bool watchdog_port_init(void)
{
    g_reset_cause = RCM->SRS;
    g_watchdog_feeds = 0U;
    g_watchdog_test_stop = false;
#if TG_WATCHDOG_ENABLE
    uint32_t primask;
    uint32_t retries = 100000U;
    __asm__ volatile ("mrs %0, primask\n\tcpsid i" : "=r" (primask) : : "memory");
    WDOG->CNT = FEATURE_WDOG_UNLOCK_VALUE;
    (void)WDOG->CNT;
    while ((WDOG->CS & WDOG_CS_ULK_MASK) == 0U && retries != 0U) { --retries; }
    if (retries == 0U) {
        __asm__ volatile ("msr primask, %0" : : "r" (primask) : "memory");
        return false;
    }
    /* LPO128kHz /256 =500Hz；500计数约1秒。调试暂停时冻结。 */
    WDOG->TOVAL = 500U;
    WDOG->WIN = 0U;
    WDOG->CS = WDOG_CS_CMD32EN_MASK | WDOG_CS_UPDATE_MASK |
               WDOG_CS_EN_MASK | WDOG_CS_CLK(1U) | WDOG_CS_PRES_MASK;
    while ((WDOG->CS & WDOG_CS_RCS_MASK) == 0U && retries != 0U) {
        --retries;
    }
    __asm__ volatile ("msr primask, %0" : : "r" (primask) : "memory");
    return retries != 0U;
#else
    return true;
#endif
}

void watchdog_port_refresh(void)
{
    if (g_watchdog_test_stop) {
        return;
    }
#if TG_WATCHDOG_ENABLE
    WDOG->CNT = FEATURE_WDOG_TRIGGER_VALUE;
#endif
    ++g_watchdog_feeds;
}
