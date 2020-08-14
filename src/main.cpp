#include <Arduino.h>
#include <IotWebConf.h>

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

#ifdef __AVR__
#include <avr/power.h>
#endif

#define NUM_STEPS 10
#define LEDS_PER_STEP 3

uint16_t stepInterval;
uint16_t stepFadeTime;
uint16_t holdTime;
uint8_t maxBrightness;


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

#define STRING_LEN 128
#define NUMBER_LEN 32

char stepIntervalParamValue[NUMBER_LEN];
char stepFadeTimeParamValue[NUMBER_LEN];
char holdTimeParamValue[NUMBER_LEN];
char maxBrightnessParamValue[NUMBER_LEN];

char mqttBrokerHostParamValue[STRING_LEN];
char mqttBrokerPortParamValue[NUMBER_LEN];
char mqttCommandTopicBaseParamValue[STRING_LEN];
char mqttStateTopicBaseParamValue[STRING_LEN];

bool needsReset = false;

#define CONFIG_VERSION "init" // key should be changed whenever config structure changes
IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
IotWebConfSeparator animationSeparator = IotWebConfSeparator("Animation Settings");
IotWebConfParameter stepIntervalParam = IotWebConfParameter("Step interval (ms)", "stepInterval", stepIntervalParamValue, NUMBER_LEN, "number", NULL, "250");
IotWebConfParameter stepFadeTimeParam = IotWebConfParameter("Step fade time (ms)", "stepFadeTime", stepFadeTimeParamValue, NUMBER_LEN, "number", NULL, "1000");
IotWebConfParameter holdTimeParam = IotWebConfParameter("Hold time (ms)", "holdTime", holdTimeParamValue, NUMBER_LEN, "number", NULL, "5000");
IotWebConfParameter maxBrightNexxParam = IotWebConfParameter("Max brightness", "maxBrightness", maxBrightnessParamValue, NUMBER_LEN, "number", NULL, "128", "min='1' max='255' step='1'");

IotWebConfSeparator mqttSeparator = IotWebConfSeparator("MQTT Settings");
IotWebConfParameter mqttBrokerHostParam = IotWebConfParameter("Mqtt host", "mqttHost", mqttBrokerHostParamValue, STRING_LEN, "text", NULL, "home.lan");
IotWebConfParameter mqttBrokerPortParam = IotWebConfParameter("Mqtt port", "mqttPort", mqttBrokerPortParamValue, STRING_LEN, "number", NULL, "8123");
IotWebConfParameter mqttCommandTopicBaseParam = IotWebConfParameter("Mqtt command topic", "mqttCommandTopic", mqttCommandTopicBaseParamValue, STRING_LEN, "text", NULL, "stairlight/command");
IotWebConfParameter mqttStateTopicBaseParam = IotWebConfParameter("Mqtt state topic", "mqttStateTopic", mqttStateTopicBaseParamValue, STRING_LEN, "text", NULL, "stairlight/state");

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
  s += "<title>Stairlight</title></head><body><p>Hello from under the stairs!</p>";
  s += "<p>Go to <a href='config'>configure page</a> to change settings.</p>";
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

      animations.StartAnimation(indexAnim, stepFadeTime, StepFadeAnimUpdate);
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
  StartStepFadeAnimation(0, RgbwColor(0), RgbwColor(maxBrightness));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, stepInterval, StairOnAnimUpdate);
}

void StartStepDownAnimation() {
  animationState[0].stepNum = NUM_STEPS - 1;
  animationState[0].direction = -1;
  // Light the first step
  StartStepFadeAnimation(NUM_STEPS - 1, RgbwColor(0), RgbwColor(maxBrightness));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, stepInterval, StairOnAnimUpdate);
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
  StartStepFadeAnimation(startStep, RgbwColor(maxBrightness), RgbwColor(0));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, stepInterval, StairOffAnimUpdate);
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

      StartStepFadeAnimation(nextStep, RgbwColor(0), RgbwColor(maxBrightness));
    } else {
      Serial.println("Lit all the steps. Holding...");
      animations.StartAnimation(0, holdTime, StairHoldAnimUpdate);
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

      StartStepFadeAnimation(nextStep, RgbwColor(maxBrightness), RgbwColor(0));
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

void configSaved()
{
  Serial.println("Configuration was updated.");
  needsReset = true;
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

  iotWebConf.addParameter(&animationSeparator);
  iotWebConf.addParameter(&stepIntervalParam);
  iotWebConf.addParameter(&stepFadeTimeParam);
  iotWebConf.addParameter(&holdTimeParam);
  iotWebConf.addParameter(&maxBrightNexxParam);
  iotWebConf.addParameter(&mqttSeparator);
  iotWebConf.addParameter(&mqttBrokerHostParam);
  iotWebConf.addParameter(&mqttBrokerPortParam);
  iotWebConf.addParameter(&mqttCommandTopicBaseParam);
  iotWebConf.addParameter(&mqttStateTopicBaseParam);

  iotWebConf.setConfigSavedCallback(&configSaved);

  // -- Initializing the configuration.
  iotWebConf.init();

  // -- Set up required URL handlers on the web server.
  server.on("/", handleRoot);
  server.on("/config", [] { iotWebConf.handleConfig(); });
  server.onNotFound([]() { iotWebConf.handleNotFound(); });

  stepInterval = atoi(stepIntervalParamValue);
  stepFadeTime = atoi(stepFadeTimeParamValue);
  holdTime = atoi(holdTimeParamValue);
  maxBrightness = atoi(maxBrightnessParamValue);

  Serial.print("Step interval: ");
  Serial.println(stepInterval);
  Serial.print("Step fade time: ");
  Serial.println(stepFadeTime);
  Serial.print("Hold time: ");
  Serial.println(holdTime);
  Serial.print("Max brightness: ");
  Serial.println(maxBrightness);
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
    //StartStepUpAnimation();
  }
  yield();
  iotWebConf.doLoop();
  yield();

  if(needsReset)
  {
    ESP.reset();
  }
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
