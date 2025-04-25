/**
 * TappieV2 - BLE Rotary Encoder Controller
 *
 * This device uses an ESP32 with a rotary encoder to control media playback
 * and volume via Bluetooth Low Energy (BLE).
 *
 * Features:
 * - Volume control via rotary encoder
 * - Multiple button actions (single click, double click, etc.)
 * - Auto-reset of encoder position after inactivity
 * - Low power consumption
 */

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ESP32Encoder.h>
#include <OneButton.h>
#include <esp_sleep.h>
#include <WiFi.h>

// ===== PIN DEFINITIONS =====
#define ENCODER_PIN_DT 32
#define ENCODER_PIN_CLK 35
#define ENCODER_PIN_SW 34

gpio_num_t reedSwitchPin = GPIO_NUM_33; // GPIO pin for reed switch

#define AuxButtonPin 2
#define GamingButtonPin 4
#define MediaButtonPin 17
#define ChatButtonPin 18
#define MasterButtonPin 22

// ===== BLE DEFINITIONS =====
#define BLE_DEVICE_NAME "TappieV2"
#define SERVICE_UUID "738b66f1-91b7-4f25-8ab8-31d38d56541a"
#define ENC_POS_UUID "a9c8c7b4-fb55-4d27-99e4-2c14b5812546"
#define ENC_BUTTON_UUID "0c2f5fbe-c20f-49ec-8c7c-ce0c9358e574"
#define MEDIA_SINGLEBUTTON_UUID "9ff67916-665f-4489-b257-46d118b1e5eb"
#define MEDIA_DOUBLEBUTTON_UUID "66f1ab02-c93d-44fe-8ca9-5e8bdbb2fe80"

// ===== TIMING CONSTANTS =====
#define AUTO_RESET_TIMEOUT 5000 // 5 seconds in milliseconds
#define BUTTON_NOTIFY_DELAY 100 // 100ms delay after button notifications

// ===== MEDIA BUTTON DEFINITIONS =====
struct MediaButton
{
  const char *name;
  uint8_t pin;
  OneButton button;
};

// ===== GLOBAL OBJECTS =====
ESP32Encoder encoder;
OneButton encButton(ENCODER_PIN_SW, true, true); // active low, enable internal pullup

// BLE server and characteristics
BLEServer *pServer = NULL;
BLECharacteristic *encPosChara = NULL;
BLECharacteristic *encButtonChara = NULL;
BLECharacteristic *mediaButtonChara = NULL;
BLECharacteristic *mediaDoubleButtonChara = NULL;

// Media buttons array
MediaButton mediaButtons[] = {
    {"Aux", AuxButtonPin, OneButton(AuxButtonPin, true, true)},
    {"Gaming", GamingButtonPin, OneButton(GamingButtonPin, true, true)},
    {"Media", MediaButtonPin, OneButton(MediaButtonPin, true, true)},
    {"Chat", ChatButtonPin, OneButton(ChatButtonPin, true, true)},
    {"Master", MasterButtonPin, OneButton(MasterButtonPin, true, true)}};
const int NUM_MEDIA_BUTTONS = sizeof(mediaButtons) / sizeof(mediaButtons[0]);

// ===== STATE VARIABLES =====
bool deviceConnected = false;
bool oldDeviceConnected = false;

// Encoder position tracking
int prevEncPosition = 0;
int currentEncPosition = 0;

// Timer for auto-reset
unsigned long lastActivityTime = 0;

// Add these variables to the STATE VARIABLES section
bool prevReedState = true;               // Store previous reed switch state
RTC_DATA_ATTR bool wasConnected = false; // Persistent through deep sleep
const int REED_CHECK_INTERVAL = 500;     // Check reed switch every 500ms
unsigned long lastReedCheckTime = 0;

