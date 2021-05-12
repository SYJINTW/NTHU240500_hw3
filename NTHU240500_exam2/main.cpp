/* ---- INCLUDE ---- */
// Basic
#include <iostream>
#include <iomanip>
#include "math.h"
#include "mbed.h"

// for gesture
#include "accelerometer_handler.h"
#include "magic_wand_model_data.h"

#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"

// for uLCD
#include "uLCD_4DGL.h"

// for WIFI & MQTT
#include "MQTTNetwork.h"
#include "MQTTmbed.h"
#include "MQTTClient.h"

// for RPC
#include "mbed_rpc.h"

// for tilt angle
#include "stm32l475e_iot01_accelero.h"


/* ---- NAMESPACE ---- */
using namespace std::chrono;


/* ---- DEFINE ---- */
#define label_num 3

/* ---- STRUCT ---- */
struct Config {

  // This must be the same as seq_length in the src/model_train/config.py
  const int seq_length = 64;

  // The number of expected consecutive inferences for each gesture type.
  const int consecutiveInferenceThresholds[label_num] = {20, 10, 15};

  const char* output_message[label_num] = {
        "RING:\n\r",
        "SLOPE:\n\r",
        "LEFT TO RIGHT:\n\r",
        };
};
Config config;


/* ---- GLOBAL VARIABLE ---- */

// type of MODE: 0 -> NONE | 1 -> GUI | 2 -> DETECTION
int MODE = 0;
int threshold = 30;
float angle = 0;
bool mqtt_flag = true;
int16_t PDataXYZ[3] = {0};
int16_t gDataXYZ[3] = {0};
int num = 0; // event number

// for gesture
constexpr int kTensorArenaSize = 60 * 1024;
uint8_t tensor_arena[kTensorArenaSize];


/* ---- uLCD ---- */
uLCD_4DGL uLCD(D1, D0, D2);

/* ---- LED ---- */
DigitalOut led1(LED1); // GUI MODE
DigitalOut led2(LED2); // DETECTION MODE
DigitalOut led3(LED3);

/* ---- INTERRUPT ---- */
InterruptIn btn(USER_BUTTON);

/* ---- WIFI ---- */
WiFiInterface *wifi;
volatile int message_num = 0;
volatile int arrivedcount = 0;
volatile bool closed = false;
const char* topic = "Mbed";

/* ---- RPC ---- */
// insert function define
void MODESelect(Arguments *in, Reply *out);

RPCFunction rpcLoop(&MODESelect, "MODESelect");
BufferedSerial pc(USBTX, USBRX);

/* ---- THREAD ---- */
Thread GUI_thread(osPriorityNormal, 8 * 1024);
Thread DETECTION_thread(osPriorityNormal);
Thread WIFI_MQTT_thread(osPriorityHigh);
Thread mqtt_thread(osPriorityHigh);
EventQueue mqtt_queue;

/* ---- FUNCTION ---- */
// for predicting gesture
int PredictGesture(float* output) {
  // How many times the most recent gesture has been matched in a row
  static int continuous_count = 0;
  // The result of the last prediction
  static int last_predict = -1;

  // Find whichever output has a probability > 0.8 (they sum to 1)
  int this_predict = -1;
  for (int i = 0; i < label_num; i++) {
    if (output[i] > 0.8) this_predict = i;
  }

  // No gesture was detected above the threshold
  if (this_predict == -1) {
    continuous_count = 0;
    last_predict = label_num;
    return label_num;
  }

  if (last_predict == this_predict) {
    continuous_count += 1;
  } else {
    continuous_count = 0;
  }
  last_predict = this_predict;

  // If we haven't yet had enough consecutive matches for this gesture,
  // report a negative result
  if (continuous_count < config.consecutiveInferenceThresholds[this_predict]) {
    return label_num;
  }
  // Otherwise, we've seen a positive result, so clear all our variables
  // and report it
  continuous_count = 0;
  last_predict = -1;

  return this_predict;
}

