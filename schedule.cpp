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
#include <TimeLib.h>
#include "psychoplug.h"
#include "schedule.h"
#include "settings.h"
#include "mqtt.h"
#include "relay.h"
#include "timezone.h"

const char *actionString[] = { "None", "On", "Off", "Toggle", "Pulse Off", "Pulse On" };


// Handle automated on/off simply on the assumption we don't lose any minutes
static char lastHour = -1;
static char lastMin = -1;
static char lastDOW = -1;
void ManageSchedule()
{ 
  // Can't run schedule if we don't know what the time is!
  if (timeStatus() == timeNotSet) return;

  // Sane startup time values
  if (lastHour == -1) {
    time_t t = LocalTime(now());
    lastHour = hour(t);
    lastMin = minute(t);
    lastDOW = weekday(t) - 1; // We 0-index, weekday() 1-indexes
  }

  // If this is a new h/m/dow then go through all events and see if we need to do an action.
  // Only execute after we scan everything, so only last action done once even if there are
  // multiple entries for the same time or multiple times scanned.

  time_t t = LocalTime(now());
  int newHour = hour(t);
  int newMin = minute(t);
  int newDOW = weekday(t)-1;
  if (newHour != lastHour || newMin != lastMin || newDOW != lastDOW) {
    int action = ACTION_NONE;
    // This *should* be fast unless we lose a day somewhere (NTP problems?)
    while (newHour != lastHour || newMin != lastMin || newDOW != lastDOW) {
      lastMin++;
      if (lastMin==60) { lastHour++; lastMin = 0; }
      if (lastHour==24) { delay(1); /* allow ctx switch */ lastDOW++; lastHour = 0; }
      if (lastDOW==7) lastDOW = 0; // Sat->Sun
      for (int i=0; i<MAXEVENTS; i++) {
        if ((settings.event[i].dayMask & (1<<lastDOW)) && (settings.event[i].hour == lastHour) && (settings.event[i].minute == lastMin) && (settings.event[i].action != ACTION_NONE)) {
          action = settings.event[i].action;
        }
      }
    }

    if (action != ACTION_NONE) {
      MQTTPublish("scheduledevent", actionString[action]);
    }

    switch (action) {
      case ACTION_NONE: break;
      case ACTION_ON: SetRelay(true); break;
      case ACTION_OFF: SetRelay(false); break;
      case ACTION_TOGGLE: SetRelay(!GetRelay()); break;
      case ACTION_PULSEOFF: SetRelay(false); delay(500); SetRelay(true); break;
      case ACTION_PULSEON: SetRelay(true); delay(500); SetRelay(false); break;
    }

    lastHour = hour(t);
    lastMin = minute(t);
    lastDOW = weekday(t)-1; // Sunday=1 for weekday(), we need 0-index
  }
}

void StopSchedule()
{
  // Cause new time to be retrieved.
  lastHour = -1;
  lastMin = -1;
  lastDOW = -1;
}

