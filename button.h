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

#ifndef _button_h
#define _button_h

#include <Arduino.h>

// Button event types
const byte BUTTON_NONE = 0;
const byte BUTTON_PRESS = 1;
const byte BUTTON_RELEASE = 2;


void StartButton();

// Check button state and act appropriately
void ManageButton();

// Used during setup to see raw state of button, not to be used elsewhere
bool RawButton();

#endif