// GUI mode
void GUI()
{

  // Whether we should clear the buffer next time we fetch data
  bool should_clear_buffer = false;
  bool got_data = false;

  // The gesture index of the prediction
  int gesture_index;

  // Set up logging.
  static tflite::MicroErrorReporter micro_error_reporter;
  tflite::ErrorReporter* error_reporter = &micro_error_reporter;

  // Map the model into a usable data structure. This doesn't involve any
  // copying or parsing, it's a very lightweight operation.
  const tflite::Model* model = tflite::GetModel(g_magic_wand_model_data);
  if (model->version() != TFLITE_SCHEMA_VERSION) {
    error_reporter->Report(
        "Model provided is schema version %d not equal "
        "to supported version %d.",
        model->version(), TFLITE_SCHEMA_VERSION);
    return;
    //return -1;
  }

  // Pull in only the operation implementations we need.
  // This relies on a complete list of all the ops needed by this graph.
  // An easier approach is to just use the AllOpsResolver, but this will
  // incur some penalty in code space for op implementations that are not
  // needed by this graph.
  static tflite::MicroOpResolver<6> micro_op_resolver;
  micro_op_resolver.AddBuiltin(
      tflite::BuiltinOperator_DEPTHWISE_CONV_2D,
      tflite::ops::micro::Register_DEPTHWISE_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_MAX_POOL_2D,
                               tflite::ops::micro::Register_MAX_POOL_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_CONV_2D,
                               tflite::ops::micro::Register_CONV_2D());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_FULLY_CONNECTED,
                               tflite::ops::micro::Register_FULLY_CONNECTED());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_SOFTMAX,
                               tflite::ops::micro::Register_SOFTMAX());
  micro_op_resolver.AddBuiltin(tflite::BuiltinOperator_RESHAPE,
                               tflite::ops::micro::Register_RESHAPE(), 1);

  // Build an interpreter to run the model with
  static tflite::MicroInterpreter static_interpreter(
      model, micro_op_resolver, tensor_arena, kTensorArenaSize, error_reporter);
  tflite::MicroInterpreter* interpreter = &static_interpreter;

  // Allocate memory from the tensor_arena for the model's tensors
  interpreter->AllocateTensors();

  // Obtain pointer to the model's input tensor
  TfLiteTensor* model_input = interpreter->input(0);
  if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
      (model_input->dims->data[1] != config.seq_length) ||
      (model_input->dims->data[2] != kChannelNumber) ||
      (model_input->type != kTfLiteFloat32)) {
    error_reporter->Report("Bad input tensor parameters in model");
    return;
    //return -1;
  }

  int input_length = model_input->bytes / sizeof(float);

  TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
  if (setup_status != kTfLiteOk) {
    error_reporter->Report("Set up failed\n");
    return;
    //return -1;
  }

  error_reporter->Report("Start gesture\n");

  threshold = 30;

  while (true) {
    if(MODE == 1)
    {
      led1 = 1;

      // Attempt to read new data from the accelerometer
      got_data = ReadAccelerometer(error_reporter, model_input->data.f,
                                  input_length, should_clear_buffer);

      // If there was no new data,
      // don't try to clear the buffer again and wait until next time
      if (!got_data) {
        should_clear_buffer = false;
        continue;
      }

      // Run inference, and report any error
      TfLiteStatus invoke_status = interpreter->Invoke();
      if (invoke_status != kTfLiteOk) {
        error_reporter->Report("Invoke failed on index: %d\n", begin_index);
        continue;
      }

      // Analyze the results to obtain a prediction
      gesture_index = PredictGesture(interpreter->output(0)->data.f);
      
      // Clear the buffer next time we read data
      should_clear_buffer = gesture_index < label_num;

      // Produce an output
      if (gesture_index < label_num)
      {
        switch (gesture_index)
        {
          case 2:
            threshold += 5;
            if (threshold > 90)
            {
              threshold = 30;
            }
            break;
          default:
            threshold = threshold;
            break;
        }
        //printf("Gesture index: %d\n", gesture_index); // 1 is the gesture_index we want to change tile
        //printf("tile: %d\n", threshold);
        //error_reporter->Report(config.output_message[gesture_index]);
        
        // Use uLCD to show the threshold
        uLCD.locate(1, 3);
        uLCD.printf("%d", threshold);
      }
    }
    else
    {
      led1 = 0;
    }
  }
}

