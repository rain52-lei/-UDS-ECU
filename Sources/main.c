/* ###################################################################
**     Filename    : main.c
**     Processor   : S32K1xx
**     Abstract    :
**         Main module.
**         This module contains user's application code.
**     Settings    :
**     Contents    :
**         No public methods
**
** ###################################################################*/
/*!
** @file main.c
** @version 01.00
** @brief
**         Main module.
**         This module contains user's application code.
*/         
/*!
**  @addtogroup main_module main module documentation
**  @{
*/         
/* MODULE main */


/* Including necessary module. Cpu.h contains other modules needed for compiling.*/
#include "Cpu.h"

  volatile int exit_code = 0;

/* User includes (#include below this line is not maintained by Processor Expert) */
#include "app.h"
#include "timebase.h"
#include "adc_input.h"
#include "pwm_output.h"
#include "serial_port.h"
#include "can_port.h"
#include "watchdog_port.h"

volatile status_t g_init_status = STATUS_SUCCESS;
volatile status_t g_uart_tx_status = STATUS_SUCCESS;
volatile status_t g_uart_tx_poll_status = STATUS_SUCCESS;

static const uint8_t s_boot_message[] = "READY\r\n";
/*! 
  \brief The main function for the project.
  \details The startup initialization sequence is the following:
 * - startup asm routine
 * - main()
*/
int main(void)
{
  /* Write your local variable definition here */


  /*** Processor Expert internal initialization. DON'T REMOVE THIS CODE!!! ***/
  #ifdef PEX_RTOS_INIT
    PEX_RTOS_INIT();                   /* Initialization of the selected RTOS. Macro is defined by the RTOS component. */
  #endif
  /*** End of Processor Expert internal initialization.                    ***/

  /* Write your code here */
  g_init_status = CLOCK_DRV_Init(&clockMan1_InitConfig0);
  if (g_init_status != STATUS_SUCCESS) {
      for (;;) {}
  }

  if (!watchdog_port_init()) { for (;;) {} }

  g_init_status = PINS_DRV_Init(NUM_OF_CONFIGURED_PINS,
                               g_pin_mux_InitConfigArr);
  if (g_init_status != STATUS_SUCCESS) {
      for (;;) {}
  }

  g_init_status = timebase_init();
  if (g_init_status != STATUS_SUCCESS) {
      for (;;) {}
  }

  adc_input_init();

  g_init_status = pwm_output_init();
  if(g_init_status != STATUS_SUCCESS){
	  for(;;){}
  }

  g_init_status = serial_port_init();
  if(g_init_status != STATUS_SUCCESS){
	  for(;;){}
  }

  g_init_status = serial_port_start_receive();
  if(g_init_status != STATUS_SUCCESS){
	  for(;;){}
  }

  g_init_status = can_port_init();
  if(g_init_status != STATUS_SUCCESS){
	  for(;;){}
  }

  g_init_status = can_port_start_loopback_test();
  if (g_init_status != STATUS_SUCCESS) {
      for (;;) {}
  }

  g_init_status = can_port_start_command_receive();
  if (g_init_status != STATUS_SUCCESS) {
      for (;;) {}
  }

  app_init();

  g_uart_tx_status = serial_port_send(s_boot_message, sizeof(s_boot_message) - 1U);

  for (;;) {
      can_port_poll();
      app_poll();
      g_uart_tx_poll_status = serial_port_get_tx_status();
  }

  /* For example: for(;;) { } */

  /*** Don't write any code pass this line, or it will be deleted during code generation. ***/
  /*** RTOS startup code. Macro PEX_RTOS_START is defined by the RTOS component. DON'T MODIFY THIS CODE!!! ***/
  #ifdef PEX_RTOS_START
    PEX_RTOS_START();                  /* Startup of the selected RTOS. Macro is defined by the RTOS component. */
  #endif
  /*** End of RTOS startup code.  ***/
  /*** Processor Expert end of main routine. DON'T MODIFY THIS CODE!!! ***/
  for(;;) {
    if(exit_code != 0) {
      break;
    }
  }
  return exit_code;
  /*** Processor Expert end of main routine. DON'T WRITE CODE BELOW!!! ***/
} /*** End of main routine. DO NOT MODIFY THIS TEXT!!! ***/

/* END main */
/*!
** @}
*/
/*
** ###################################################################
**
**     This file was created by Processor Expert 10.1 [05.21]
**     for the NXP S32K series of microcontrollers.
**
** ###################################################################
*/
