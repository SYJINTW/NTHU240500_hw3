// Include header
/* --------------------------------------- */
// Basic
#include <iostream>
#include <thread> // for thread
#include <ctime> // for clock
#include <string>
#include "math.h"

// mbed
#include "mbed.h"

// uLCD
#include "uLCD_4DGL.h" // for uLCD

// RPC
#include "mbed_rpc.h"

// Accelerometer
#include "stm32l475e_iot01_accelero.h"

// tensor flow
// LAB8
#include "accelerometer_handler.h"
#include "config.h"
#include "magic_wand_model_data.h"
#include "tensorflow/lite/c/common.h"
#include "tensorflow/lite/micro/kernels/micro_ops.h"
#include "tensorflow/lite/micro/micro_error_reporter.h"
#include "tensorflow/lite/micro/micro_interpreter.h"
#include "tensorflow/lite/micro/micro_mutable_op_resolver.h"
#include "tensorflow/lite/schema/schema_generated.h"
#include "tensorflow/lite/version.h"


// Set namespace
/* --------------------------------------- */
using namespace std::chrono;

// Global Variable
/* --------------------------------------- */
int MODE = 0; // 0 -> NONE | 1 -> GUI | 2 -> DETECTION 
int threshold = 30;

// LED
/* --------------------------------------- */
DigitalOut led1(LED1); // GUI MODE
DigitalOut led2(LED2); // DETECTION MODE
DigitalOut led3(LED3);

// Thread
/* --------------------------------------- */
Thread GUI_thread(osPriorityNormal, 8 * 1024);
Thread Detection_thread;
Thread WIFI_thread;
Thread MQTT_thread;
EventQueue MQTT_queue;


// Function
/* --------------------------------------- */
void GUI()
{
    // flags
    bool should_clear_buffer = false;
    bool got_data = false;

    // The gesture index of the prediction
    int gesture_index;

    // Set up logging.
    static tflite::MicroErrorReporter micro_error_reporter;
    tflite::ErrorReporter *error_reporter = &micro_error_reporter;

    // Map the model into a usable data structure. This doesn't involve any
    // copying or parsing, it's a very lightweight operation.
    const tflite::Model *model = tflite::GetModel(g_magic_wand_model_data);
    if (model->version() != TFLITE_SCHEMA_VERSION)
    {
        error_reporter->Report(
            "Model provided is schema version %d not equal "
            "to supported version %d.",
            model->version(), TFLITE_SCHEMA_VERSION);
        return -1;
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
    tflite::MicroInterpreter *interpreter = &static_interpreter;

    // Allocate memory from the tensor_arena for the model's tensors
    interpreter->AllocateTensors();

    // Obtain pointer to the model's input tensor
    TfLiteTensor *model_input = interpreter->input(0);
    if ((model_input->dims->size != 4) || (model_input->dims->data[0] != 1) ||
        (model_input->dims->data[1] != config.seq_length) ||
        (model_input->dims->data[2] != kChannelNumber) ||
        (model_input->type != kTfLiteFloat32))
    {
        error_reporter->Report("Bad input tensor parameters in model");
        return -1;
    }

    int input_length = model_input->bytes / sizeof(float);

    TfLiteStatus setup_status = SetupAccelerometer(error_reporter);
    if (setup_status != kTfLiteOk)
    {
        error_reporter->Report("Set up failed\n");
        return -1;
    }
    //printf("OS_stack_size = %d\n", OS_STACK_SIZE);
    error_reporter->Report("Set up successful...\n");
    threshold = 30; // start angle
    while(1)
    {
        if(MODE == 1)
        {
            // turn on LED
            led1 = 1;

            // read data from accelerometer
            got_data = ReadAccelerometer(error_reporter, model_input->data.f, input_length, should_clear_buffer);

            // no new data
            if(!got_data)
            {
                should_clear_buffer = false;
                continue;
            }

            // new data
            TfLiteStatus invoke_status = interpreter->Invoke();
            if (invoke_status != kTfLiteOk)
            {
                error_reporter->Report("Invoke failed on index: %d\n", begin_index);
                continue;
            }

            // Analyze the results to obtain a prediction
            gesture_index = PredictGesture(interpreter->output(0)->data.f);
            printf("Now, gesture_index = %d\n", gesture_index);

            // Clear the buffer next time we read data
            should_clear_buffer = gesture_index < label_num;

        }
        else
        {
            led1 = 0;
        }

    }

}



// Main code
/* --------------------------------------- */
int main()
{
    GUI_thread.start(GUI);
}





