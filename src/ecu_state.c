#include "ecu_state.h"

ecu_state_t ecu_state_evaluate(bool enable_request, bool sensor_fault, bool overtemp_latched)
{
	if(sensor_fault || overtemp_latched){
		return ECU_FAILSAFE;
	}

	if(!enable_request){
		return ECU_OFF;
	}

	return ECU_RUNNING;
}