// ===== FUNCTION DECLARATIONS =====
void setupBLE();
void setupEncoder();
void setupMediaButtons();
void resetEncoder();
void handleConnectionChanges();
String getBatteryLevel();
void enterDeepSleep();
void sendNotification(BLECharacteristic *characteristic, const char *value);

/**
 * Helper function to send BLE notifications with auto-reset
 */
void sendNotification(BLECharacteristic *characteristic, const char *value)
{
  if (!deviceConnected)
    return;

  characteristic->setValue(value);
  characteristic->notify();

  // If this is a button action (not a position value), reset after delay
  if (characteristic == encButtonChara || characteristic == mediaButtonChara)
  {
    delay(BUTTON_NOTIFY_DELAY);
    characteristic->setValue("0");
    characteristic->notify();
  }
}

// Add this function before setupMediaButtons()
void buttonClickCallback(void *parameter)
{
  int buttonIndex = *((int *)parameter);
  const char *buttonName = mediaButtons[buttonIndex].name;

  Serial.print("Button clicked: ");
  Serial.println(buttonName);

  if (deviceConnected)
  {
    sendNotification(mediaButtonChara, buttonName);
  }
}

void buttonDoubleClickCallback(void *parameter)
{
  int buttonIndex = *((int *)parameter);
  const char *buttonName = mediaButtons[buttonIndex].name;

  Serial.print("Button double clicked: ");
  Serial.println(buttonName);

  if (deviceConnected)
  {
    sendNotification(mediaDoubleButtonChara, buttonName);
  }
}

/**
 * Setup media buttons with consistent configuration
 */
void setupMediaButtons()
{
  // Static array to store button indices - must be static to persist!
  static int indices[NUM_MEDIA_BUTTONS];

  for (int i = 0; i < NUM_MEDIA_BUTTONS; i++)
  {
    pinMode(mediaButtons[i].pin, INPUT_PULLUP);

    // Store the button index in our static array
    indices[i] = i;

    // Use the parameterized version of attachClick with the index pointer
    mediaButtons[i].button.attachClick(buttonClickCallback, &indices[i]);
    mediaButtons[i].button.attachDoubleClick(buttonDoubleClickCallback, &indices[i]);
  }

  Serial.println("Media buttons initialized");
}

String getBatteryLevel()
{
  int batteryLevel = 49; // Random battery level for simulation
  // Use a proper separator format: " batteryLevel=" followed by the value
  String batteryStr = String(" " + String(batteryLevel));
  return batteryStr;
}

// ===== BLE CALLBACKS =====
/**
 * Callbacks for BLE connection events
 */
class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
    Serial.println("Device connected");
    Serial.println("Device connected at: " + String(millis()));
  };

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    Serial.println("Device disconnected");
    Serial.println("Device disconnected at: " + String(millis()));
  }
};

// ===== SETUP FUNCTION =====
void setup()
{
  // Initialize serial communication
  Serial.begin(115200);

  // Check wakeup reason
  esp_sleep_wakeup_cause_t wakeupReason = esp_sleep_get_wakeup_cause();
  if (wakeupReason == ESP_SLEEP_WAKEUP_EXT1)
  {
    Serial.println("Woke up from deep sleep due to reed switch HIGH");
  }
  else
  {
    Serial.println("Initial boot");
  }

  Serial.println("Starting TappieV2 BLE Server...");

  // Configure reed switch
  pinMode(reedSwitchPin, INPUT_PULLUP); // Set reed switch pin as input with pull-up resistor

  // Check initial reed switch state - go back to sleep if LOW
  if (digitalRead(reedSwitchPin) == LOW)
  {
    Serial.println("Reed switch still LOW - going back to sleep");
    delay(100); // Small delay for stability
    enterDeepSleep();
  }
  WiFi.mode(WIFI_OFF);
  // Continue with normal setup
  btStop(); // Disable classic Bluetooth to save power
  esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
  setCpuFrequencyMhz(80);
  esp_sleep_enable_ext1_wakeup(reedSwitchPin, ESP_EXT1_WAKEUP_ANY_HIGH); // Enable wakeup on reed switch

  // Initialize BLE, encoder and buttons
  setupBLE();
  setupEncoder();
  setupMediaButtons();

  // Initialize activity timer
  lastActivityTime = millis();

  Serial.println("Initialization complete - ready for connections");
}

