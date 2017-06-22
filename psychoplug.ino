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

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPUpdateServer.h>

#include "psychoplug.h"
#include "settings.h"
#include "schedule.h"
#include "mqtt.h"
#include "ntp.h"
#include "button.h"
#include "log.h"
#include "power.h"
#include "password.h"
#include "relay.h"
#include "led.h"
#include "timezone.h"
#include "dns.h"
#include "web.h"

bool isSetup = false;


// Our setup IP
static IPAddress setupIP = {192, 168, 4, 1};
static IPAddress setupMask = {255, 255, 255, 0};


// All it does is redirect to https://<given url>
static WiFiServer redirector(80);

// OTA updates on the cheap...
static ESP8266WebServer *otaServer = NULL;
static ESP8266HTTPUpdateServer *otaUpdateServer = NULL;


// HTTPS interface
static const uint8_t rsakey[] ICACHE_RODATA_ATTR = {
#include "key.h"
};

static const uint8_t x509[] ICACHE_RODATA_ATTR = {
#include "x509.h"
};

// Create an instance of the server
// specify the port to listen on as an argument
static WiFiServerSecure https(443);

// Return a *static* char * to an IP formatted string, so DO NOT USE MORE THAN ONCE PER LINE
const char *FormatIP(const byte ip[4], char *buff, int buffLen)
{
  snprintf_P(buff, buffLen, PSTR("%d.%d.%d.%d"), ip[0], ip[1], ip[2], ip[3]);
  return buff;
}

const char *FormatBool(bool b, char *dest, int len)
{
  if (b) strncpy_P(dest, PSTR("True"), len);
  else strncpy_P(dest, PSTR("False"), len);

  return dest;
}

void PrintSettings(WiFiClient *client, Settings *settings)
{
  char buff[16];
 
  WebPrintf(client, "<br><h1>WiFi Network</h1>\n");
  WebPrintf(client, "SSID: %s<br>\n", settings->wifi.ssid);
  WebPrintf(client, "PSK: %s<br>\n", settings->wifi.psk);
  WebPrintf(client, "Hostname: %s<br>\n", settings->wifi.hostname);
  WebPrintf(client, "DHCP: %s<br>\n", FormatBool(settings->wifi.useDHCP, buff, sizeof(buff)));
  if (!settings->wifi.useDHCP) {
    WebPrintf(client, "IP: %s<br>\n", FormatIP(settings->wifi.ip, buff, sizeof(buff)));
    WebPrintf(client, "Gateway: %s<br>\n", FormatIP(settings->wifi.gateway, buff, sizeof(buff)));
    WebPrintf(client, "Netmask: %s<br>\n", FormatIP(settings->wifi.netmask, buff, sizeof(buff)));
    WebPrintf(client, "DNS: %s<br>\n", FormatIP(settings->wifi.dns, buff, sizeof(buff)));
  }
  WebPrintf(client, "UDP Log Server: %s:9911 (nc -l -u 9911)<br>\n", FormatIP(settings->wifi.logsvr, buff, sizeof(buff)));
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebPrintf(client, "NTP: %s<br>\n", settings->time.ntp);
  WebPrintf(client, "Timezone: %s<br>\n", settings->time.timezone);
  WebPrintf(client, "12 hour time format: %s<br>\n", FormatBool(settings->time.use12hr, buff, sizeof(buff)));
  WebPrintf(client, "DD/MM/YY date format: %s<br>\n", FormatBool(settings->time.usedmy, buff, sizeof(buff)));
  
  WebPrintf(client, "<br><h1>Power Settings</h1>\n");
//  WebPrintf(client, "System Voltage: %d<br>\n", settings.voltage);
  WebPrintf(client, "On after power failure: %s<br>\n", FormatBool(settings->events.onAfterPFail, buff, sizeof(buff)));

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  WebPrintf(client, "Enabled: %s<br>\n", FormatBool(settings->mqtt.enable, buff, sizeof(buff)));
  WebPrintf(client, "Host: %s<br>\n", settings->mqtt.host);
  WebPrintf(client, "Port: %d<br>\n", settings->mqtt.port);
  WebPrintf(client, "Use SSL: %s<br>\n", FormatBool(settings->mqtt.SSL, buff, sizeof(buff)));
  WebPrintf(client, "ClientID: %s<br>\n", settings->mqtt.clientID);
  WebPrintf(client, "Topic: %s<br>\n", settings->mqtt.topic);
  WebPrintf(client, "User: %s<br>\n", settings->mqtt.user);
  WebPrintf(client, "Pass: %s<br>\n", settings->mqtt.pass);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  WebPrintf(client, "Admin User: %s<br>\n", settings->ui.user);
  WebPrintf(client, "Admin Pass: *HIDDEN*<br>\n");
}



