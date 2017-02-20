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

static byte CalcSettingsChecksum();


bool LoadSettings(bool reset)
{
  bool ok = false;
  
  EEPROM.begin(4096);
  // Try and read from "EEPROM", if that fails use defaults
  byte *p = (byte *)&settings;
  for (unsigned int i=0; i<sizeof(settings); i++) {
    byte b = EEPROM.read(i);
    *(p++) = b;
  }
  byte chk = EEPROM.read(sizeof(settings));
  byte notChk = EEPROM.read(sizeof(settings)+1);

  byte calcChk = CalcSettingsChecksum();
  byte notCalcChk = ~calcChk;

  if ((chk != calcChk) || (notChk != notCalcChk) ||(settings.version != SETTINGSVERSION) || (reset)) {
    Log("Setting checksum mismatch, generating default settings");
    memset(&settings, 0, sizeof(settings));
    settings.version = SETTINGSVERSION;
    settings.ssid[0] = 0;
    settings.psk[0] = 0;
    strcpy(settings.hostname, "wifiplug");
    settings.useDHCP = true;
    memset(settings.ip, 0, 4);
    memset(settings.dns, 0, 4);
    memset(settings.gateway, 0, 4);
    memset(settings.netmask, 0, 4);
    memset(settings.logsvr, 0, 4);
    strcpy(settings.ntp, "us.pool.ntp.org");
    strcpy(settings.uiUser, "admin");
    HashPassword("admin"); // This will set settings.uiPassEnc
    memset(settings.logsvr, 0, 4); 
    settings.utc =-8;
    settings.onAfterPFail = false;
    settings.voltage = 120;
    settings.mqttEnable = false;
    ok = false;
    Log("Unable to restore settings from EEPROM\n");
  } else {
    Log("Settings restored from EEPROM\n");
    ok = true;
  }
  EEPROM.end();
  return ok;
}

void SaveSettings()
{
  EEPROM.begin(4096);
  byte *p = (byte *)&settings;
  for (unsigned int i=0; i<sizeof(settings); i++) EEPROM.write(i, *(p++));
  byte ck = CalcSettingsChecksum();
  EEPROM.write(sizeof(settings), ck);
  EEPROM.write(sizeof(settings)+1, ~ck);
  EEPROM.end();
}



static byte CalcSettingsChecksum()
{
  byte *p = (byte*)&settings;
  byte c = 0xef;
  for (unsigned int j=0; j<sizeof(settings); j++) c ^= *(p++);
  return c;
}

