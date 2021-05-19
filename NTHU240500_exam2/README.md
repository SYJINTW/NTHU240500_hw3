<!-- ABOUT THE PROJECT -->
## About The Project

This is a project for Embedded System Lab's homework.
Homework 3 TF Lite, WiFi and MQTT.

### Built With

* C++
* Python
* json

### Equipment List

* PC or notebook
* B_L4S5I_IOT01A
* uLCD display

## Homework Description

* Complete work flow
    1. mbed run a RPC loop with two custom functions (operation modes): (1) gesture UI, and (2) tilt angle detection
    2. PC/Python use RPC over serial to send a command to call gesture UI mode on mbed
        * The gesture UI function will start a thread function.
        * In the thread function, user will use gesture to select from a few threshold angles.
        * After the selection is confirmed with a user button, the selected threshold angle is published through WiFi/MQTT to a broker (run on PC).
        * After the PC/Python get the published confirmation from the broker, it sends a command to mbed to stop the guest UI mode. Therefore, the mbed is back to RPC loop. Also PC/Python will show the selection on screen.
    3. PC/Python use RPC over serial to send a command to call tilt angle detection mode on mbed
        * The tilt angle function will start a thread function.
        * If the tilt angle is over the selected threshold angle, mbed will publish the event and angle through WiFi/MQTT to a broker.
        * After the PC/Python get a preset number of tilt events, e.g., 10, from the broker, it sends a command to mbed to stop the tilt detection mode. Therefore, the mbed is back to RPC loop. Also PC/Python will show all tilt events on screen.

* More about gesture UI mode
    Please use LEDs to indicate the start of UI mode.
    Please use your gesture TF Lite model from mbed lab 8 to identify user's selection.
    The purpose to to select from a list of angles, e.g., 30, 35, 40, 45, etc.
    Before confirmation, please show the selection on uLCD (so a user can see their current selection).

* More about tilt angle detection mode
    Please use LEDs to indicate the start of tilt mode.
    Before we start the tilt measurement, we should measure the reference acceleration vector. Please use LEDs to show this initialization process for a user to place the mbed on table. We assume this is the stationary position of the mbed. Please use accelerometer to measure this acceleration vector as the reference (the direction should align with gravity).
    After we initialize the gravity vector, please use LEDs to indicate for a user to tilt the mbed. In this mode, we use the accelerometer vectors to detect the tilt angles for each predefined period, e.g., 100ms. A MQTT message will publish if mbed tilts over the selected threshold degree to the stationary position.
    For each predefined period (100ms as above), please show the tilt angle on uLCD (so a user can determine how to tilt mbed).


<!-- GETTING STARTED -->
## Getting Started

### Running & compile

* Compile project.
  
    ```sh
    sudo mbed compile --source . --source ~/ee2405/mbed-os/ -m B_L4S5I_IOT01A -t GCC_ARM -f
    ```

    * If you compile success, then you will see this in your terminal.
        <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/compile_success.png?raw=true">
    
* Input command from screen.
    
    ```sh
    sudo screen /dev/ttyACM0
    ```

* Inside Screen. Way to command.
    
    * Run Gesture UI MODE 
        
        ```sh
        /MODESelect/run 1
        ```

    * Run DETECTION MODE 
        
        ```sh
        /MODESelect/run 2
        ```

    * <strong>Don't</strong> need to run SAFE MODE, it will compile automatically by python.

        ```sh
        /MODESelect/run 0
        ```

<!-- ROADMAP -->
## Roadmap
1. Connect WIFI and MQTT: run in a WIFI_MQTT_thread and have high priority to aviod wifi disconnect.
    In this part, no only contain WIFI and MQTT initalize, but also contain mqtt_thread for preparing sending message in anytime, and contain the button interrupt for leaving GUI MODE, and DETECTION_thread (it will discuss below).
    However, we also need to prepare for stop MQTT after using. 
    <strong>Caution:</strong> need to change the internet ip address.

1. GUI MODE: run in a GUI_thread and check MODE to know if executing GUI part and predicting gesture. If we press the button, then we will trigger a interrupt and run <strong>publish_message</strong>. In publish_message, we will send messages by MQTT

1. DETECTION MODE: run in a DETECTION_thread (inside WIFI_MQTT_thread) and check MODE to know if executing detection mode. 
    The main part of detection is 

<!-- Screenshot -->
## Results

* Connect successfully with WIFI and MQTT Client.
  
    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/connect_success.png?raw=true">  

* Gesture MODE (default 30 degree)

    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/select_mode_1.png?raw=true">

* Gesture MODE (change to 40 degree)
    
    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/select_mode_2.png?raw=true"> 

* Gesture MODE (end)
    
    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/select_mode_end.png?raw=true">

* Detection MODE (start)

    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/detection_mode_1.png?raw=true">  

* Detection MODE (5 times)

    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/detection_mode_2.png?raw=true">

* Detection MODE (end)
  
    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/detection_mode_end.png?raw=true"> 

* Safe MODE (by command, actually don't need)
  
    <img src="https://github.com/SYJINTW/NTHU240500_hw3/blob/master/NTHU240500_exam2/img/safe_mode.png?raw=true"> 

* Selection MODE (Video for Demo)
    [![](http://img.youtube.com/vi/UrSFGXki5qI/0.jpg)](http://www.youtube.com/watch?v=UrSFGXki5qI "selection_mode")
* Detection MODE (Video for Demo)
    [![](http://img.youtube.com/vi/WD26TGfL0H0/0.jpg)](http://www.youtube.com/watch?v=WD26TGfL0H0 "detection_mode")

<!-- ACKNOWLEDGEMENTS -->
## Acknowledgements

* [Embedded System Lab](https://www.ee.nthu.edu.tw/ee240500/)

