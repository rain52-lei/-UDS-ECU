#include <stdint.h>
#include "timebase.h"
#include "adc_input.h"
#include "adConv1.h"

volatile uint32_t g_adc_init_done = 0U;

void adc_input_init(void)
{
	g_adc_init_done = 0U;

	ADC_DRV_ConfigConverter(INST_ADCONV1, &adConv1_ConvConfig0);
	ADC_DRV_AutoCalibration(INST_ADCONV1);

	g_adc_init_done = 1U;
}

bool adc_input_read(uint16_t *value)
{
	uint32_t start_ms = timebase_ms();

	ADC_DRV_ConfigChan(INST_ADCONV1, 0U, &adConv1_ChnConfig0);

	while(!ADC_DRV_GetConvCompleteFlag(INST_ADCONV1,0)){
		if((timebase_ms() - start_ms) >= 2U){
			return false;
		}
	}
	ADC_DRV_GetChanResult(INST_ADCONV1,0U,value);
	return true;
}