void MakeSSID(char *ssid, int len)
{
  byte mac[6];
  WiFi.macAddress(mac);
  snprintf_P(ssid, len, PSTR("PSYCHOPLUG-%02X%02X%02X"), mac[3], mac[4], mac[5]);
}


void StartSetupAP()
{
  char ssid[32];

  LogPrintf("Starting Setup AP Mode\n");
  WiFi.mode(WIFI_AP);

  isSetup = false;
  MakeSSID(ssid, sizeof(ssid));
  WiFi.softAPConfig(setupIP, setupIP, setupMask);
  WiFi.softAP(ssid);

//  StartDNS(&setupIP);
  
  LogPrintf("Waiting for connection\n");
  https.begin();
  https.setNoDelay(true);
  redirector.begin();
  redirector.setNoDelay(true);
}

void StartSTA(Settings *settings)
{
  LogPrintf("Starting STA Mode\n");
  WiFi.mode(WIFI_STA);
  
  if (settings->wifi.hostname[0])
    WiFi.hostname(settings->wifi.hostname);
  
  if (!settings->wifi.useDHCP) {
    WiFi.config(settings->wifi.ip, settings->wifi.gateway, settings->wifi.netmask, settings->wifi.dns);
  } else {
    byte zero[] = {0,0,0,0};
    WiFi.config(zero, zero, zero, zero);
  }
  if (settings->wifi.psk[0]) WiFi.begin(settings->wifi.ssid, settings->wifi.psk);
  else WiFi.begin(settings->wifi.ssid);

  // Try forever
  while (WiFi.status() != WL_CONNECTED) {
    LogPrintf("Trying to connect to '%s' key '%s'\n", settings->wifi.ssid, settings->wifi.psk );
    for (int i=0; i<10; i++) {  // Wait one second, but keep button and LED live
      ManageLED(LED_CONNECTING);
      ManageButton();
      delay(100);
    }
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Reset(); // Punt, maybe we're in a weird way.  Reboot and try it again
  }

  IPAddress ip = WiFi.localIP();
  LogPrintf("IP:%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

  StartMQTT(&settings->mqtt);
    
  // Start standard interface
  https.begin();
  https.setNoDelay(true);
  redirector.begin();
  redirector.setNoDelay(true);
  StartNTP(&settings->time);
  StartLog(&settings->wifi);
  
  SetTZ(settings->time.timezone);
  char atime[64];
  LogPrintf("Local time is now: %s\n", AscTime(now(), settings->time.use12hr, settings->time.usedmy, atime, sizeof(atime)));
}


// Setup web page
void SendSetupHTML(WiFiClient *client)
{
  char buff[16];
  Settings settings;
  LoadAllSettings(false, &settings);

  LogPrintf("+SendSetupHTML\n");
  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>PsychoPlug Setup</title>" ENCODING "</head>\n");
  WebPrintf(client, "<body><h1>PsychoPlug Setup</h1>\n");
  WebPrintf(client, "<form action=\"config.html\" method=\"POST\">\n");

  WebPrintf(client, "<br><h1>WiFi Network</h1>\n");
  WebFormText(client, PSTR("SSID"), "ssid", settings.wifi.ssid, true);
  int cnt = WiFi.scanNetworks();
  if (cnt==0) {
    WebPrintf(client, "Discovered networks: No WIFI networks detected.<br>\n");
  } else {
    WebPrintf(client, "Discovered networks: <select onchange=\"setval(this)\">");
    WebPrintf(client, "<option></option>");
    for (byte i=0; i<cnt; i++) {
      WebPrintf(client, "<option>%s</option>", WiFi.SSID(i).c_str());
    }
    WebPrintf(client, "</select><br>\n");
    WebPrintf(client, "<script language=\"javascript\">function setval(i) { document.getElementById(\"ssid\").value = i.options[i.selectedIndex].text;}</script>\n");
  }
  WebFormText(client, PSTR("Password"), "psk", settings.wifi.psk, true);
  WebFormText(client, PSTR("Hostname"), "hn", settings.wifi.hostname, true);
  const char *ary1[] = {"ip", "nm", "gw", "dns", ""};
  WebFormCheckboxDisabler(client, PSTR("DHCP Networking"), "dh", false, settings.wifi.useDHCP, true, ary1 );
  WebFormText(client, PSTR("IP"), "ip", FormatIP(settings.wifi.ip, buff, sizeof(buff)), !settings.wifi.useDHCP);
  WebFormText(client, PSTR("Netmask"), "nm", FormatIP(settings.wifi.netmask, buff, sizeof(buff)), !settings.wifi.useDHCP);
  WebFormText(client, PSTR("Gateway"), "gw", FormatIP(settings.wifi.gateway, buff, sizeof(buff)), !settings.wifi.useDHCP);
  WebFormText(client, PSTR("DNS"), "dns", FormatIP(settings.wifi.dns, buff, sizeof(buff)), !settings.wifi.useDHCP);
  WebFormText(client, PSTR("UDP Log Server"), "ls", FormatIP(settings.wifi.logsvr, buff, sizeof(buff)), true);
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebFormText(client, PSTR("NTP Server"), "ntp", settings.time.ntp, true);
  WebTimezonePicker(client, settings.time.timezone);
  WebFormCheckbox(client, PSTR("12hr Time Format"), "u12h", settings.time.use12hr, true);
  WebFormCheckbox(client, PSTR("DD/MM/YY Date Format"), "udmy", settings.time.usedmy, true);

  WebPrintf(client, "<br><h1>Power</h1>\n");
  //WebFormText(client, PSTR("Mains Voltage"), "voltage", settings.voltage, true);
  WebFormCheckbox(client, PSTR("Start powered up after power loss"), "pf", settings.events.onAfterPFail, true);

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  const char *ary2[] = { "mh", "mp", "ms", "mu", "mp", "mt", "mc", "" };
  WebFormCheckboxDisabler(client, PSTR("Enable MQTT"), "mEn", true, settings.mqtt.enable, true, ary2 );
  WebFormText(client, PSTR("Host"), "mh", settings.mqtt.host, settings.mqtt.enable);
  WebFormText(client, PSTR("Port"), "mp", settings.mqtt.port, settings.mqtt.enable);
  WebFormCheckbox(client, PSTR("Use SSL"), "ms", settings.mqtt.SSL, settings.mqtt.enable);
  WebFormText(client, PSTR("User"), "mu", settings.mqtt.user, settings.mqtt.enable);
  WebFormText(client, PSTR("Pass"), "mp", settings.mqtt.pass, settings.mqtt.enable);
  WebFormText(client, PSTR("ClientID"), "mc", settings.mqtt.clientID, settings.mqtt.enable);
  WebFormText(client, PSTR("Topic"), "mt", settings.mqtt.topic, settings.mqtt.enable);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  WebFormText(client, PSTR("Admin User"), "uu", settings.ui.user, true);
  strncpy_P(buff, PSTR("*****"), sizeof(buff)); WebFormText(client, PSTR("Admin Password"), "up", buff, true);

  WebPrintf(client, "<input type=\"submit\" value=\"Submit\">\n");
  WebPrintf(client, "</form></body></html>\n");
  LogPrintf("-SendSetupHTML\n");
}


// Status Web Page
void SendStatusHTML(WiFiClient *client)
{
  bool curPower = GetRelay();
  char tmp[64];
  SettingsEvents events;
  SettingsTime time;
  SettingsWiFi wifi;
  LoadSettingsEvents(&events);
  LoadSettingsTime(&time);
  LoadSettingsWiFi(&wifi);

  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>%s Status</title>" ENCODING "</head>\n", wifi.hostname);
  WebPrintf(client, "<body>\n");
  WebPrintf(client, "Hostname: %s<br>\n", wifi.hostname);
  MakeSSID(tmp, sizeof(tmp));
  WebPrintf(client, "SSID: %s<br>\n", tmp);
  WebPrintf(client, "Current Time: %s<br>\n", AscTime(now(), time.use12hr, time.usedmy, tmp, sizeof(tmp)));
  unsigned long ms = millis();
  unsigned long days = ms / (24L * 60L * 60L * 1000L);
  ms -= days * (24L * 60L * 60L * 1000L);
  unsigned long hours = ms / (60L * 60L * 1000L);
  ms -= hours * (60L * 60L * 1000L);
  unsigned long mins = ms / (60L * 1000L);
  ms -= mins * (60L * 1000L);
  unsigned long secs = ms / (1000L);
  WebPrintf(client, "Uptime: %d days, %d hours, %d minutes, %d seconds<br>\n", days, hours, mins, secs);
  if (curPower) {
    WebPrintf(client, "Power: ON <a href=\"off.html\">Toggle</a><br><br>\n");
  } else {
    WebPrintf(client, "Power: OFF <a href=\"on.html\">Toggle</a><br><br>\n");
  }
//  WebPrintf(client, "Current: %dmA (%dW @ %dV)<br>\n", GetCurrentMA(), (GetCurrentMA()* settings.voltage) / 1000, settings.voltage);

  WebPrintf(client, "<table border=\"1px\">\n");
  WebPrintf(client, "<tr><th>#</th><th>Sun</th><th>Mon</th><th>Tue</th><th>Wed</th><th>Thu</th><th>Fri</th><th>Sat</th><th>Time</th><th>Action</th><th>EDIT</th></tr>\n");
  char *buff = (char *)alloca(500); // By construction this will fit a row of the table
  for (byte i=0; i<MAXEVENTS; i++) {
    int len = 0;
    sprintf_P(buff+len, PSTR("<tr><td>%d.</td>"), i+1);
    len += strlen(buff+len);
    for (byte j=0; j<7; j++) {
      sprintf_P(buff+len, PSTR("<td>")); // %s</td>"), (events.event[i].dayMask & (1<<j))?"[X]":"[&nbsp;]");
      len += strlen(buff+len);
      sprintf_P(buff+len, (events.event[i].dayMask & (1<<j)) ? PSTR("[X]") : PSTR("[&nbsp;]") ); 
      len += strlen(buff+len);
      sprintf_P(buff+len, PSTR("</td>")); 
      len += strlen(buff+len);
    }
    if (time.use12hr) {
      int prthr = events.event[i].hour%12;
      if (!prthr) prthr = 12;
      sprintf_P(buff+len, PSTR("<td>%d:%02d %s</td>"), prthr, events.event[i].minute, (events.event[i].hour<12)?"AM":"PM");
      len += strlen(buff+len);
    } else {
      sprintf_P(buff+len, PSTR("<td>%d:%02d</td>"), events.event[i].hour, events.event[i].minute);
      len += strlen(buff+len);
    }
    char str[16];
    sprintf_P(buff+len, PSTR("<td>%s</td>"), GetActionString(events.event[i].action, str, sizeof(str)));
    len += strlen(buff+len);
    sprintf_P(buff+len, PSTR("<td><a href=\"edit.html?id=%d\">Edit</a></td></tr>\r\n"), i);
    client->print(buff);
  }
  WebPrintf(client, "</table><br>\n");
  WebPrintf(client, "<a href=\"reconfig.html\">Change System Configuration</a><br><br>\n");

  WebPrintf(client, "CGI Action URLs: <a href=\"on.html\">On</a> <a href=\"off.html\">Off</a> <a href=\"toggle.html\">Toggle</a> <a href=\"pulseoff.html\">Pulse Off</a> ");
  WebPrintf(client, "<a href=\"pulseon.html\">Pulse On</a> <a href=\"status.html\">Status</a> <a href=\"hang.html\">Reset</a><br>\n");
  WebPrintf(client, "<br>\n<a href=\"enableupdate.html\">Enable HTTP update of firmware for 10 minutes</a>\n");
  WebPrintf(client, "</body>\n");
}

// Edit Rule
void SendEditHTML(WiFiClient *client, int id)
{
  SettingsEvents events;
  SettingsTime time;
  LoadSettingsEvents(&events);
  LoadSettingsTime(&time);
  
  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>PsychoPlug Rule Edit</title>" ENCODING "</head>\n");
  WebPrintf(client, "<body>\n");
  WebPrintf(client, "<h1>Editing rule %d</h1>\n", id+1);

  WebPrintf(client, "<form action=\"update.html\" method=\"POST\">\n");
  WebPrintf(client, "<input type=\"hidden\" name=\"id\" value=\"%d\">\n", id);
  WebPrintf(client, "<table border=\"1px\">\n");
  WebPrintf(client, "<tr><th>#</th><th>All</th><th>Sun</th><th>Mon</th><th>Tue</th><th>Wed</th><th>Thu</th><th>Fri</th><th>Sat</th><th>Time</th><th>Action</th></tr>\n");
  WebPrintf(client, "<tr><td>%d.</td>\n", id+1);
  WebPrintf(client, "<td><button type=\"button\" onclick=\"x=document.getElementById('a').checked?false:true; ");
  for (byte j=0; j<7; j++) {
    WebPrintf(client, "document.getElementById('%c').checked = x; ", 'a'+j);
  }
  WebPrintf(client, "\">All</button></td>\n");
  for (byte j=0; j<7; j++) {
    WebPrintf(client, "<td><input type=\"checkbox\" id=\"%c\" name=\"%c\" %s></td>\n", 'a'+j, 'a'+j, events.event[id].dayMask & (1<<j)?"checked":"");
  }
    WebPrintf(client, "<td><select name=\"hr\">");
  if (time.use12hr) {
    int selhr = events.event[id].hour%12;
    if (!selhr) selhr = 12;
    for (int j=1; j<=12; j++) {
      WebPrintf(client, "<option %s>%d</option>", selhr==j?"selected":"", j)
    }
  } else {
    for (int j=0; j<24; j++) {
      WebPrintf(client, "<option %s>%d</option>", events.event[id].hour==j?"selected":"", j)
    }
  }
  WebPrintf(client, "</select>:<select name=\"mn\">");
  char *buff = (char *)alloca(20*60+10);
  int len=0;
  for (int j=0; j<60; j++) {
    sprintf_P(buff+len, PSTR("<option %s>%02d</option>"), (events.event[id].minute==j)?"selected":"", j);
    len += strlen(buff+len);
  }
  client->print(buff);
  WebPrintf(client, "</select> ");
  if (time.use12hr)
    WebPrintf(client, "<select name=\"ampm\"><option %s>AM</option><option %s>PM</option></select></td>", events.event[id].hour<12?"selected":"", events.event[id].hour>=12?"selected":"");
  WebPrintf(client, "<td>\n<select name=\"action\">");
  char str[16];
  for (int j=0; j<=ACTION_MAX; j++) WebPrintf(client, "<option %s>%s</option>", events.event[id].action==j?"selected":"", GetActionString(j, str, sizeof(str)));
  WebPrintf(client, "</select></td></table><br>\n");
  WebPrintf(client, "<input type=\"submit\" value=\"Submit\">\n");
  WebPrintf(client, "</form></body></html>\n");
}

// Success page, auto-refresh in 1 sec to index
void SendSuccessHTML(WiFiClient *client)
{
  WebHeaders(client, PSTR("Refresh: 1; url=index.html\r\n"));
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>Success</title>" ENCODING "</head><body><h1><a href=\"index.html\">Success.  Click here if not auto-refreshed</a></h1></body></html>\n");
}


