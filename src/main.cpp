#include <Arduino.h>
#include <IotWebConf.h>

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

#define NUM_STEPS 10
#define LEDS_PER_STEP 3
#define STEP_INTERVAL 250
#define STEP_FADE_TIME 1000
#define HOLD_TIME 5000
#define MAX_BRIGHTNESS 128


#define SENSOR_PIN 2

const uint8_t AnimationChannels = NUM_STEPS+1; // One channel per step + 1 as overall animation coordinator

NeoPixelBus<NeoGrbwFeature, NeoEsp8266Dma800KbpsMethod> strip(NUM_STEPS * LEDS_PER_STEP);
NeoPixelAnimator animations(AnimationChannels); // NeoPixel animation management object
NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma


void StairUpOnAnimUpdate(const AnimationParam& param);
void StairUpOffAnimUpdate(const AnimationParam& param);
void StairUpHoldAnimUpdate(const AnimationParam& param);
void StepFadeAnimUpdate(const AnimationParam& param);

struct StepAnimationState
{
  RgbwColor StartColor;
  RgbwColor EndingColor;
  int stepNum;
};

StepAnimationState animationState[AnimationChannels];

// -- Initial name of the Thing. Used e.g. as SSID of the own Access Point.
const char thingName[] = "stairlight01";

// -- Initial password to connect to the Thing, when it creates an own Access Point.
const char wifiInitialApPassword[] = "smrtTHNG8266";

DNSServer dnsServer;
WebServer server(80);

IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword);

/**
 * Handle web requests to "/" path.
 */
void handleRoot()
{
  // -- Let IotWebConf test and handle captive portal requests.
  if (iotWebConf.handleCaptivePortal())
  {
    // -- Captive portal request were already served.
    return;
  }
  String s = "<!DOCTYPE html><html lang=\"en\"><head><meta name=\"viewport\" content=\"width=device-width, initial-scale=1, user-scalable=no\"/>";
  s += "<title>IotWebConf 01 Minimal</title></head><body>Hello world!";
  s += "Go to <a href='config'>configure page</a> to change settings.";
  s += "</body></html>\n";

  server.send(200, "text/html", s);
}

void StartStepFadeAnimation(int stepNumber, RgbwColor startColor, RgbwColor endColor) {
  Serial.print("Animating step with index: ");
  Serial.println(stepNumber);
  
  uint16_t indexAnim;

  if (animations.NextAvailableAnimation(&indexAnim, 1))
  {
      animationState[indexAnim].StartColor = startColor;
      animationState[indexAnim].EndingColor = endColor;
      animationState[indexAnim].stepNum = stepNumber;

      animations.StartAnimation(indexAnim, STEP_FADE_TIME, StepFadeAnimUpdate);
  }
}

void StepFadeAnimUpdate(const AnimationParam& param)
{
  RgbwColor updatedColor = RgbwColor::LinearBlend(animationState[param.index].StartColor, animationState[param.index].EndingColor, param.progress);
  int startPixel = animationState[param.index].stepNum * LEDS_PER_STEP;

  for(uint8_t i = 0; i < LEDS_PER_STEP; i++)
  {
    strip.SetPixelColor(startPixel + i, colorGamma.Correct(updatedColor));
  }
}

void StartStepUpAnimation() {
  animationState[0].stepNum = 0;
  // Light the first step
  StartStepFadeAnimation(0, RgbwColor(0), RgbwColor(MAX_BRIGHTNESS));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, STEP_INTERVAL, StairUpOnAnimUpdate);
}

void StartStepUpOffAnimation() {
  animationState[0].stepNum = 0;
  // Light the first step
  StartStepFadeAnimation(0, RgbwColor(MAX_BRIGHTNESS), RgbwColor(0));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, STEP_INTERVAL, StairUpOffAnimUpdate);
}

void StairUpOnAnimUpdate(const AnimationParam& param) 
{
  if(param.state == AnimationState_Completed)
  {
    Serial.println("Getting ready to light the next step...");
    int nextStep = animationState[param.index].stepNum + 1;
    
    if(nextStep < NUM_STEPS)
    {
      animationState[param.index].stepNum = nextStep;
      animations.RestartAnimation(param.index); // Restart this animation to reset the "timer"

      StartStepFadeAnimation(nextStep, RgbwColor(0), RgbwColor(MAX_BRIGHTNESS));
    } else {
      Serial.println("Lit all the steps. Holding...");
      animations.StartAnimation(0, HOLD_TIME, StairUpHoldAnimUpdate);
    }
  }
}

void StairUpOffAnimUpdate(const AnimationParam& param) 
{
  if(param.state == AnimationState_Completed)
  {
    Serial.println("Getting ready to dim the next step...");
    int nextStep = animationState[param.index].stepNum + 1;
    
    if(nextStep < NUM_STEPS)
    {
      animationState[param.index].stepNum = nextStep;
      animations.RestartAnimation(param.index); // Restart this animation to reset the "timer"

      StartStepFadeAnimation(nextStep, RgbwColor(MAX_BRIGHTNESS), RgbwColor(0));
    } else {
      Serial.println("Dimmed all the steps. Done!");
      
    }
  }
}

void StairUpHoldAnimUpdate(const AnimationParam& param) 
{
  if(param.state == AnimationState_Completed)
  {
    Serial.println("Done holding, starting dimming!");
    
    animationState[0].stepNum = 0;
    StartStepUpOffAnimation();
  }
}

void setup()
{
  pinMode(SENSOR_PIN, INPUT);

  Serial.begin(115200);
  while (!Serial); // wait for serial attach
  Serial.println();
  Serial.println("Starting up...");
  Serial.flush();

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();
  

  // -- Initializing the configuration.
  iotWebConf.init();

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() { iotWebConf.handleNotFound(); });

  IPAddress ip = WiFi.softAPIP();
  Serial.print("IP address: ");
  Serial.println(ip.toString());

  StartStepUpAnimation();
}

void loop()
{
  if(animations.IsAnimating())
  {
    animations.UpdateAnimations();
    strip.Show();
  } else {
    StartStepUpAnimation();
  }
  yield();
  iotWebConf.doLoop();
  yield();
  // if(digitalRead(SIGNAL_PIN)==HIGH) {
  //   Serial.println("Movement detected.");
  // } else {
  //   Serial.println("Did not detect movement.");
  // }
  // delay(1000);
  // stepManager.Handle();
  // yield();
  // iotWebConf.doLoop();
  // yield();

  
}
