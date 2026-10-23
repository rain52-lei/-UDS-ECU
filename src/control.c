#include "control.h"

#define COUNTROL_BASE_PERMILE  200
#define COUNTROL_KP_PER_DC     2
#define COUNTROL_I_DIVISOR     100
#define COUNTROL_I_LIMIT       200000
#define COUNTROL_SLEW_STEP     20

static int32_t s_integral = 0;
static int32_t s_output_permille = 0;

uint16_t control_step(ecu_state_t state, int16_t temperature_dC, int16_t target_dC)
{
	int32_t error;
	int32_t output;
	int32_t proposal;

    if(state == ECU_OFF){
    	control_reset();
    	return 0U;
    }

    if(state != ECU_RUNNING){
    	s_integral = 0;
    	s_output_permille = 1000;
    	return 1000U;
    }

    error = (int32_t)temperature_dC - target_dC;
    proposal = s_integral + error;

    if(proposal > COUNTROL_I_LIMIT){
    	proposal = COUNTROL_I_LIMIT;
    }else if(proposal < -COUNTROL_I_LIMIT){
    	proposal = -COUNTROL_I_LIMIT;
    }


    output = COUNTROL_BASE_PERMILE + COUNTROL_KP_PER_DC * error +  proposal / COUNTROL_I_DIVISOR;

    if ((output >= 0 && output <= 1000)
        || (output > 1000 && error < 0)
        || (output < 0 && error > 0)) {
        s_integral = proposal;
    }

    output = COUNTROL_BASE_PERMILE + COUNTROL_KP_PER_DC * error + s_integral / COUNTROL_I_DIVISOR;

    if (output < 0) {
        output = 0;
    } else if (output > 1000) {
        output = 1000;
    }

    if(output > s_output_permille + COUNTROL_SLEW_STEP){
    	output = s_output_permille + COUNTROL_SLEW_STEP;
    }else if(output < s_output_permille - COUNTROL_SLEW_STEP){
    	output = s_output_permille - COUNTROL_SLEW_STEP;
    }

    s_output_permille = output;

	return (uint16_t)output;

}

void control_reset(void)
{
	s_integral = 0;
	s_output_permille = 0;
}