// ===== BLE SETUP =====
/**
 * Setup BLE server, service, and characteristics
 */
void setupBLE()
{
  // Create the BLE Device
  BLEDevice::init(BLE_DEVICE_NAME);
  BLEDevice::setPower(ESP_PWR_LVL_N12);

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create characteristics with consistent configuration
  encPosChara = pService->createCharacteristic(
      ENC_POS_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);

  encButtonChara = pService->createCharacteristic(
      ENC_BUTTON_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);

  mediaButtonChara = pService->createCharacteristic(
      MEDIA_SINGLEBUTTON_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);

  mediaDoubleButtonChara = pService->createCharacteristic(
      MEDIA_DOUBLEBUTTON_UUID,
      BLECharacteristic::PROPERTY_READ |
          BLECharacteristic::PROPERTY_WRITE |
          BLECharacteristic::PROPERTY_NOTIFY);

  // Add descriptor and set initial values
  encPosChara->addDescriptor(new BLE2902());
  encButtonChara->addDescriptor(new BLE2902());
  mediaButtonChara->addDescriptor(new BLE2902());
  mediaDoubleButtonChara->addDescriptor(new BLE2902());

  encPosChara->setValue(("0" + getBatteryLevel()).c_str());
  encButtonChara->setValue("0");
  mediaButtonChara->setValue("Master");
  mediaDoubleButtonChara->setValue("0");

  // Start the service
  pService->start();

  // Configure and start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(true);
  pAdvertising->setMinInterval(0x20); // 40ms
  pAdvertising->setMaxInterval(0x40);
  BLEDevice::startAdvertising();

  Serial.println("BLE server ready for connections");
}

// ===== ENCODER SETUP =====
/**
 * Setup encoder and button with interrupts
 */
void setupEncoder()
{
  // Configure encoder pins with pull-up resistors
  pinMode(ENCODER_PIN_DT, INPUT_PULLUP);
  pinMode(ENCODER_PIN_CLK, INPUT_PULLUP);
  pinMode(ENCODER_PIN_SW, INPUT_PULLUP);

  // Configure ESP32Encoder
  ESP32Encoder::useInternalWeakPullResistors = puType::up;
  encoder.attachHalfQuad(ENCODER_PIN_DT, ENCODER_PIN_CLK);
  encoder.clearCount();
  encoder.setFilter(1023); // Set filter to reduce noise

  // Configure button handlers for different actions
  encButton.attachClick([]()
                        {
    Serial.println("Button: Single click");
    if (deviceConnected) sendNotification(encButtonChara, "single click"); });

  encButton.attachDoubleClick([]()
                              {
    Serial.println("Button: Double click");
    if (deviceConnected) sendNotification(encButtonChara, "double click"); });

  encButton.attachMultiClick([]()
                             {
    Serial.println("Button: Multi click");
    if (deviceConnected) sendNotification(encButtonChara, "multi click"); });

  encButton.attachLongPressStop([]()
                                {
    Serial.println("Button: Long press");
    if (deviceConnected) sendNotification(encButtonChara, "long press release"); });

  Serial.println("Encoder and button initialized with interrupts");
}

// ===== ENCODER RESET =====
/**
 * Reset encoder position and notify clients
 */
void resetEncoder()
{
  encoder.clearCount();
  prevEncPosition = 0;
  currentEncPosition = 0;
  Serial.println("Encoder count auto-reset after inactivity");

  // Send reset notification to connected client
  if (deviceConnected)
  {
    String resetStr = "reset" + getBatteryLevel();
    Serial.println(resetStr.c_str());
    encPosChara->setValue(resetStr.c_str());
    encPosChara->notify();
  }

  // Update the activity timer
  lastActivityTime = millis();
}