extern "C" {
  sint8 espconn_tcp_set_max_con(uint8 num);
  uint8 espconn_tcp_get_max_con(void);
}

void setup()
{
  Settings settings;

  Serial.begin(115200);
  Serial.flush();
  delay(100);
  LogPrintf("Starting up...\n");
  delay(10);

  espconn_tcp_set_max_con(15); // Allow lots of connections, which happens during wifi discovery by phones/etc.
  LogPrintf("Maximum TCP connections: %d\n", espconn_tcp_get_max_con());
  LogPrintf("Settings size=%d\n", sizeof(settings));
  
  StartButton();
  StartLED();
  StartPowerMonitor();
  
  LogPrintf("Loading Settings\n");

  bool ok = LoadAllSettings(RawButton(), &settings);
  StartRelay(settings.events.onAfterPFail?true:false);

  // Load our certificate and key from FLASH before we start
  https.setServerKeyAndCert_P(rsakey, sizeof(rsakey), x509, sizeof(x509));

  // Make sure ESP isn't doing any wifi operations.  Sometimes starts back up in AP mode, for example
  WiFi.disconnect();
  WiFi.softAPdisconnect(true);

  if (ok) {
    isSetup = true;
    StartSTA(&settings);
  } else {
    isSetup = false;
    StartSetupAP();
  }

  LogPrintf("End setup()\n");
}


