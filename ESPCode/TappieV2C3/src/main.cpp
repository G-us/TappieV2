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
#include <AiEsp32RotaryEncoder.h>
#include <OneButton.h>
#include <esp_sleep.h>
#include <driver/periph_ctrl.h>
#include <driver/adc.h>

// ===== PIN DEFINITIONS =====
const uint8_t ENCODER_PIN_DT = 1;
const uint8_t ENCODER_PIN_CLK = 0;
const uint8_t ENCODER_PIN_SW = 2;
#define ENCODER_STEPS 4

gpio_num_t reedSwitchPin = GPIO_NUM_5; // GPIO pin for reed switch

#define AuxButtonPin 6
#define GamingButtonPin 7
#define MediaButtonPin 8
#define ChatButtonPin 5
#define MasterButtonPin 10

#define BATTERY_PIN 3 // GPIO pin for battery level measurement

// ===== BLE DEFINITIONS =====
#define BLE_DEVICE_NAME "TappieV2"
#define SERVICE_UUID "738b66f1-91b7-4f25-8ab8-31d38d56541a"
#define ENC_POS_UUID "a9c8c7b4-fb55-4d27-99e4-2c14b5812546"
#define ENC_BUTTON_UUID "0c2f5fbe-c20f-49ec-8c7c-ce0c9358e574"
#define MEDIA_SINGLEBUTTON_UUID "9ff67916-665f-4489-b257-46d118b1e5eb"
#define MEDIA_DOUBLEBUTTON_UUID "66f1ab02-c93d-44fe-8ca9-5e8bdbb2fe80"

// ===== TIMING CONSTANTS =====
#define AUTO_RESET_TIMEOUT 5000       // 5 seconds in milliseconds
#define BUTTON_NOTIFY_DELAY 100       // 100ms delay after button notifications
#define BATTERY_CHECK_INTERVAL 300000 // 1 minute in milliseconds

// ===== POWER MANAGEMENT CONSTANTS =====
#define LIGHT_SLEEP_TIMEOUT 10000  // 10 seconds of inactivity before light sleep
#define INACTIVE_CPU_FREQ 40       // CPU MHz when inactive
#define ACTIVE_CPU_FREQ 80         // CPU MHz when active
#define BLE_MIN_CONN_INTERVAL 0x40 // 80ms (was 0x20 = 40ms)
#define BLE_MAX_CONN_INTERVAL 0x80 // 160ms (was 0x40)
#define DISABLE_UNUSED_PERIPHERALS true

// Add to STATE VARIABLES section
int currentCpuFreq = ACTIVE_CPU_FREQ;

int lastBatteryCheckTime = 0; // Last time battery level was checked

AiEsp32RotaryEncoder rotaryEncoder = AiEsp32RotaryEncoder(ENCODER_PIN_CLK, ENCODER_PIN_DT, ENCODER_PIN_SW, ENCODER_STEPS);

// ===== MEDIA BUTTON DEFINITIONS =====
struct MediaButton
{
  const char *name;
  uint8_t pin;
  OneButton button;
};

// ===== GLOBAL VARIABLES =====
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

int lastStateCLK;
int currentStateCLK;

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
class MyServerCallbacks;

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

void encoderRotaryLoop()
{
  static unsigned long lastTimeTurned = 0;
  if (rotaryEncoder.encoderChanged() && millis() - lastTimeTurned > 50)
  {
    lastTimeTurned = millis();
    String positionStr = String(rotaryEncoder.readEncoder() + getBatteryLevel());
    Serial.println(positionStr.c_str());
    if (deviceConnected)
    {
      encPosChara->setValue(positionStr.c_str());
      encPosChara->notify();
    }
  }
}

void IRAM_ATTR readEncoderISR()
{
  rotaryEncoder.readEncoder_ISR();
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

  float voltage = analogReadMilliVolts(BATTERY_PIN) * 2; // Read battery voltage
  int batteryLevel = (int)(voltage / 4200 * 100);        // Convert to percentage
  // Use a proper separator format: " batteryLevel=" followed by the value
  String batteryStr = String(" " + String(batteryLevel));
  return batteryStr;
}

/**
 * Function to optimize power for unused GPIOs
 */
void configureUnusedGPIOs()
{
  // List of GPIO pins not used in this application - adjust based on your hardware
  const int unusedPins[] = {0, 12, 13, 14, 15, 16, 19, 21, 23, 25, 26, 27};
  const int numUnusedPins = sizeof(unusedPins) / sizeof(unusedPins[0]);

  // Configure unused pins as inputs with no pullups to minimize power
  for (int i = 0; i < numUnusedPins; i++)
  {
    pinMode(unusedPins[i], INPUT);
    gpio_pulldown_dis(static_cast<gpio_num_t>(unusedPins[i]));
    gpio_pullup_dis(static_cast<gpio_num_t>(unusedPins[i]));
  }

  Serial.println("Unused GPIOs configured for power saving");
}