// init uLCD display
void init_uLCD_display()
{
  uLCD.cls();
  uLCD.locate(1, 2);
  uLCD.printf("A:");
  uLCD.locate(1, 3);
  uLCD.printf("%d", threshold);
  uLCD.locate(1, 6);
  uLCD.printf("B:");
  uLCD.locate(1, 7);
  uLCD.printf("X\n");
}

// close MQTT
void close_MQTT()
{
  mqtt_flag = false;
}

// tilt angle
void tilt_angle(MQTT::Client<MQTTNetwork, Countdown> *client)
{
  BSP_ACCELERO_Init();
  BSP_ACCELERO_AccGetXYZ(gDataXYZ);

  // get init X Y Z as the base line
  int Axis[3];
  for (int i = 0; i < 3; i++)
  {
    Axis[i] = gDataXYZ[i];
  }
  
  // start detection
  while (1)
  {
    if (MODE == 2)
    {
      led2 = 1;

      long int dotproduct = 0;
      long int normA = 0;
      long int normg = 0;
      
      // get XYZ data
      BSP_ACCELERO_AccGetXYZ(gDataXYZ);
      for (int i = 0; i < 3; i++)
      {
        dotproduct += gDataXYZ[i] * Axis[i];
        normA += Axis[i] * Axis[i];
        normg += gDataXYZ[i] * gDataXYZ[i];
      }

      // calculate the angle by dot
      float cosvalue = dotproduct / sqrt(normg) / sqrt(normA);
      angle = acos(cosvalue) * 180 / 3.1415926;
      
      // change uLCD display
      uLCD.locate(1, 7);
      uLCD.printf("%-4.1f", angle);
      ThisThread::sleep_for(250ms);

      // difference as 5 degree
      if (angle < 5)
      {
        led3 = !led3; // led spark means still on table
      }
      if (angle >= threshold && num < 5)
      {
        num++;

        MQTT::Message message;
        char buff[100];
        sprintf(buff, "The %d time(s)", num);
        message.qos = MQTT::QOS0;
        message.retained = false;
        message.dup = false;
        message.payload = (void *)buff;
        message.payloadlen = strlen(buff) + 1;
        int rc = client->publish(topic, message);

        printf("rc:  %d\r\n", rc);
        printf("Puslish message: %s\r\n", buff);
        if (num >= 5)
        {
            MODE = 0;
        }
      }
    }
    else
    {
      led2 = 0;
    }
  }
}

// collect message
void messageArrived(MQTT::MessageData &md)
{
   MQTT::Message &message = md.message;
   char msg[300];
   sprintf(msg, "Message arrived: QoS%d, retained %d, dup %d, packetID %d\r\n", message.qos, message.retained, message.dup, message.id);
   printf(msg);
   ThisThread::sleep_for(1000ms);
   char payload[300];
   sprintf(payload, "Payload %.*s\r\n", message.payloadlen, (char *)message.payload);
   printf(payload);
   ++arrivedcount;
}

// MODE selection
void MODESelect(Arguments *in, Reply *out)
{
  int input_mode = in->getArg<int>();
  char buffer[200];
  printf("PRESENT MODE = %d\n", MODE);
  MODE = input_mode;
  if (input_mode == 1)
  {
    sprintf(buffer, "UI MODE");
  }
  else if (input_mode == 2)
  {
    sprintf(buffer, "DETECTION MODE");
  }
  else
  {
    sprintf(buffer, "SAFE MODE");
  }
  printf("NEW MODE = \n", MODE);
  out->putData(buffer);
}

// after interrupt
void publish_message(MQTT::Client<MQTTNetwork, Countdown> *client)
{
   message_num++;
   MQTT::Message message;
   char buff[100];
   sprintf(buff, "Angle threshold is %d", threshold);
   message.qos = MQTT::QOS0;
   message.retained = false;
   message.dup = false;
   message.payload = (void *)buff;
   message.payloadlen = strlen(buff) + 1;
   int rc = client->publish(topic, message);

   printf("rc: %d\r\n", rc);
   printf("Puslish message: %s\r\n", buff);
   MODE = 0;
}

