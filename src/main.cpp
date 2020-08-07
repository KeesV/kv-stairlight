#include <Arduino.h>
#ifdef __AVR__
  #include <avr/power.h>
#endif

#include "stepManager.h"

#define SENSOR_PIN 2

StepManager stepManager;

void setup()
{
  Serial.begin(9600);

  stepManager.Setup();

  pinMode(SENSOR_PIN, INPUT);
}

void loop() {
  // if(digitalRead(SIGNAL_PIN)==HIGH) {
  //   Serial.println("Movement detected.");
  // } else {
  //   Serial.println("Did not detect movement.");
  // }
  // delay(1000);
  
  stepManager.Handle();
}

