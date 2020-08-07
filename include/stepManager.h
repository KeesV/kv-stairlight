#ifndef __STEPMANAGER_H__
#define __STEPMANAGER_H__

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

#include "step.h"

#define STRIP_PIN 7
#define NUM_STEPS 10
#define LEDS_PER_STEP 3

enum StepManagerState { Off, GoingUpOn, GoingUpOff, On, GoingDownOn, GoingDownOff };

class StepManager {
    private:
        Step steps[NUM_STEPS];
        Adafruit_NeoPixel strip;
        StepManagerState state;

        unsigned long lastStepStartTime = 0;
        unsigned long nextStepInterval = 250;
        unsigned long onTime = 0;
        unsigned long onInterval = 4000;
        unsigned long offTime = 0;
        unsigned long offInterval = 3000;
        uint8_t stepToStart = 0;

    public:
        StepManager();
        void Setup();
        void Handle();
};

#endif