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

#include "psychoplug.h"
#include "button.h"
#include "settings.h"
#include "mqtt.h"
#include "relay.h"
#include "log.h"

static byte DebounceButton();

#define PIN_BUTTON (13)


void StartButton()
{
  pinMode(PIN_BUTTON, INPUT_PULLUP);
}

// Hendle all power button management (debounce, mqtt, etc.)
void ManageButton()
{
  // Let the button toggle the relay always
  byte action = DebounceButton();
  
  if (action==BUTTON_PRESS) {
    SetRelay(!GetRelay());
  }
  if (action != BUTTON_NONE) {
    MQTTPublish("button", action==BUTTON_PRESS?"press":"release");
  }
}

bool RawButton()
{
  return !digitalRead(PIN_BUTTON);
}

// Check if button has been pressed (after debounce) and return one event for press and one for release
static byte DebounceButton()
{
  static bool ignoring = false;
  static unsigned long checkTime = 0;
  static bool prevButton = false;
  static bool debounceButton = false;
  byte event = BUTTON_NONE;
  
  if (!ignoring) {
    bool curButton = !digitalRead(PIN_BUTTON);
    if (curButton != prevButton) {
      ignoring = true;
      checkTime = micros() + 5000;
    }
    prevButton = curButton;
  } else {
    bool curButton = !digitalRead(PIN_BUTTON);
    if (curButton != prevButton) {
      // Noise, reset the clock
      ignoring = false;
    } else if (micros() > checkTime) {
      if (curButton != debounceButton) {
        event = (debounceButton)?BUTTON_RELEASE:BUTTON_PRESS;
        debounceButton = curButton;
        LogPrintf("Button Event: %d\n", event);
      }
      ignoring = false;
    }
  }
  return event;
}

