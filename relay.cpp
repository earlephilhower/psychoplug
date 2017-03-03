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
#include "relay.h"
#include "mqtt.h"
#include "log.h"

#define PIN_RELAY (15)


// Initializes relay control pins (relay state undefined)
void StartRelay(bool state)
{
  SetRelay(state);
  pinMode(PIN_RELAY, OUTPUT);
}

// Sets the relay on or off and handles any logging required
void SetRelay(bool on)
{
  digitalWrite(PIN_RELAY, on ? HIGH:LOW );
  MQTTPublishInt("powerstate", on ? 1 : 0);
}

// Returns relay state
bool GetRelay()
{
  return digitalRead(PIN_RELAY)==LOW?false:true;
}