void ParseSetupForm(char *params, Settings *settings)
{
  char *valPtr;
  char *namePtr;
  
  // Checkboxes don't actually return values if they're unchecked, so by default these get false
  settings->wifi.useDHCP = false;
  settings->events.onAfterPFail = false;
  settings->mqtt.enable = false;
  settings->mqtt.SSL = false;
  settings->time.use12hr = false;
  settings->time.usedmy = false;
  
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamText("ssid", settings->wifi.ssid);
    ParamText("psk", settings->wifi.psk);
    ParamText("hn", settings->wifi.hostname);
    ParamCheckbox("dh", settings->wifi.useDHCP);
    Param4Int("ip", settings->wifi.ip);
    Param4Int("nm", settings->wifi.netmask);
    Param4Int("gw", settings->wifi.gateway);
    Param4Int("dns", settings->wifi.dns);
    Param4Int("ls", settings->wifi.logsvr);
    
    ParamText("ntp", settings->time.ntp);
    ParamText("tz", settings->time.timezone);
    ParamCheckbox("u12h", settings->time.use12hr);
    ParamCheckbox("udmy", settings->time.usedmy);

    ParamCheckbox("pf", settings->events.onAfterPFail);

    ParamCheckbox("mEn", settings->mqtt.enable);
    ParamText("mh", settings->mqtt.host);
    ParamInt("mp", settings->mqtt.port);
//    int v;
//    ParamInt("voltage", v);
//    if ((v<80) || (v>255)) v=120; // Sanity-check
//    settings.voltage = v;
    ParamCheckbox("ms", settings->mqtt.SSL);
    ParamText("mt", settings->mqtt.topic);
    ParamText("mc", settings->mqtt.clientID);
    ParamText("mu", settings->mqtt.user);
    ParamText("mp", settings->mqtt.pass);

    ParamText("uu", settings->ui.user);
    char tempPass[64];
    ParamText("up", tempPass);
    if (strcmp_P(tempPass, PSTR("*****"))) {
      // Was changed, regenerate salt and store it
      HashPassword(tempPass, settings->ui.salt, settings->ui.passEnc);
    }
    memset(tempPass, 0, sizeof(tempPass)); // I think I'm paranoid
  }
}

