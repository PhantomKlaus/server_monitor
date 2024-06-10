/* 
 * Project myProject
 * Author: Your Name
 * Date: 
 * For comprehensive documentation and examples, please visit:
 * https://docs.particle.io/firmware/best-practices/firmware-template/
 */

// Include Particle Device OS APIs
#include "Particle.h"
#include "Serial2/Serial2.h"
#include "prototypes_defs.hpp"
#include "HttpClient.h"

STARTUP(USBSerial1.begin(9600));

#define FSM_STATE_WAIT_CONV 2
#define FSM_STATE_ACTIVE 1
#define FSM_STATE_RST    0
#define LTM_TIMEOUT_US   22UL
#define LTM_SWTIMER_PER_MS 20
#define USB_SERIAL_RX_BUFF 64
// Let Device OS manage the connection to the Particle Cloud
SYSTEM_MODE(AUTOMATIC);

// Run the application and system concurrently in separate threads
SYSTEM_THREAD(ENABLED);

//ISR Call-backs definitions
void LTM_Timer_callback();
void LTM_IRQ_Handler();


//HttpClient http_client_obj;

// Show system, cloud connectivity, and application logs over USB
// View logs with CLI using 'particle serial monitor --follow'
SerialLogHandler logHandler(9600,LOG_LEVEL_WARN,{{"app",LOG_LEVEL_ALL},{"app.network",LOG_LEVEL_INFO}});

//RTOS Soft timer 
Timer LTM_timer(LTM_SWTIMER_PER_MS,LTM_Timer_callback,false);
//global time variable
time32_t time_var_glb;

//global JSON string buffer
char json_buffer[256];
JSONBufferWriter json_writer(json_buffer,sizeof(json_buffer) - 1);

/**
 * Publish Temperature function.
 * Publishes a JSOn Object with form: 
 * {
 *    Temp:(double,2),
 *    Device_time:(string ISO8601_FULL)
 * 
 * }
 */
void PublishTemp(float &temp,time32_t &time_var)
{
  String time_str = Time.format(time_var, TIME_FORMAT_ISO8601_FULL);
  memset(json_buffer, 0, sizeof(json_buffer));
  json_writer.beginObject();
  json_writer.name("Temp").value((double) temp,2);
  json_writer.name("Device_time").value(time_str);
  json_writer.endObject();
  json_writer.buffer()[std::min(json_writer.bufferSize(), json_writer.dataSize())] = 0;
  Particle.publish("JSON Obj",json_writer.buffer(),PRIVATE,NO_ACK);
}


volatile unsigned char RX_app_buffer[USB_SERIAL_RX_BUFF];
volatile unsigned int  data_in_rx_buffer;
unsigned int LTM01_sensor = D2;
unsigned int LED = D7;
volatile unsigned long int LTM_last_timestamp = 0;
volatile unsigned int  LTM_pulse_count = 0;
volatile unsigned int  LTM_last_conv_res = 0;
volatile bool          LTM_conv_read = true;
volatile unsigned int  fsm_state_var = FSM_STATE_RST;
volatile bool          missed_conv = false;

/*Finite State Machine executed on every edge from LTM sensor*/
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
      LTM_pulse_count = 1;
      break;
    case FSM_STATE_WAIT_CONV:
      LTM_last_timestamp = micros();
      LTM_pulse_count += 1;
      break;
  }
 
  
}
/*Periodic Timer ISR eecuted to check states and run the FSM*/
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
        //detachInterrupt(LTM01_sensor);
      }
      /*
        else keep the active state
      */
      break;
    case FSM_STATE_WAIT_CONV:
      if(now - LTM_last_timestamp >= LTM_TIMEOUT_US )
      {
        /*
          Timeout elapsed...at least 1 pulse count missing so we are in the pause sequenc
          Overrun Conv finished
        */
        LTM_pulse_count = 0;
        if(LTM_conv_read)
        {
          /*Do RST HERE*/
          /*Only RESET and start new conversion if the last one was read*/
          LTM_conv_read = false;
          fsm_state_var = FSM_STATE_ACTIVE;
        }
        else
        {
          fsm_state_var = FSM_STATE_RST;
        }
      }
      break;
    default:
      fsm_state_var = FSM_STATE_RST;
      break;
  }
  
}

void usbSerialEvent1()
{
  int data_in = USBSerial1.available();
  WITH_LOCK(USBSerial1)
  {
    if(data_in_rx_buffer == 0)
    {
      if(data_in <= USB_SERIAL_RX_BUFF)
        data_in_rx_buffer=USBSerial1.readBytes((char*)RX_app_buffer,data_in);
      else
        data_in_rx_buffer=USBSerial1.readBytes((char*)RX_app_buffer,USB_SERIAL_RX_BUFF);
    }
  }
}

// setup() runs once, when the device is first turned on
void setup() {
  // Put initialization like pinMode and begin functions here
  Log.info("System started");
  
  USBSerial1.isConnected();
  Time.zone(3);
  Time.setFormat(TIME_FORMAT_ISO8601_FULL);
  time_var_glb = Time.now();
  memset(json_buffer, 0, sizeof(json_buffer));
  pinMode(LED,OUTPUT);
  pinMode(LTM01_sensor,INPUT);
  int success = attachInterrupt(LTM01_sensor,LTM_IRQ_Handler,RISING,13,1);
  if(success)
  {
    //Log.info("IRQ Installed");
  }
  else
  {
    //Log.error("IRQ installation failed");
  }
  Log.info("current time: %s", Time.timeStr().c_str());
  
  float temp =0.4;
  PublishTemp(temp,time_var_glb);
}

// loop() runs over and over again, as quickly as it can execute.
void loop() {
  // The core of your code will likely live here.

  // Example: Publish event to cloud every 10 seconds. Uncomment the next 3 lines to try it!
  // Log.info("Sending Hello World to the cloud!");
  // Particle.publish("Hello world!");
  // delay( 10 * 1000 ); // milliseconds and blocking - see docs for more info!
}
