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
  if (!isSetup || !settings.logsvr[0]) {
    Serial.print(str);
    Serial.flush();
  } else {
    udpLog.beginPacket(settings.logsvr, 9911);
    udpLog.write(str, strlen(str));
    udpLog.endPacket();  
  }
}