void SendRebootHTML(WiFiClient *client)
{
  Settings settings;
  LoadAllSettings(false, &settings);
  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>Setting Configuration</title>" ENCODING "</head><body>\n");
  WebPrintf(client, "<h1>Setting Configuration</h1>");
  WebPrintf(client, "<br>\n");
  PrintSettings(client, &settings);
  if (isSetup) WebPrintf(client, "<hr><h1><a href=\"index.html\">Click here to reconnect after 5 seconds.</a></h1>\n")
  else WebPrintf(client, "<br><h1>PsychoPlug will now reboot and connect to given network</h1>");
  WebPrintf(client, "</body>");
}

void SendResetHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>Resetting PsychoPlug</title>" ENCODING "</head><body>\n");
  WebPrintf(client, "<h1>Resetting the plug, please manually reconnect in 5 seconds.</h1>");
  WebPrintf(client, "</body>");
}

void SendGoToConfigureHTTPS(WiFiClient *client)
{
  LogPrintf("+SendGoToConfigureHTTPS\n");
  WebHeaders(client, NULL);
  LogPrintf("SendGoToConfigureHTTPS: Sent headers\n");
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>Configure PsychoPlug</title>" ENCODING "</head><body>\n");
  WebPrintf(client, "<h1><a href=\"https://%d.%d.%d.%d/index.html\">Go to configuration</a></h1>", setupIP[0], setupIP[1], setupIP[2], setupIP[3]);
  WebPrintf(client, "</body>");
}