/**
 * Function to disable unused peripherals
 */
void disableUnusedPeripherals()
{

  // Disable UART2

  // Disable I2C if not used
  periph_module_disable(PERIPH_I2C0_MODULE);

  Serial.println("Unused peripherals disabled for power saving");
}

class MyServerCallbacks : public BLEServerCallbacks
{
  void onConnect(BLEServer *pServer)
  {
    deviceConnected = true;
    Serial.println("Device connected");
    resetEncoder(); // Reset encoder position on new connection
  }

  void onDisconnect(BLEServer *pServer)
  {
    deviceConnected = false;
    Serial.println("Device disconnected");
  }
};

// Modify setupBLE() to optimize BLE parameters
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
  pAdvertising->setMinInterval(BLE_MIN_CONN_INTERVAL); // Increased interval (80ms)
  pAdvertising->setMaxInterval(BLE_MAX_CONN_INTERVAL); // Increased interval (160ms)
  BLEDevice::startAdvertising();

  Serial.println("BLE server ready with optimized power settings");
}

// ===== ENCODER SETUP =====
/**
 * Setup encoder and button with interrupts
 */
void setupEncoder()
{

  rotaryEncoder.begin();
  rotaryEncoder.setup(readEncoderISR);
  rotaryEncoder.disableAcceleration();

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
  Serial.println("Encoder count auto-reset after inactivity");

  // Send reset notification to connected client
  if (deviceConnected)
  {
    rotaryEncoder.reset(0);
    String resetStr = "reset" + getBatteryLevel();
    Serial.println(resetStr.c_str());
    encPosChara->setValue(resetStr.c_str());
    encPosChara->notify();
  }

  // Update the activity timer
  lastActivityTime = millis();
}

// Global variables for encoder state tracking
volatile int8_t encoderDelta = 0;         // Track encoder movement, processed in main loop
volatile uint8_t encoderState = 0;        // Current quadrature state
volatile bool fullDetentRotation = false; // Flag to indicate a complete detent rotation


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

void setup()
{

  // Initialize serial for debugging
  Serial.begin(115200);

  // Configure reed switch pin
  pinMode(reedSwitchPin, INPUT_PULLUP);

  pinMode(BATTERY_PIN, INPUT); // Set battery pin as input

  // //Set initial CPU frequency
  // setCpuFrequencyMhz(ACTIVE_CPU_FREQ);
  // currentCpuFreq = ACTIVE_CPU_FREQ;

  // //Configure unused GPIOs to save power
  // configureUnusedGPIOs();

  // Disable unused peripherals if enabled
  // if (DISABLE_UNUSED_PERIPHERALS)
  // {
  //   disableUnusedPeripherals();
  // }

  // Setup hardware components
  setupEncoder();
  setupMediaButtons();
  setupBLE();

  Serial.println("Setup complete!");
  // digitalWrite(1, HIGH); // Set reed switch pin to HIGH to avoid false trigger
}

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
  // esp_sleep_enable_ext1_wakeup(wakeupBitMask, ESP_EXT1_WAKEUP_ANY_HIGH);

  Serial.println("Going to sleep now");
  Serial.flush(); // Make sure all serial output is sent

  // Enter deep sleep
  esp_deep_sleep_start();

  // Code never reaches here - after waking, execution restarts at beginning of setup()
}

// ===== MAIN LOOP =====
void loop()
{

  // // Process button events
  encButton.tick();

  // Process media button events
  for (int i = 0; i < NUM_MEDIA_BUTTONS; i++)
  {
    mediaButtons[i].button.tick();
  }
  encoderRotaryLoop();
  // Handle BLE connection changes
  handleConnectionChanges();

  // Check reed switch state periodically
  if (millis() - lastReedCheckTime > REED_CHECK_INTERVAL)
  {
    lastReedCheckTime = millis();

    // Read current reed switch state
    bool reedState = digitalRead(reedSwitchPin);

    // If reed switch becomes LOW, enter deep sleep
    if (reedState == LOW && prevReedState == HIGH)
    {
      Serial.println("Reed switch changed to LOW, dont forget to uncomment the deep sleep line in the code");
      // enterDeepSleep();           // Uncomment this line to enable deep sleep on reed switch LOW   REMEMBER TO UNCOMMENT YOU IDIOT AAAAA
    }

    prevReedState = reedState;
  }

  if (millis() - lastBatteryCheckTime > BATTERY_CHECK_INTERVAL)
  {
    lastBatteryCheckTime = millis();
    resetEncoder(); // Reset encoder position every minute
  }

  delay(2); // Small delay to avoid busy-waiting
}