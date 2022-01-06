#include <Arduino.h>
#include <ArduinoOTA.h>
#include <IotWebConf.h>
#include <IotWebConfUsing.h>

#include <PubSubClient.h>
#include <ArduinoJson.h>
const char *mqtt_server = "home.lan";
const char *mqtt_user = "esp_woonkamer";
const char *mqtt_pass = "1A1Fbi8D6gEbaPQU98XP4ERNAcZoIeYl";

WiFiClient espClient;
PubSubClient mqttClient(espClient);
StaticJsonDocument<256> receivedJson;

#include <NeoPixelBus.h>
#include <NeoPixelAnimator.h>

#include "RemoteDebug.h"
#include "RemoteDebugger.h"
RemoteDebug Debug;

#ifdef __AVR__
#include <avr/power.h>
#endif

#define NUM_STEPS 16     // 16
#define LEDS_PER_STEP 29 //29

uint16_t stepInterval;
uint16_t stepFadeTime;
uint16_t holdTime;
uint8_t maxBrightness;

#define SENSOR_UP_PIN D1
#define SENSOR_DOWN_PIN D2

const uint8_t AnimationChannels = NUM_STEPS + 1; // One channel per step + 1 as overall animation coordinator

// Connect NeoPixel to GPIO3 on ESP8266 = RX on Wemos D1 mini
NeoPixelBus<NeoGrbwFeature, NeoEsp8266Dma800KbpsMethod> strip(NUM_STEPS *LEDS_PER_STEP);
NeoPixelAnimator animations(AnimationChannels); // NeoPixel animation management object
NeoGamma<NeoGammaTableMethod> colorGamma;       // for any fade animations, best to correct gamma

void StairOnAnimUpdate(const AnimationParam &param);
void StairOffAnimUpdate(const AnimationParam &param);
void StairHoldAnimUpdate(const AnimationParam &param);
void StepFadeAnimUpdate(const AnimationParam &param);

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

#define CONFIG_VERSION "init1" // key should be changed whenever config structure changes
IotWebConf iotWebConf(thingName, &dnsServer, &server, wifiInitialApPassword, CONFIG_VERSION);
iotwebconf::ParameterGroup animationGroup = iotwebconf::ParameterGroup("Animation Settings");
iotwebconf::NumberParameter stepIntervalParam = iotwebconf::NumberParameter("Step interval (ms)", "stepInterval", stepIntervalParamValue, NUMBER_LEN, "number", NULL, "250");
iotwebconf::NumberParameter stepFadeTimeParam = iotwebconf::NumberParameter("Step fade time (ms)", "stepFadeTime", stepFadeTimeParamValue, NUMBER_LEN, "number", NULL, "1000");
iotwebconf::NumberParameter holdTimeParam = iotwebconf::NumberParameter("Hold time (ms)", "holdTime", holdTimeParamValue, NUMBER_LEN, "number", NULL, "5000");
iotwebconf::NumberParameter maxBrightNexxParam = iotwebconf::NumberParameter("Max brightness", "maxBrightness", maxBrightnessParamValue, NUMBER_LEN, "number", NULL, "128", "min='1' max='255' step='1'");

iotwebconf::ParameterGroup mqttGroup = iotwebconf::ParameterGroup("MQTT Settings");
iotwebconf::TextParameter mqttBrokerHostParam = iotwebconf::TextParameter("Mqtt host", "mqttHost", mqttBrokerHostParamValue, STRING_LEN, "text", NULL, "home.lan");
iotwebconf::NumberParameter mqttBrokerPortParam = iotwebconf::NumberParameter("Mqtt port", "mqttPort", mqttBrokerPortParamValue, STRING_LEN, "number", NULL, "8123");
iotwebconf::TextParameter mqttCommandTopicBaseParam = iotwebconf::TextParameter("Mqtt command topic", "mqttCommandTopic", mqttCommandTopicBaseParamValue, STRING_LEN, "text", NULL, "stairlight/command");
iotwebconf::TextParameter mqttStateTopicBaseParam = iotwebconf::TextParameter("Mqtt state topic", "mqttStateTopic", mqttStateTopicBaseParamValue, STRING_LEN, "text", NULL, "stairlight/state");

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