void Reset()
{
//  SaveSettings();
  StopSettings();

  // Will hang if you just did serial upload.  Needs powercycle once after upload to function properly.
  ESP.restart();

  // Should never hit here...
  delay(1000000);
}


void HandleConfigSubmit(WiFiClient *client, char *params)
{
  Settings settings;
  ParseSetupForm(params, &settings);
  SaveSettings(&settings);
  SendRebootHTML(client);
  Reset(); // Restarting safer than trying to change wifi/mqtt/etc.
}

void HandleUpdateSubmit(WiFiClient *client, char *params)
{
  char *namePtr;
  char *valPtr;

  // We need the TZ presentation info here
  SettingsTime time;
  LoadSettingsTime(&time);

  // Any of these not defined in the submit, error
  int id = -1;
  int hr = -1;
  int mn = -1;
  int ampm = -1;
  int action = -1;
  byte mask = 0; // Day bitmap
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamInt("id", id);
    ParamInt("hr", hr);
    ParamInt("mn", mn);
    if (time.use12hr) {
      if (!strcmp_P(namePtr, PSTR("ampm"))) {
        if (!strcmp_P(valPtr, PSTR("AM"))) ampm=0;
        else ampm=1;
      }
    } else {
       ampm = 0;
    }
    if (!strcmp_P(namePtr, PSTR("action"))) {
      char str[16];
      for (int i=0; i<=ACTION_MAX; i++)
        if (!strcmp(valPtr, GetActionString(i, str, sizeof(str)))) action = i;
    }
    if (namePtr[0]>='a' && namePtr[0]<='g' && namePtr[1]==0) {
      if (!strcmp_P(valPtr, PSTR("on"))) mask |= 1<<(namePtr[0]-'a');
    }
  }
  bool err = false;
  // Check settings are good
  if (id < 0 || id >= MAXEVENTS) err = true;
  if (time.use12hr) {
    if (hr < 0 || hr > 12) err = true;
  } else {
    if (hr < 0 || hr > 23) err = true;
  }
  if (mn < 0 || mn >= 60) err = true;
  if (ampm < 0 || ampm > 1) err = true;
  if (action < 0 || action > ACTION_MAX ) err = true;

  if (hr==12 && ampm==0 && time.use12hr) { hr = 0; }
  if (err) {
    WebError(client, 400, NULL);
  } else {
    // Update the entry, send refresh page to send back to index
    Settings settings;
    LoadAllSettings(false, &settings);
    settings.events.event[id].dayMask = mask;
    settings.events.event[id].hour = hr + 12 * ampm; // !use12hr => ampm=0, so safe
    settings.events.event[id].minute = mn;
    settings.events.event[id].action = action;
    SaveSettings(&settings); // Store in flash
    SendSuccessHTML(client);
  }
}

