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
#include <ESP8266WiFi.h>
#include <WiFiUdp.h>

#include "psychoplug.h"
#include "log.h"
#include "settings.h"

static WiFiUDP udpLog;


void StartLog()
{
  udpLog.begin(8976);
}

void StopLog()
{
  udpLog.stop();
}

void Log(const char *str)
{
/*  if (!isSetup) return;
  if (!settings.logsvr[0]) return;
  udpLog.beginPacket(settings.logsvr, 9911);
  udpLog.write(str, strlen(str));
  udpLog.endPacket();  
  */
  Serial.print(str);
  Serial.flush();
}

void LogSettings()
{
  LogPrintf("-------------------------\n");
  LogPrintf("settings.version = %d\n", settings.version);
  LogPrintf("settings.ssid = '%s'\n", settings.ssid);
  LogPrintf("settings.psk = '%s'\n", settings.psk);
  LogPrintf("settings.hostname = '%s'\n", settings.hostname);
  LogPrintf("settings.useDHCP = %d\n", settings.useDHCP);
  LogPrintf("settings.ip = %d.%d.%d.%d\n", settings.ip[0], settings.ip[1], settings.ip[2], settings.ip[3]);
  LogPrintf("settings.dns = %d.%d.%d.%d\n", settings.dns[0], settings.dns[1], settings.dns[2], settings.dns[3]);
  LogPrintf("settings.gateway = %d.%d.%d.%d\n", settings.gateway[0], settings.gateway[1], settings.gateway[2], settings.gateway[3]);
  LogPrintf("settings.netmask = %d.%d.%d.%d\n", settings.netmask[0], settings.netmask[1], settings.netmask[2], settings.netmask[3]);
  LogPrintf("settings.logsvr = %d.%d.%d.%d\n", settings.logsvr[0], settings.logsvr[1], settings.logsvr[2], settings.logsvr[3]);

  LogPrintf("settings.ntp = '%s'\n", settings.ntp);
  LogPrintf("settings.utc = %d\n", settings.utc);

  LogPrintf("settings.onAfterPFail = %d\n", settings.onAfterPFail);
  LogPrintf("settings.voltage = %d\n", settings.voltage);
  
  LogPrintf("settings.mqttEnable = %d\n", settings.mqttEnable);
  LogPrintf("settings.mqttHost = '%s'\n", settings.mqttHost);
  LogPrintf("settings.mqttPort = %d\n", settings.mqttPort);
  LogPrintf("settings.mqttSSL = %d\n", settings.mqttSSL);
  LogPrintf("settings.mqttClientID = '%s'\n", settings.mqttClientID);
  LogPrintf("settings.mqttTopic = '%s'\n", settings.mqttTopic);
  LogPrintf("settings.mqttUser = '%s'\n", settings.mqttUser);
  LogPrintf("settings.mqttPass = '%s'\n", settings.mqttPass);

  // Web Interface
  LogPrintf("settings.uiEnable = %d\n", settings.uiEnable);
  LogPrintf("settings.uiUser = '%s'\n", settings.uiUser);
  LogPrintf("-------------------------\n");
}



