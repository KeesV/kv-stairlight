#ifndef __STEP_H__
#define __STEP_H__

#include <Arduino.h>
#include <Adafruit_NeoPixel.h>

enum StepState { StepOff, FadingIn, StepOn, FadingOut };

class Step {
    private:
        Adafruit_NeoPixel *strip;
        int startPixel;
        int length;
        uint8_t dimValue_w;
        StepState state;
        void UpdateStrip();

        unsigned long lastDimUpdate = 0;
        unsigned long dimInterval = 10;

    public:
        Step();
        void Setup(Adafruit_NeoPixel *strip, int startPixel, int length);
        void Handle();
        void FadeIn();
        void FadeOut();

};

#endif