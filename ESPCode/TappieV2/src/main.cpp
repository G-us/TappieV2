#include <Arduino.h>
#include <ESP32Encoder.h>
#include <OneButton.h>
// put function declarations here:
#define encPin1 32
#define encPin2 35

#define encSwitch 34

ESP32Encoder encoder;

OneButton button(encSwitch, true);

void click();

void setup()
{
  // put your setup code here, to run once:
  pinMode(encPin1, INPUT);
  pinMode(encPin2, INPUT);
  pinMode(encSwitch, INPUT_PULLUP);
  Serial.begin(115200);
  Serial.println("Hello World");
  Serial.println("Encoder Test");
  encoder.attachHalfQuad(encPin1, encPin2);
  encoder.clearCount();
  button.attachClick(click);
}

void loop()
{
  // put your main code here, to run repeatedly:
  long newPosition = encoder.getCount() / 2;
  Serial.println(newPosition);
  button.tick();
  delay(10);
}

void click()
{
  encoder.clearCount();
}
