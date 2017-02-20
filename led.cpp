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
#include "led.h"

#define PIN_LED (2)

void StartLED()
{
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_LED, OUTPUT);
}

static const uint32_t ledBlinkValue[] = {
  0x00000000, // LED_OFF
  0xffffffff, // LED_ON
  0xffff0000, // LED_CONNECTING (slow blink)
  0xaaaaaaaa, // LED_AWAITSETUP (annoying flash)
  0x00010001  // LED_CONNECTED  (occasional blip)
};

void ManageLED(ledBlink type)
{
  byte whichBit = (millis()/100) % 32;
  digitalWrite(PIN_LED, ledBlinkValue[type] & (1<<whichBit) ? LOW : HIGH);
}

