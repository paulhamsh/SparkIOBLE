#ifndef SparkComms_h
#define SparkComms_h

#include "NimBLEDevice.h"
#include "BluetoothSerial.h"
#include "RingBuffer.h"

#define HW_BAUD 1000000
#define BLE_BUFSIZE 5000

// Bluetooth vars
#define  SPARK_NAME  "Spark 40 Audio"
#define  MY_NAME     "Heltec"

void start_ser();
void start_bt(bool isBLE);
void connect_to_spark();

bool ser_available();
bool bt_available();

uint8_t ser_read();
uint8_t bt_read();

void ser_write(byte *buf, int len);
void bt_write(byte *buf, int len);
    

// bluetooth communications

BluetoothSerial *bt;
HardwareSerial *ser;
bool is_ble;

// BLE 
NimBLEAdvertisedDevice device;
NimBLEClient *pClient;
NimBLERemoteService *pService;
NimBLERemoteCharacteristic *pSender;
NimBLERemoteCharacteristic *pReceiver;

RingBuffer ble_in;

//byte ble_in[BLE_BUFSIZE];
//int ble_len;
//int ble_pos;

void notifyCB(NimBLERemoteCharacteristic* pRemoteCharacteristic, uint8_t* pData, size_t length, bool isNotify);

#endif
