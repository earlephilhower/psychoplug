/*
  PsychoPlug
  ESP8266 based remote outlet with standalone timer and MQTT integration
  
  Copyright (C) 2017  Earle F. Philhower, III

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation, either version 3 of the License, or
  (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include <Arduino.h>
#include <ESP8266WiFi.h>
#include <MQTTClient.h>

#include "psychoplug.h"
#include "log.h"
#include "mqtt.h"
#include "settings.h"
#include "relay.h"

// MQTT interface
static WiFiClient wifiMQTT;
static WiFiClientSecure wifiMQTTSSL;
static MQTTClient mqttClient;

// Callback for the MQTT library
void messageReceived(String topic, String payload, char *bytes, unsigned int length)
{
  char t[64], p[32];

  (void)bytes;
  (void)length;
  
  topic.toCharArray(t, sizeof(t));
  payload.toCharArray(p, sizeof(p));
  LogPrintf("MQTT: '%s'='%s'\n", t, p); 

  char topicStr[64];
  snprintf(topicStr, sizeof(topicStr), "%s/remotepower", settings.mqttTopic);
  if (!strcmp(topicStr, t)) {
    if (!strcasecmp("on", p) || !strcmp("1",p))
      SetRelay(true);
    else if (!strcasecmp("off", p) || !strcmp("0",p))
      SetRelay(false);
    else if (!strcasecmp("toggle", p) )
      SetRelay(!GetRelay());
  }
}


void StartMQTT()
{
  LogPrintf("Starting MQTT\n");
  LogPrintf("Free heap = %d\nConnecting MQTT...\n", ESP.getFreeHeap());
  if (settings.mqttEnable) {
    mqttClient.begin(settings.mqttHost, settings.mqttPort, settings.mqttSSL ? wifiMQTTSSL : wifiMQTT);
    mqttClient.connect(settings.mqttClientID, settings.mqttUser, settings.mqttPass);
    if (mqttClient.connected() ) {
      char topic[64];
      snprintf(topic, sizeof(topic), "%s/remotepower", settings.mqttTopic);
      mqttClient.subscribe(topic);
    }
  }
  LogPrintf("Free heap = %d after connection\n", ESP.getFreeHeap());
}

void ManageMQTT()
{
  // Only have MQTT loop if we're connected and configured
  if (settings.mqttEnable && mqttClient.connected()) {
    mqttClient.loop();
    delay(10);
  } else if (settings.mqttEnable && !mqttClient.connected()){
    LogPrintf("MQTT disconnected, reconnecting\n");
    mqttClient.connect(settings.mqttClientID, settings.mqttUser, settings.mqttPass);
    if (mqttClient.connected() ) {
      char topic[64];
      snprintf(topic, sizeof(topic), "%s/remotepower", settings.mqttTopic);
      mqttClient.subscribe(topic);
    }
    delay(10);
  }
}

void StopMQTT()
{
  
}

void MQTTPublish(const char *key, const char *value)
{
  if (isSetup && settings.mqttEnable && mqttClient.connected()) {
    char topic[64];
    snprintf(topic, sizeof(topic), "%s/%s", settings.mqttTopic, key);
    mqttClient.publish(topic, value);
  }
}

void MQTTPublishInt(const char *key, const int value)
{
  char str[16];
  snprintf(str, sizeof(str), "%d", value);
  MQTTPublish(key, str);
}


