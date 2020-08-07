#include "stepManager.h"

StepManager::StepManager()
{
    // Parameter 1 = number of pixels in strip
    // Parameter 2 = Arduino pin number (most are valid)
    // Parameter 3 = pixel type flags, add together as needed:
    //   NEO_KHZ800  800 KHz bitstream (most NeoPixel products w/WS2812 LEDs)
    //   NEO_KHZ400  400 KHz (classic 'v1' (not v2) FLORA pixels, WS2811 drivers)
    //   NEO_GRB     Pixels are wired for GRB bitstream (most NeoPixel products)
    //   NEO_RGB     Pixels are wired for RGB bitstream (v1 FLORA pixels, not v2)
    //   NEO_RGBW    Pixels are wired for RGBW bitstream (NeoPixel RGBW products)
    this->strip = Adafruit_NeoPixel(NUM_STEPS * LEDS_PER_STEP, STRIP_PIN, NEO_GRBW + NEO_KHZ800);

    // IMPORTANT: To reduce NeoPixel burnout risk, add 1000 uF capacitor across
    // pixel power leads, add 300 - 500 Ohm resistor on first pixel's data input
    // and minimize distance between Arduino and first pixel.  Avoid connecting
    // on a live circuit...if you must, connect GND first.
    state = Off;
}

void StepManager::Setup()
{
    for (int i = 0; i < NUM_STEPS; i++)
    {
        steps[i].Setup(&strip, i * LEDS_PER_STEP, LEDS_PER_STEP);
    }

    this->strip.begin();
    this->strip.setBrightness(50);
    this->strip.show(); // Initialize all pixels to 'off'
}

void StepManager::Handle()
{
    unsigned long currentMillis = millis();

    switch (state)
    {
    case Off:
        if (currentMillis - offTime > offInterval)
        {
            stepToStart = 0;
            state = GoingUpOn;
        }
        break;

    case GoingUpOn:
        if (currentMillis - this->lastStepStartTime > nextStepInterval && stepToStart < NUM_STEPS)
        {
            // It's time to start the next step
            this->lastStepStartTime = currentMillis;
            this->steps[stepToStart].FadeIn();
            stepToStart++;
        }

        if (stepToStart >= NUM_STEPS)
        {
            this->onTime = currentMillis;
            state = On;
        }
        break;

    case On:
        if (currentMillis - onTime > onInterval)
        {
            stepToStart = 0;
            state = GoingUpOff;
        }
        break;

    case GoingUpOff:
        if (currentMillis - this->lastStepStartTime > nextStepInterval && stepToStart < NUM_STEPS)
        {
            // It's time to start the next step
            this->lastStepStartTime = currentMillis;
            this->steps[stepToStart].FadeOut();
            stepToStart++;
        }

        if (stepToStart >= NUM_STEPS)
        {
            this->offTime = currentMillis;
            state = Off;
        }
        break;
    }

    for (int i = 0; i < NUM_STEPS; i++)
    {
        this->steps[i].Handle();
    }
    this->strip.show();
}