#include "step.h"

Step::Step() {
    dimValue_w = 0;
    state = StepOff;
}

void Step::Setup(Adafruit_NeoPixel *strip, int startPixel, int length) {
    this->strip = strip;
    this->startPixel = startPixel;
    this->length = length;
}

void Step::FadeIn() {
    state = FadingIn;
}

void Step::FadeOut() {
    state = FadingOut;
}

void Step::UpdateStrip() {
    for(uint16_t i=0; i<this->length; i++ )
    {
        this->strip->setPixelColor(i+startPixel, this->strip->Color(0,0,0,dimValue_w));
    }
}

void Step::Handle() {
    unsigned long currentMillis = millis();

    switch(state) {
        case StepOff:
            dimValue_w = 0;
        break;

        case FadingIn:
            if(currentMillis - this->lastDimUpdate > this->dimInterval)
            {
                this->lastDimUpdate = currentMillis;
                dimValue_w++;
            }
            
            if(dimValue_w >= 255)
                state = StepOn;
            break;

        case StepOn:
            dimValue_w = 255;
            break;

        case FadingOut:
            if(currentMillis - this->lastDimUpdate > this->dimInterval)
            {
                this->lastDimUpdate = currentMillis;
                dimValue_w--;
            }

            if(dimValue_w <= 0)
                state = StepOff;
            break;
    }

    UpdateStrip();
}