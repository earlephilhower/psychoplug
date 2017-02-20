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

#ifndef _led_h
#define _led_h

void StartLED();


typedef enum { LED_OFF, LED_ON, LED_CONNECTING, LED_AWAITSETUP, LED_CONNECTED } ledBlink;

void ManageLED(ledBlink type);


#endif