// WIFI model
void WIFI_MQTT()
{
  /*---- WIFI CONNECTION ----*/
  wifi = WiFiInterface::get_default_instance();
  if (!wifi)
  {
    printf("ERROR: No WiFiInterface found.\r\n");
    return;
  }

  printf("\nConnecting to %s...\r\n", MBED_CONF_APP_WIFI_SSID);
  int ret = wifi->connect(MBED_CONF_APP_WIFI_SSID, MBED_CONF_APP_WIFI_PASSWORD, NSAPI_SECURITY_WPA_WPA2);
  if (ret != 0)
  {
    printf("\nConnection error: %d\r\n", ret);
    return;
  }


  /*---- MQTT ----*/
  NetworkInterface *net = wifi;
  MQTTNetwork mqttNetwork(net);
  MQTT::Client<MQTTNetwork, Countdown> client(mqttNetwork);

  // TODO: revise host to your IP
  const char *host = "192.168.0.24";
  printf("Connecting to TCP network...\r\n");

  SocketAddress sockAddr;
  sockAddr.set_ip_address(host);
  sockAddr.set_port(1883);

  printf("address is %s/%d\r\n", (sockAddr.get_ip_address() ? sockAddr.get_ip_address() : "None"), (sockAddr.get_port() ? sockAddr.get_port() : 0)); //check setting

  int rc = mqttNetwork.connect(sockAddr); //(host, 1883);
  if (rc != 0)
  {
    printf("Connection error.");
    return;
  }
  printf("Successfully connected!\r\n");

  MQTTPacket_connectData data = MQTTPacket_connectData_initializer;
  data.MQTTVersion = 3;
  data.clientID.cstring = "Mbed";

  if ((rc = client.connect(data)) != 0)
  {
    printf("Fail to connect MQTT\r\n");
  }
  if (client.subscribe(topic, MQTT::QOS0, messageArrived) != 0)
  {
    printf("Fail to subscribe\r\n");
  }

  // MQTT thread
  mqtt_thread.start(callback(&mqtt_queue, &EventQueue::dispatch_forever));

  // button interrupt -> run publish_message to client
  btn.rise(mqtt_queue.event(&publish_message, &client));

  // detection mode thread
  DETECTION_thread.start(callback(&tilt_angle, &client));

  // 
  int num = 0;
  while (num != 5)
  {
    client.yield(100);
    ++num;
  }

  // wifi main loop
  while (1)
  {
    if (!mqtt_flag)
    {
      break;
    } 
    client.yield(500);
    ThisThread::sleep_for(500ms);
  }

  printf("Ready to close MQTT Network......\n");

  if ((rc = client.unsubscribe(topic)) != 0)
  {
    printf("Failed: rc from unsubscribe was %d\n", rc);
  }
  if ((rc = client.disconnect()) != 0)
  {
    printf("Failed: rc from disconnect was %d\n", rc);
  }
  mqttNetwork.disconnect();
  printf("Successfully closed!\n");
}

int main(int argc, char* argv[])
{
  // Variable
  char buf[256], outbuf[256];
  FILE *devin = fdopen(&pc, "r");
  FILE *devout = fdopen(&pc, "w");


  // init uLCD display
  init_uLCD_display();
  
  // init Accelerometer
  BSP_ACCELERO_Init();

  // start GUI thread
  GUI_thread.start(GUI);

  // start WIFI thread
  WIFI_MQTT_thread.start(&WIFI_MQTT);


  // main while
  while(1)
  {
    memset(buf, 0, 256); // clear buffer
    for (int i = 0; i < 255; i++)
    {
        char recv = fgetc(devin);
        if (recv == '\r' || recv == '\n')
        {
          printf("\r\n");
          break;
        }
        buf[i] = fputc(recv, devout);
    }
    RPC::call(buf, outbuf);
    printf("%s\r\n", outbuf);
  }
  return 0;
}