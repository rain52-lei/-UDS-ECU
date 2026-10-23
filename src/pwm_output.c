#include "pwm_output.h"
#include "flexTimer_pwm1.h"
#include "ftm_common.h"
#include "ftm_pwm_driver.h"

static ftm_state_t s_ftm0_state;

status_t pwm_output_init(void)
{
	status_t status;

	status = FTM_DRV_Init(INST_FLEXTIMER_PWM1, &flexTimer_pwm1_InitConfig,&s_ftm0_state);

	if(status != STATUS_SUCCESS){
		return status;
	}

	return FTM_DRV_InitPwm(INST_FLEXTIMER_PWM1, &flexTimer_pwm1_PwmConfig);
}

status_t pwm_output_set_permille(uint16_t duty_permille)
{
	uint16_t duty;

	if(duty_permille > 1000U){
		duty_permille = 1000U;
	}

	duty = (uint16_t)((uint32_t)duty_permille * 32768U / 1000U);

	return FTM_DRV_UpdatePwmChannel(INST_FLEXTIMER_PWM1, 0U, FTM_PWM_UPDATE_IN_DUTY_CYCLE, duty, 0U, true);
}