void HandleEditHTML(WiFiClient *client, char *params)
{
  int id = -1;
  char *namePtr;
  char *valPtr;
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamInt("id", id);
  }
  if (id >=0 && id < MAXEVENTS) {
    SendEditHTML(client, id);
  } else {
    WebError(client, 400, NULL);
  }
}


void SendOTARedirect(WiFiClient *client)
{
  LogPrintf("+SendOTARedirect\n");
  IPAddress ip = WiFi.localIP();
  WebHeaders(client, NULL);
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>OTA Enabled</title>" ENCODING "</head><body>\n");
  WebPrintf(client, "<h1><a href=\"http://%d.%d.%d.%d:8080/update\">Go to OTA page</a></h1>", ip[0], ip[1], ip[2], ip[3]);
  WebPrintf(client, "</body>");
}




void loop()
{
  static unsigned long lastMS = 0;
  static unsigned long killUpdateTime = 0;

  // Time to restart the plug if the update window is over
  if (killUpdateTime && (millis() > killUpdateTime) ) {
    LogPrintf("Restarting ESP due to update timeout\n");
    ESP.restart();
  } else if (otaServer)  {
    otaServer->handleClient();
  }
  
  if (lastMS>millis() || (millis()-lastMS)>5000) {
    lastMS = millis();
    LogPrintf("@%d: ESP Heap Free=%d\n", lastMS, ESP.getFreeHeap());
  }

  // Let the button toggle the relay always
  ManageButton();

  // Blink the LED appropriate to the state
  ManageLED(isSetup ? LED_CONNECTED : LED_AWAITSETUP);

  // Pump DNS queue
  if (!isSetup) {
//    ManageDNS();
  }
  
  char *url;
  char *params;

  // Any HTTP request, send it to https:// on our IP
  WiFiClient redir = redirector.available();
  if (redir) {
    LogPrintf("HTTP Redirector available\n");
    if (WebReadRequest(&redir, &url, &params, false)) {
      LogPrintf("HTTP Redirector request: %s\n", url);
      char newLoc[64];
      if (isSetup) {
        IPAddress ip = WiFi.localIP();
        snprintf_P(newLoc, sizeof(newLoc), PSTR("Location: https://%d.%d.%d.%d/%s"), ip[0], ip[1], ip[2], ip[3], url[0]?url:"index.html");
        WebError(&redir, 301, newLoc, false);
      } else {
        if (!strcmp_P(url, PSTR("favicon.ico"))) {
          WebError(&redir, 404, NULL);
//        } else if (!strcmp_P(url, PSTR("generate_204"))) {
//          LogPrintf("Sending 301 redirector to https://<>/configure.html\n");
//          snprintf_P(newLoc, sizeof(newLoc), PSTR("Location: https://%d.%d.%d.%d/configure.html"), setupIP[0], setupIP[1], setupIP[2], setupIP[3]);
//          WebError(&redir, 301, newLoc, false);
        } else {
          LogPrintf("Sending redirector web page listing config https link\n");
          SendGoToConfigureHTTPS(&redir);
          LogPrintf("Sent\n");
        }
      }
      redir.flush();
      redir.stop();
      LogPrintf("redir.stop()\n");
    }
  } else if (!isSetup) {
    WiFiClientSecure client = https.available();
    if (client) {
      LogPrintf("+HTTPS setup request\n");
      if (WebReadRequest(&client, &url, &params, false)) {
        if (IsIndexHTML(url)) {
          SendSetupHTML(&client);
        } else if (!strcmp_P(url, PSTR("config.html")) && *params) {
          HandleConfigSubmit(&client, params);
        } else {
          WebError(&client, 404, NULL);
        }
      }
      client.flush();
      client.stop();
      LogPrintf("-HTTPS setup request\n");
    }
  } else {
    ManageMQTT();
    ManageSchedule();
    ManagePowerMonitor();

    WiFiClientSecure client = https.available();
    if (client) {
      StopMQTT(); //
      SettingsUI ui;
      LoadSettingsUI(&ui);
      if (WebReadRequest(&client, &url, &params, true, ui.user, ui.salt, ui.passEnc)) {
        if (IsIndexHTML(url)) {
          SendStatusHTML(&client);
        } else if (!strcmp_P(url, PSTR("on.html"))) {
          PerformAction(ACTION_ON);
          SendSuccessHTML(&client);
        } else if (!strcmp_P(url, PSTR("off.html"))) {
          PerformAction(ACTION_OFF);
          SendSuccessHTML(&client);
        } else if (!strcmp_P(url, PSTR("toggle.html"))) {
          PerformAction(ACTION_TOGGLE);
          SendSuccessHTML(&client);
        } else if (!strcmp_P(url, PSTR("pulseoff.html"))) {
          PerformAction(ACTION_PULSEOFF);
          SendSuccessHTML(&client);
        } else if (!strcmp_P(url, PSTR("pulseon.html"))) {
          PerformAction(ACTION_PULSEON);
          SendSuccessHTML(&client);
        } else if (!strcmp_P(url, PSTR("status.html"))) {
          WebPrintf(&client, "%d", GetRelay()?1:0);
        } else if (!strcmp_P(url, PSTR("hang.html"))) {
          SendResetHTML(&client);
          Reset(); // Restarting safer than trying to change wifi/mqtt/etc.
        } else if (!strcmp_P(url, PSTR("edit.html")) && *params) {
          HandleEditHTML(&client, params);
        } else if (!strcmp_P(url, PSTR("update.html")) && *params) {
          HandleUpdateSubmit(&client, params);
        } else if (!strcmp_P(url, PSTR("reconfig.html"))) {
          SendSetupHTML(&client);
        } else if (!strcmp_P(url, PSTR("config.html")) && *params) {
          HandleConfigSubmit(&client, params);
        } else if (!strcmp_P(url, PSTR("enableupdate.html"))) {
          otaUpdateServer = new ESP8266HTTPUpdateServer;
          otaServer = new ESP8266WebServer(8080);
          otaUpdateServer->setup(otaServer);
          otaServer->begin();
          killUpdateTime = millis() + 10*60*1000; // now + 10 mins
          SendOTARedirect(&client);
        } else {
          WebError(&client, 404, NULL);
        }
      }
      client.flush();
      client.stop();
    }
  }
}

