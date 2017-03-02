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

#ifndef _schedule_h
#define _schedule_h

#include <Arduino.h>

// Maximum # of events to operate upon
#define MAXEVENTS (24) 

typedef struct {
  byte dayMask; // binary flags per-day
  byte hour;
  byte minute;
  byte action;
} Event;

#define ACTION_NONE     (0)
#define ACTION_ON       (1)
#define ACTION_OFF      (2)
#define ACTION_TOGGLE   (3)
#define ACTION_PULSEOFF (4)
#define ACTION_PULSEON  (5)
#define ACTION_MAX      (5)
extern const char *actionString[];
extern void PerformAction(int action);


// Handle scheduled operations
void ManageSchedule();
void StopSchedule();

#endif

