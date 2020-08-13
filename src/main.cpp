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


#define SENSOR_UP_PIN D1
#define SENSOR_DOWN_PIN D2

const uint8_t AnimationChannels = NUM_STEPS+1; // One channel per step + 1 as overall animation coordinator

NeoPixelBus<NeoGrbwFeature, NeoEsp8266Dma800KbpsMethod> strip(NUM_STEPS * LEDS_PER_STEP);
NeoPixelAnimator animations(AnimationChannels); // NeoPixel animation management object
NeoGamma<NeoGammaTableMethod> colorGamma; // for any fade animations, best to correct gamma


void StairOnAnimUpdate(const AnimationParam& param);
void StairOffAnimUpdate(const AnimationParam& param);
void StairHoldAnimUpdate(const AnimationParam& param);
void StepFadeAnimUpdate(const AnimationParam& param);

struct StepAnimationState
{
  RgbwColor StartColor;
  RgbwColor EndingColor;
  int8_t stepNum;
  int8_t direction; // 1 = up, -1 = down
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

void StartStepFadeAnimation(int8_t stepNumber, RgbwColor startColor, RgbwColor endColor) {
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
  animationState[0].direction = 1;
  // Light the first step
  StartStepFadeAnimation(0, RgbwColor(0), RgbwColor(MAX_BRIGHTNESS));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, STEP_INTERVAL, StairOnAnimUpdate);
}

void StartStepDownAnimation() {
  animationState[0].stepNum = NUM_STEPS - 1;
  animationState[0].direction = -1;
  // Light the first step
  StartStepFadeAnimation(NUM_STEPS - 1, RgbwColor(0), RgbwColor(MAX_BRIGHTNESS));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, STEP_INTERVAL, StairOnAnimUpdate);
}

void StartStepOffAnimation() {
  int8_t startStep;

  if(animationState[0].direction == 1)
  {
    startStep = 0;
  } else {
    startStep = NUM_STEPS - 1;
  }

  animationState[0].stepNum = startStep;
  // Light the first step
  StartStepFadeAnimation(startStep, RgbwColor(MAX_BRIGHTNESS), RgbwColor(0));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, STEP_INTERVAL, StairOffAnimUpdate);
}

void StairOnAnimUpdate(const AnimationParam& param) 
{
  if(param.state == AnimationState_Completed)
  {
    Serial.print("Getting ready to light the next step: ");
    int nextStep = animationState[param.index].stepNum + animationState[param.index].direction;
    Serial.println(nextStep);
    
    if(nextStep < NUM_STEPS && nextStep >= 0)
    {
      animationState[param.index].stepNum = nextStep;
      animations.RestartAnimation(param.index); // Restart this animation to reset the "timer"

      StartStepFadeAnimation(nextStep, RgbwColor(0), RgbwColor(MAX_BRIGHTNESS));
    } else {
      Serial.println("Lit all the steps. Holding...");
      animations.StartAnimation(0, HOLD_TIME, StairHoldAnimUpdate);
    }
  }
}

void StairOffAnimUpdate(const AnimationParam& param) 
{
  if(param.state == AnimationState_Completed)
  {
    Serial.print("Getting ready to dim the next step: ");
    int nextStep = animationState[param.index].stepNum + animationState[param.index].direction;
    Serial.println(nextStep);
    
    if(nextStep < NUM_STEPS && nextStep >= 0)
    {
      animationState[param.index].stepNum = nextStep;
      animations.RestartAnimation(param.index); // Restart this animation to reset the "timer"

      StartStepFadeAnimation(nextStep, RgbwColor(MAX_BRIGHTNESS), RgbwColor(0));
    } else {
      Serial.println("Dimmed all the steps. Done!");
    }
  }
}

void StairHoldAnimUpdate(const AnimationParam& param) 
{
  if(param.state == AnimationState_Completed)
  {
    Serial.println("Done holding, starting dimming!");
    
    StartStepOffAnimation();
  }
}

void setup()
{
  pinMode(SENSOR_UP_PIN, INPUT);
  pinMode(SENSOR_DOWN_PIN, INPUT);

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
}

void loop()
{
  if(animations.IsAnimating())
  {
    animations.UpdateAnimations();
    strip.Show();
  } else {
    // if(digitalRead(SENSOR_UP_PIN) == HIGH)
    // {
    //   Serial.println("Movement going up detected!");
    //   StartStepUpAnimation();
    // }
    // } else if(digitalRead(SENSOR_DOWN_PIN) == HIGH)
    // {
    //   Serial.println("Movement going down detected!");
    //   StartStepDownAnimation();
    // }
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
