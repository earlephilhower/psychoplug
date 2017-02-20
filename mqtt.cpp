/*
  Psychoplug
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
#include "psychoplug.h"
#include "log.h"
#include "mqtt.h"
#include "settings.h"

// MQTT interface
WiFiClient wifiMQTT;
WiFiClientSecure wifiMQTTSSL;
MQTTClient mqttClient;

// Callback for the MQTT library
// TODO FIXME - Add actual hooks to set/clear power state
void messageReceived(String topic, String payload, char *bytes, unsigned int length)
{
  char t[128], p[128];
  topic.toCharArray(t, 128);
  payload.toCharArray(p, 128);
  LogPrintf("MQTT: '%s'='%s'\n", t, p); 
}


void StartMQTT()
{
  LogPrintf("Starting MQTT\n");
  if (settings.mqttEnable) {
    mqttClient.begin(settings.mqttHost, settings.mqttPort, settings.mqttSSL ? wifiMQTTSSL : wifiMQTT);
    mqttClient.connect(settings.mqttClientID, settings.mqttUser, settings.mqttPass);
    if (mqttClient.connected() ) mqttClient.subscribe(settings.mqttTopic);
  }
}

void ManageMQTT()
{
  // Only have MQTT loop if we're connected and configured
  if (settings.mqttEnable && mqttClient.connected()) {
    mqttClient.loop();
    delay(10);
  }
}

void StopMQTT()
{
  
}

void MQTTPublish(const char *key, const char *value)
{
//  LogPrintf("MQTTPublish: '%s'='%s'\n", key, value);
  if (isSetup && settings.mqttEnable && mqttClient.connected()) {
    char topic[128];
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


