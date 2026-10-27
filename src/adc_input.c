#include "timebase.h"
#include "adc_input.h"
#include "adConv1.h"
#include <stddef.h>

volatile uint32_t g_adc_init_done;

void adc_input_init(void)
{
    g_adc_init_done = 0U;
    ADC_DRV_ConfigConverter(INST_ADCONV1, &adConv1_ConvConfig0);
    ADC_DRV_AutoCalibration(INST_ADCONV1);
    g_adc_init_done = 1U;
}

bool adc_input_read(uint16_t *value)
{
    uint32_t start = timebase_ms();
    uint32_t budget = 100000U;
    if (value == NULL) { return false; }
    ADC_DRV_ConfigChan(INST_ADCONV1, 0U, &adConv1_ChnConfig0);
    while (!ADC_DRV_GetConvCompleteFlag(INST_ADCONV1, 0U)) {
        /* 毫秒时基异常时也要有有限轮询上限，避免永久卡在驱动等待中。 */
        if (timebase_ms() - start >= 2U || --budget == 0U) { return false; }
    }
    ADC_DRV_GetChanResult(INST_ADCONV1, 0U, value);
    return true;
}
