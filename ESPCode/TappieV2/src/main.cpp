// #include <Arduino.h>
// #include <ESP32Encoder.h>
// #include <OneButton.h>
// // put function declarations here:
// #define encPin1 32
// #define encPin2 35

// #define encSwitch 34

// ESP32Encoder encoder;

// OneButton button(encSwitch, true);

// void click();

// void setup()
// {
//   // put your setup code here, to run once:
//   pinMode(encPin1, INPUT);
//   pinMode(encPin2, INPUT);
//   pinMode(encSwitch, INPUT_PULLUP);
//   Serial.begin(115200);
//   Serial.println("Hello World");
//   Serial.println("Encoder Test");
//   encoder.attachHalfQuad(encPin1, encPin2);
//   encoder.clearCount();
//   button.attachClick(click);
// }

// void loop()
// {
//   // put your main code here, to run repeatedly:
//   long newPosition = encoder.getCount() / 2;
//   Serial.println(newPosition);
//   button.tick();
//   delay(10);
// }

// void click()
// {
//   encoder.clearCount();
// }
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// Default Temperature is in Celsius
// Comment the next line for Temperature in Fahrenheit


// BLE server name
#define bleServerName "TappieTest"

BLECharacteristic TestChar("a9c8c7b4-fb55-4d27-99e4-2c14b5812546", BLECharacteristic::PROPERTY_NOTIFY);
BLEDescriptor TestCharDescriptor(BLEUUID((uint16_t)0x2901));

// Timer variables
unsigned long lastTime = 0;
unsigned long timerDelay = 30000;

bool deviceConnected = false;

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/
#define SERVICE_UUID "738b66f1-91b7-4f25-8ab8-31d38d56541a"

// Temperature Characteristic and Descriptor

// Setup callbacks onConnect and onDisconnect
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
  };
  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
  }
};


void setup()
{
  // Start serial communication
  Serial.begin(115200);


  // Create the BLE Device
  BLEDevice::init(bleServerName);

  // Create the BLE Server
  BLEServer *pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *bmeService = pServer->createService(SERVICE_UUID);

  bmeService->addCharacteristic(&TestChar);
  TestCharDescriptor.setValue("Hello this is henry hes so cool");
  TestChar.addDescriptor(&TestCharDescriptor);

  // Start the service
  bmeService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pServer->getAdvertising()->start();
  Serial.println("Waiting a client connection to notify...");
  String randomValue = String(rand() % 100); // Random value between 0 and 99
  TestChar.setValue("hello hello");
}


void loop()
{
  if (deviceConnected)
  {
    Serial.println("Device connected, sending data...");
    String randomValue = String(rand() % 100); // Random value between 0 and 99
    TestChar.setValue(randomValue.c_str());
  }
}