void StartStepFadeAnimation(int8_t stepNumber, RgbwColor startColor, RgbwColor endColor)
{
  // Serial.print("Animating step with index: ");
  // Serial.println(stepNumber);

  uint16_t indexAnim;

  if (animations.NextAvailableAnimation(&indexAnim, 1))
  {
    animationState[indexAnim].StartColor = startColor;
    animationState[indexAnim].EndingColor = endColor;
    animationState[indexAnim].stepNum = stepNumber;

    animations.StartAnimation(indexAnim, stepFadeTime, StepFadeAnimUpdate);
  }
}

void StepFadeAnimUpdate(const AnimationParam &param)
{
  RgbwColor updatedColor = RgbwColor::LinearBlend(animationState[param.index].StartColor, animationState[param.index].EndingColor, param.progress);
  int startPixel = animationState[param.index].stepNum * LEDS_PER_STEP;

  for (uint8_t i = 0; i < LEDS_PER_STEP; i++)
  {
    strip.SetPixelColor(startPixel + i, colorGamma.Correct(updatedColor));
  }
}

void StartStepUpAnimation()
{
  animationState[0].stepNum = 0;
  animationState[0].direction = 1;
  // Light the first step
  StartStepFadeAnimation(0, RgbwColor(0), RgbwColor(maxBrightness));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, stepInterval, StairOnAnimUpdate);
}

void StartStepDownAnimation()
{
  animationState[0].stepNum = NUM_STEPS - 1;
  animationState[0].direction = -1;
  // Light the first step
  StartStepFadeAnimation(NUM_STEPS - 1, RgbwColor(0), RgbwColor(maxBrightness));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, stepInterval, StairOnAnimUpdate);
}

void StartStepOffAnimation()
{
  int8_t startStep;

  if (animationState[0].direction == 1)
  {
    startStep = 0;
  }
  else
  {
    startStep = NUM_STEPS - 1;
  }

  animationState[0].stepNum = startStep;
  // Light the first step
  StartStepFadeAnimation(startStep, RgbwColor(maxBrightness), RgbwColor(0));
  // And start an animation that will trigger the next steps
  animations.StartAnimation(0, stepInterval, StairOffAnimUpdate);
}

void StairOnAnimUpdate(const AnimationParam &param)
{
  if (param.state == AnimationState_Completed)
  {
    //Serial.print("Getting ready to light the next step: ");
    int nextStep = animationState[param.index].stepNum + animationState[param.index].direction;
    //Serial.println(nextStep);

    if (nextStep < NUM_STEPS && nextStep >= 0)
    {
      animationState[param.index].stepNum = nextStep;
      animations.RestartAnimation(param.index); // Restart this animation to reset the "timer"

      StartStepFadeAnimation(nextStep, RgbwColor(0), RgbwColor(maxBrightness));
    }
    else
    {
      Serial.println("Lit all the steps. Holding...");
      animations.StartAnimation(0, holdTime, StairHoldAnimUpdate);
    }
  }
}

void StairOffAnimUpdate(const AnimationParam &param)
{
  if (param.state == AnimationState_Completed)
  {
    //Serial.print("Getting ready to dim the next step: ");
    int nextStep = animationState[param.index].stepNum + animationState[param.index].direction;
    //Serial.println(nextStep);

    if (nextStep < NUM_STEPS && nextStep >= 0)
    {
      animationState[param.index].stepNum = nextStep;
      animations.RestartAnimation(param.index); // Restart this animation to reset the "timer"

      StartStepFadeAnimation(nextStep, RgbwColor(maxBrightness), RgbwColor(0));
    }
    else
    {
      //Serial.println("Dimmed all the steps. Done!");
    }
  }
}

