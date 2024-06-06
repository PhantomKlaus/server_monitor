/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"

#define FSM_STATE_WAIT_CONV 2
#define FSM_STATE_ACTIVE 1
#define FSM_STATE_RST    0
#define LTM_TIMEOUT_US   22UL
#define LTM_SWTIMER_PER_MS 20MS
// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(9600,LOG_LEVEL_WARN,{{"app",LOG_LEVEL_ALL},{"app.network",LOG_LEVEL_INFO}});
Timer LTM_timer(10,LTM_Timer_callback,false);

int LTM01_sensor = D2;
int LED = D7;
volatile unsigned long int LTM_last_timestamp = 0;
volatile unsigned int  LTM_pulse_count = 0;
volatile unsigned int  LTM_last_conv_res = 0;
volatile bool          LTM_conv_read = true;
volatile unsigned int  fsm_state_var = FSM_STATE_RST;
volatile bool          missed_conv = false;
void LTM_Timer_callback()
{
  /*Virtual Timer callback from RTOS(SysTick Handler)*/
  unsigned long int now = micros();
  /* Handle overflow of the 32bit val */
  if(now < LTM_last_timestamp)
  {
    unsigned long int temp = 0xFFFFFFFFUL - LTM_last_timestamp;
    LTM_last_timestamp = now;
    now = now + temp;
  }
  switch(fsm_state_var)
  {
    case FSM_STATE_RST:
      if(LTM_conv_read)
      {
        /*Only RESET and start new conversion if the last one was read*/
        LTM_pulse_count = 0;
        LTM_conv_read = false;
        fsm_state_var = FSM_STATE_ACTIVE;
      }
      break;
    case FSM_STATE_ACTIVE:
      if(now - LTM_last_timestamp >= LTM_TIMEOUT_US )
      {
        /*
          Timeout elapsed...at least 1 pulse count missing so we are in the pause sequenc
          Save results and reset.
        */
        LTM_last_conv_res = LTM_pulse_count;
        LTM_conv_read = false;
        LTM_last_timestamp = now;
        fsm_state_var = FSM_STATE_RST;

      }
      /*
        else keep the active state
      */
      break;
    default:
      fsm_state_var = FSM_STATE_RST;
      break;
  }
  
}
void LTM_IRQ_Handler()
{
  switch(fsm_state_var)
  {
    case FSM_STATE_ACTIVE:
      LTM_last_timestamp = micros();
      LTM_pulse_count += 1;
      break;
    case FSM_STATE_RST:
      missed_conv = true;
      fsm_state_var = FSM_STATE_WAIT_CONV;
      break;
    case FSM_STATE_WAIT_CONV:
      LTM_last_timestamp = micros();
      LTM_pulse_count += 1;
      break;
  }
 
  
}
// setup() runs once, when the device is first turned on
void setup() {
  // Put initialization like pinMode and begin functions here
  Log.info("System started");
  pinMode(LED,OUTPUT);
  pinMode(LTM01_sensor,INPUT);
  int success = attachInterrupt(LTM01_sensor,LTM_IRQ_Handler,RISING,13,1);
  if(success)
  {
    Log.info("IRQ Installed");
  }
  else
  {
    Log.error("IRQ installation failed");
  }
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  // The core of your code will likely live here.

  // Example: Publish event to cloud every 10 seconds. Uncomment the next 3 lines to try it!
  // Log.info("Sending Hello World to the cloud!");
  // Particle.publish("Hello world!");
  // delay( 10 * 1000 ); // milliseconds and blocking - see docs for more info!
}
