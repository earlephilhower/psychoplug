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

#ifndef _settings_h
#define _settings_h

#include <Arduino.h>
#include "password.h"
#include "schedule.h"

#define SETTINGSVERSION (3)


typedef struct {
  char ssid[32];
  char psk[32];  
  char hostname[16];
  bool useDHCP;
  byte ip[4];
  byte dns[4];
  byte gateway[4];
  byte netmask[4];
  byte logsvr[4];
} SettingsWiFi;

typedef struct {
  char ntp[48];
  bool use12hr;
  bool usedmy;
  char timezone[20];
} SettingsTime;

typedef struct {
  bool enable;
  char host[48];
  int16_t port;
  bool SSL;
  char clientID[32];
  char topic[16]; 
  char user[32];
  char pass[32];
} SettingsMQTT;

typedef struct {
  // Web Interface
  char user[32];
  char passEnc[PASSENCLEN];
  char salt[SALTLEN];
} SettingsUI;

typedef struct {
  bool onAfterPFail;
  
  // Events to process
  Event event[MAXEVENTS];
} SettingsEvents;


// The EEPROM formatted version
typedef struct {
  byte version;
  SettingsWiFi wifi;
  SettingsTime time;
  SettingsMQTT mqtt;
  SettingsUI   ui;
  SettingsEvents events;
} Settings;

//extern Settings settings;

void StartSettings();

void LoadSettingsWiFi(SettingsWiFi *wifi);
void LoadSettingsTime(SettingsTime *time);
void LoadSettingsMQTT(SettingsMQTT *mqtt);
void LoadSettingsUI(SettingsUI *ui);
void LoadSettingsEvents(SettingsEvents *events);
bool LoadAllSettings(bool reset, Settings *settings);

void SaveSettings(Settings *settings);

void StopSettings();

#endif

