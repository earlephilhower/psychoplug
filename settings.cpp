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
#include <EEPROM.h>
#include "settings.h"
#include "password.h"
#include "log.h"

static byte CalcSettingsChecksum(Settings *settings);

void StartSettings()
{
  EEPROM.begin(sizeof(Settings)+2);
}

void StopSettings()
{
  EEPROM.commit();
  EEPROM.end();
}

static void CopyFromEEPROM(void *dest, size_t offset, size_t len)
{
 EEPROM.begin(sizeof(Settings)+2);
 byte *p = (byte *)dest;
  for (size_t i=0; i<len; i++) {
    byte b = EEPROM.read(offset + i);
    *(p++) = b;
  }
  EEPROM.end();
}

void LoadSettingsTime(SettingsTime *time)
{
  CopyFromEEPROM(time, offsetof(Settings, time), sizeof(SettingsTime));
}

void LoadSettingsMQTT(SettingsMQTT *mqtt)
{
  CopyFromEEPROM(mqtt, offsetof(Settings, mqtt), sizeof(SettingsMQTT));
}

void LoadSettingsWiFi(SettingsWiFi *wifi)
{
  CopyFromEEPROM(wifi, offsetof(Settings, wifi), sizeof(SettingsWiFi));
}

void LoadSettingsEvents(SettingsEvents *events)
{
  CopyFromEEPROM(events, offsetof(Settings, events), sizeof(SettingsEvents));
}

void LoadSettingsUI(SettingsUI *ui)
{
  CopyFromEEPROM(ui, offsetof(Settings, ui), sizeof(SettingsUI));
}


bool LoadAllSettings(bool reset, Settings *settings)
{
  bool ok = false;

  StartSettings();
  
  // Try and read from "EEPROM", if that fails use defaults
  byte *p = (byte *)settings;
  for (unsigned int i=0; i<sizeof(Settings); i++) {
    byte b = EEPROM.read(i);
    *(p++) = b;
  }
  byte chk = EEPROM.read(sizeof(Settings));
  byte notChk = EEPROM.read(sizeof(Settings)+1);

  byte calcChk = CalcSettingsChecksum(settings);
  byte notCalcChk = ~calcChk;

  if ((chk != calcChk) || (notChk != notCalcChk) ||(settings->version != SETTINGSVERSION) || (reset)) {
    LogPrintf("Setting checksum mismatch, generating default settings\n");
    memset(settings, 0, sizeof(Settings));
    settings->version = SETTINGSVERSION;
    settings->wifi.ssid[0] = 0;
    settings->wifi.psk[0] = 0;
    strcpy_P(settings->wifi.hostname, PSTR("psychoplug"));
    settings->wifi.useDHCP = true;
    memset(settings->wifi.ip, 0, 4);
    memset(settings->wifi.dns, 0, 4);
    memset(settings->wifi.gateway, 0, 4);
    memset(settings->wifi.netmask, 0, 4);
    memset(settings->wifi.logsvr, 0, 4);
    strcpy_P(settings->time.ntp, PSTR("us.pool.ntp.org"));
    strcpy_P(settings->ui.user, PSTR("admin"));
    strcpy_P(settings->time.timezone, PSTR("America/Los_Angeles"));
    settings->time.use12hr = true;
    settings->time.usedmy = false;
    HashPassword(settings->ui.user, settings->ui.salt, settings->ui.passEnc);
    memset(settings->wifi.logsvr, 0, 4); 
    settings->events.onAfterPFail = false;
//    settings.voltage = 120;
    settings->mqtt.enable = false;
    ok = false;
    LogPrintf("Unable to restore settings from EEPROM\n");
  } else {
    LogPrintf("Settings restored from EEPROM\n");
    ok = true;
  }

  StopSettings();

  return ok;
}

void SaveSettings(Settings *settings)
{
  LogPrintf("Saving Settings\n");

  StartSettings();
  
  byte *p = (byte *)settings;
  for (unsigned int i=0; i<sizeof(Settings); i++) EEPROM.write(i, *(p++));
  byte ck = CalcSettingsChecksum(settings);
  EEPROM.write(sizeof(Settings), ck);
  EEPROM.write(sizeof(Settings)+1, ~ck);
  EEPROM.commit();

  StopSettings();
}

static byte CalcSettingsChecksum(Settings *settings)
{
  byte *p = (byte*)settings;
  byte c = 0xef;
  for (unsigned int j=0; j<sizeof(Settings); j++) c ^= *(p++);
  return c;
}