/**
 * Check and handle BLE connection state changes
 */
void handleConnectionChanges()
{
  // Handle disconnection
  if (!deviceConnected && oldDeviceConnected)
  {
    delay(500); // Give BLE stack time to get ready
    pServer->startAdvertising();
    Serial.println("Restarting advertising");
    oldDeviceConnected = deviceConnected;
  }

  // Handle new connection
  if (deviceConnected && !oldDeviceConnected)
  {
    Serial.println("Client connected");
    oldDeviceConnected = deviceConnected;

    // When client connects, send current position
    String encPositionStr = String(currentEncPosition);
    String combinedStr = encPositionStr + getBatteryLevel();
    Serial.println(combinedStr.c_str());
    encPosChara->setValue(combinedStr.c_str());
    encPosChara->notify();
  }
}

// Add this function before loop()
void enterDeepSleep()
{
  Serial.println("Reed switch LOW - Entering deep sleep mode");

  // Save state for wake-up
  wasConnected = deviceConnected;

  // Disconnect BLE if connected to prevent issues on wake
  if (deviceConnected)
  {
    Serial.println("Disconnecting BLE before sleep");
    pServer->disconnect(pServer->getConnId()); // Disconnect the client
    // stop ble
    BLEDevice::deinit(true); // Deinitialize BLE stack
    // Client gets disconnected automatically when going to sleep
  }

  // Configure wakeup on HIGH state of reed switch (bitmask format)
  uint64_t wakeupBitMask = 1ULL << reedSwitchPin;
  esp_sleep_enable_ext1_wakeup(wakeupBitMask, ESP_EXT1_WAKEUP_ANY_HIGH);

  Serial.println("Going to sleep now");
  Serial.flush(); // Make sure all serial output is sent

  // Enter deep sleep
  esp_deep_sleep_start();

  // Code never reaches here - after waking, execution restarts at beginning of setup()
}

// ===== MAIN LOOP =====
void loop()
{
  // Process button events
  encButton.tick();

  // Process media button events
  for (int i = 0; i < NUM_MEDIA_BUTTONS; i++)
  {
    mediaButtons[i].button.tick();
  }

  // Get current encoder position
  currentEncPosition = encoder.getCount() / 2;

  // Handle encoder position changes
  if (currentEncPosition != prevEncPosition)
  {
    // Update activity timer
    lastActivityTime = millis();

    if (deviceConnected)
    {
      // Convert position to string and notify client
      String encPositionStr = String(currentEncPosition);
      String combinedStr = encPositionStr + getBatteryLevel();
      Serial.println(combinedStr.c_str());
      encPosChara->setValue(combinedStr.c_str());
      encPosChara->notify();

      Serial.print("Encoder position: ");
      Serial.println(currentEncPosition);
    }

    // Update previous position
    prevEncPosition = currentEncPosition;
  }

  // Auto-reset encoder after inactivity (only if not at zero)
  if (millis() - lastActivityTime > AUTO_RESET_TIMEOUT && currentEncPosition != 0)
  {
    resetEncoder();
  }

  // Handle BLE connection changes
  handleConnectionChanges();

  // Check reed switch state periodically to save power
  if (millis() - lastReedCheckTime > REED_CHECK_INTERVAL)
  {
    lastReedCheckTime = millis();

    // Read current reed switch state
    bool reedState = digitalRead(reedSwitchPin);

    // If reed switch becomes LOW, enter deep sleep
    if (reedState == LOW && prevReedState == HIGH)
    {
      Serial.println("Reed switch changed to LOW");
      enterDeepSleep();
    }

    prevReedState = reedState;
  }

  // Small delay to prevent CPU hogging
  delay(5);
}