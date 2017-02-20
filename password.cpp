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
#include <Hash.h>  //sha1 exported here
#include "password.h"
#include "settings.h"


// Set the settings.uiPassEnc to the raw password and callthis to make a new salt and run encryption against it
// Output overwrites the uiPassEnc variable
void HashPassword(const char *pass)
{
  memset(settings.uiSalt, 0, sizeof(settings.uiSalt)); // Clear salt to start
  memset(settings.uiPassEnc, 0, sizeof(settings.uiPassEnc)); // Clear salt to start
  if (pass[0]==0) return; // No password
  for (unsigned int i=0; i<sizeof(settings.uiSalt); i++)
    settings.uiSalt[i] = RANDOM_REG32 & 0xff;

  // Now catenate the hash and raw password to temp storage
  char raw[128];
  memset(raw, 0, 128);
  memcpy(raw, settings.uiSalt, sizeof(settings.uiSalt));
  strncpy(raw+sizeof(settings.uiSalt), pass, 64);
  int len = strnlen(pass, 63)+1;
  sha1((uint8_t*)raw, sizeof(settings.uiSalt)+len, (uint8_t*)settings.uiPassEnc);
  memset(raw, 0, 128); // Get rid of plaintext temp copy 
}

bool VerifyPassword(char *pass)
{
  // Now catenate the hash and raw password to temp storage
  char raw[128];
  memset(raw, 0, 128);
  memcpy(raw, settings.uiSalt, sizeof(settings.uiSalt));
  strncpy(raw+sizeof(settings.uiSalt), pass, 64);
  int len = strnlen(pass, 63)+1;
  while (*pass) { *(pass++) = 0; } // clear out sent-in 
  char *dest[20];
  sha1((uint8_t*)raw, sizeof(settings.uiSalt)+len, (uint8_t*)dest);
  memset(raw, 0, 128);
  if (memcmp(settings.uiPassEnc, dest, 20)) return false;
  return true;
}

