# Zephyr Health Monitor

This repository is for building the Zephyr RTOS sample Health Monitor Application that works with the Arduino 101board.  This repository is a fork from Zephyr RTOS 1.4.0 that can be found at http://www.zephyrproject.org. 

It contains applications demonstrating Bluetooth Smart profiles: 
* **Heart Rate Monitor**, 
* **Health Thermometer Monitor**, 
* **Pulse Oximeter Monitor**,

The sample application path can be found at https://github.com/zephyrhealthproject/zephyr/samples/bluetooth/peripheral_health_sensor.
- Includes Pulsesensor.com heart-rate algorithm, Neopenda's respiration rate calculation, DSP measurements for heart rate, temperature, blood oxygen, and respiration rate, and new BLE profiles for HRS (Heart-Rate), SpO2 (Pulse Oximeter), and HTS (Health Thermometer).
- The ARC Core is responsible for processing both the analog and digital sensor values and providing results to the X86 Core.
- The X86 Core is responsible for setting up and configuring the BLE peripheral radio and handling sending the health measurements to the BLE central device (Android Application).

The corresponding Android application that works with this firmware can be found at https://github.com/zephyrhealthproject/android.

### Known problems
- There is a limit to the number of concurrent devices that can be connected to the corresponding Android Application due to the version of Android O.S.  The theorectical limit of devices connected to the application at one time is up to 10 devices.
- The current draw from the Arduino 101 will require using external power supply such as a 9 volt battery.  Using the USB power can give unexpected behavior.


