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

#ifndef _settings_h
#define _settings_h

#include <Arduino.h>

#include "schedule.h"


const byte SETTINGSVERSION = 1;
typedef struct {
  byte version;
  char ssid[32];
  char psk[64];
  char hostname[32];
  bool useDHCP;
  byte ip[4];
  byte dns[4];
  byte gateway[4];
  byte netmask[4];
  byte logsvr[4];
  char ntp[64];
  int utc;
  
  bool onAfterPFail;
  byte voltage;
  
  // MQTT Configuration
  bool mqttEnable;
  char mqttHost[64];
  int mqttPort;
  bool mqttSSL;
  char mqttClientID[32];
  char mqttTopic[64]; 
  char mqttUser[32];
  char mqttPass[64];

  // Web Interface
  bool uiEnable;
  char uiUser[32];
  char uiPassEnc[20];
  char uiSalt[32];

  // Events to process
  Event event[MAXEVENTS];
} Settings;

extern Settings settings;


bool LoadSettings(bool reset);
void SaveSettings();


#endif