void StairHoldAnimUpdate(const AnimationParam &param)
{
  if (param.state == AnimationState_Completed)
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

void onMqttReceiveMessage(char *topic, byte *p_payload, unsigned int p_length)
{
  // concat the payload into a string
  char payload[p_length + 1];
  for (unsigned int i = 0; i < p_length; i++)
  {
    payload[i] = (char)p_payload[i];
  }
  payload[p_length] = '\0';

  // Deserialize the JSON document
  DeserializationError error = deserializeJson(receivedJson, payload);

  // // Test if parsing succeeds.
  if (error)
  {
    Serial.print(F("deserializeJson() failed: "));
    return;
  }

  const char *state = receivedJson["state"];

  if (strcmp(state, "ON") == 0 && !animations.IsAnimating())
  {
    mqttClient.publish("stairlight/state", "Going UP");

    StartStepUpAnimation();
  }
}

void reconnectMqtt()
{
  Serial.print("Attempting MQTT connection...");
  // Create a random client ID
  String clientId = "stairlight-";
  clientId += String(random(0xffff), HEX);
  // Attempt to connect
  if (mqttClient.connect(clientId.c_str(), mqtt_user, mqtt_pass))
  {
    Serial.println("connected");
    // Once connected, publish an announcement...
    mqttClient.publish("stairlight/state", "hello world");
    mqttClient.subscribe("stairmotion/motion");
  }
  else
  {
    Serial.print("failed, rc=");
    Serial.print(mqttClient.state());
    Serial.println(" try again in 5 seconds");
    // Wait 5 seconds before retrying
    delay(5000);
  }
}

void setup()
{
  pinMode(SENSOR_UP_PIN, INPUT);
  pinMode(SENSOR_DOWN_PIN, INPUT);

  Serial.begin(115200);
  while (!Serial)
    ; // wait for serial attach
  Serial.println();
  Serial.println("Starting up...");
  Serial.flush();

  // this resets all the neopixels to an off state
  strip.Begin();
  strip.Show();

  animationGroup.addItem(&stepIntervalParam);
  animationGroup.addItem(&stepFadeTimeParam);
  animationGroup.addItem(&holdTimeParam);
  animationGroup.addItem(&maxBrightNexxParam);
  
  mqttGroup.addItem(&mqttBrokerHostParam);
  mqttGroup.addItem(&mqttBrokerPortParam);
  mqttGroup.addItem(&mqttCommandTopicBaseParam);
  mqttGroup.addItem(&mqttStateTopicBaseParam);
  
  iotWebConf.addParameterGroup(&mqttGroup);
  iotWebConf.addParameterGroup(&animationGroup);
  
  iotWebConf.setConfigSavedCallback(&configSaved);

  // -- Initializing the configuration.
  iotWebConf.init();

  Debug.begin(thingName);
  Debug.setSerialEnabled(true);
  Debug.showProfiler(false); // Profiler (Good to measure times, to optimize codes)
  Debug.showColors(true);    // Colors

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

  ArduinoOTA.onStart([]() {
    String type;
    if (ArduinoOTA.getCommand() == U_FLASH)
    {
      type = "sketch";
    }
    else
    { // U_FS
      type = "filesystem";
    }

    // NOTE: if updating FS this would be the place to unmount FS using FS.end()
    Serial.println("Start updating " + type);
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("\nEnd");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });

  ArduinoOTA.begin();

  mqttClient.setServer(mqtt_server, 1883);
  mqttClient.setCallback(onMqttReceiveMessage);

  Serial.println("Startup completed!");
}

uint32_t mLastTime = 0;
void loop()
{
  if ((millis() - mLastTime) >= 1000)
  {
    // Time
    mLastTime = millis();
    debugV("* Ploink!");
  }

  if (animations.IsAnimating())
  {
    animations.UpdateAnimations();
    strip.Show();
  }
  else
  {
    //StartStepUpAnimation();
  }

  if (iotWebConf.getState() == IOTWEBCONF_STATE_ONLINE && !mqttClient.connected())
  {
    reconnectMqtt();
  }

  yield();
  iotWebConf.doLoop();
  yield();
  Debug.handle();
  yield();
  ArduinoOTA.handle();
  yield();
  mqttClient.loop();
  yield();

  if (needsReset)
  {
    ESP.reset();
  }
}
