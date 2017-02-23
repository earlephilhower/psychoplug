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
#include <EEPROM.h>
#include <WiFiUdp.h>
#include <TimeLib.h>
#include <Wire.h>
#include <MQTTClient.h>

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

bool isSetup = false;
Settings settings;


// Global way of writing out dynamic HTML to socket
// snprintf guarantees a null termination
#define WebPrintf(c, fmt, ...) { char webBuff[512]; snprintf(webBuff, sizeof(webBuff), fmt, ## __VA_ARGS__); (c)->print(webBuff); }

// Web request line (URL, PARAMs parsed in-line)
static char reqBuff[512];

// HTTP interface
static WiFiServer webSetup(80);
static WiFiServer webIface(80);


// Return a *static* char * to an IP formatted string, so DO NOT USE MORE THAN ONCE PER LINE
const char *PrintIP(const byte ip[4])
{
  static char str[17]; // 255.255.255.255\0
  sprintf(str, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return str;
}

const char *PrintBool(bool b)
{
  return b ? "True" : "False";
}

const char *encoding = "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>\n";

void PrintSettings(WiFiClient *client)
{
  WebPrintf(client, "Version: %d<br>\n", settings.version);
  
  WebPrintf(client, "<br><h1>WiFi Network</h1>\n");
  WebPrintf(client, "SSID: %s<br>\n", settings.ssid);
  WebPrintf(client, "PSK: %s<br>\n", settings.psk);
  WebPrintf(client, "Hostname: %s<br>\n", settings.hostname);
  WebPrintf(client, "DHCP: %s<br>\n", PrintBool(settings.useDHCP));
  if (!settings.useDHCP) {
    WebPrintf(client, "IP: %s<br>\n", PrintIP(settings.ip));
    WebPrintf(client, "Gateway: %s<br>\n", PrintIP(settings.gateway));
    WebPrintf(client, "Netmask: %s<br>\n", PrintIP(settings.netmask));
    WebPrintf(client, "DNS: %s<br>\n", PrintIP(settings.dns));
  }
  WebPrintf(client, "UDP Log Server: %s:9911 (nc -l -u 9911)<br>\n", PrintIP(settings.logsvr));
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebPrintf(client, "NTP: %s<br>\n", settings.ntp);
  WebPrintf(client, "Timezone: %s<br>\n", settings.timezone);
  WebPrintf(client, "12 hour time format: %s<br>\n", PrintBool(settings.use12hr));
  WebPrintf(client, "DD/MM/YY date format: %s<br>\n", PrintBool(settings.usedmy));
  
  WebPrintf(client, "<br><h1>Power Settings</h1>\n");
  WebPrintf(client, "System Voltage: %d<br>\n", settings.voltage);
  WebPrintf(client, "On after power failure: %s<br>\n", PrintBool(settings.onAfterPFail));

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  WebPrintf(client, "Host: %s<br>\n", settings.mqttHost);
  WebPrintf(client, "Port: %d<br>\n", settings.mqttPort);
  WebPrintf(client, "Use SSL: %s<br>\n", PrintBool(settings.mqttSSL));
  WebPrintf(client, "ClientID: %s<br>\n", settings.mqttClientID);
  WebPrintf(client, "Topic: %s<br>\n", settings.mqttTopic);
  WebPrintf(client, "User: %s<br>\n", settings.mqttUser);
  WebPrintf(client, "Pass: %s<br>\n", settings.mqttPass);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  WebPrintf(client, "Admin User: %s<br>\n", settings.uiUser);
  WebPrintf(client, "Admin Pass: %s<br>\n", "*HIDDEN*");
}

void WebError(WiFiClient *client, const char *ret, const char *headers, const char *body)
{
  WebPrintf(client, "HTTP/1.1 %s\r\n", ret);
  WebPrintf(client, "Server: PsychoPlug\r\n");
  //fprintf(fp, "Content-length: %d\r\n", strlen(errorPage));
  WebPrintf(client, "Content-type: text/html\r\n");
  WebPrintf(client, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
  WebPrintf(client, "Pragma: no-cache\r\n");
  WebPrintf(client, "Expires: 0\r\n");
  if (headers) WebPrintf(client, "%s\r\n", headers);
  WebPrintf(client, "<html><head><title>%s</title>%s</head>\n", ret, encoding);
  WebPrintf(client, "<body><h1>%s</h1><p>%s</p></body></html>\r\n", ret, body);
}



void WebHeaders(WiFiClient *client, const char *headers)
{
  WebPrintf(client, "HTTP/1.1 200 OK\r\n");
  WebPrintf(client, "Server: PsychoPlug\r\n");
  WebPrintf(client, "Content-type: text/html\r\n");
  WebPrintf(client, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
  WebPrintf(client, "Pragma: no-cache\r\n");
  WebPrintf(client, "Expires: 0\r\n");
  if (headers) WebPrintf(client, "%s\r\n", headers);
  WebPrintf(client, "\r\n");
}



void StartSetupAP()
{
  char ssid[16];
  byte mac[6];

  Log("Starting Setup AP Mode\n");

  isSetup = false;
  WiFi.macAddress(mac);
  sprintf(ssid, "PSYCHOPLUG-%02X%02X%02X", mac[3], mac[4], mac[5]);
  WiFi.softAP(ssid);

  Log("Waiting for connection\n");
  webSetup.begin();
}

void StartSTA()
{
  Log("Starting STA Mode\n");
  if (settings.hostname[0]) WiFi.hostname(settings.hostname);
  if (!settings.useDHCP) {
    WiFi.config(settings.ip, settings.gateway, settings.netmask, settings.dns);
  } else {
    byte zero[] = {0,0,0,0};
    WiFi.config(zero, zero, zero, zero);
  }
  if (settings.psk[0]) WiFi.begin(settings.ssid, settings.psk);
  else WiFi.begin(settings.ssid);

  // Try forever.  
  while (WiFi.status() != WL_CONNECTED) {
    ManageLED(LED_CONNECTING);
    ManageButton();
    delay(1);
  }
  
  if (WiFi.status() != WL_CONNECTED) {
    Reset(); // Punt, maybe we're in a weird way.  Reboot and try it again
  } else {
    // Start standard interface
    webIface.begin();
    StartNTP();
  }

  StartLog();
  StartMQTT();
}

// In-place decoder, overwrites source with decoded values.  Needs 0-termination on input
// Try and keep memory needs low, speed not critical
static uint8_t b64lut(uint8_t i)
{
  if (i >= 'A' && i <= 'Z') return i - 'A';
  if (i >= 'a' && i <= 'z') return i - 'a' + 26;
  if (i >= '0' && i <= '9') return i - '0' + 52;
  if (i == '-') return 62;
  if (i == '_') return 63;
  else return 64;// sentinel
}

void Base64Decode(char *str)
{
  char *dest;
  dest = str;

  if (strlen(str)%4) return; // Not multiple of 4 == error
  
  while (*str) {
    uint8_t a = b64lut(*(str++));
    uint8_t b = b64lut(*(str++));
    uint8_t c = b64lut(*(str++));
    uint8_t d = b64lut(*(str++));
    *(dest++) = (a << 2) | ((b & 0x30) >> 4);
    if (c == 64) break;
    *(dest++) = ((b & 0x0f) << 4) | ((c & 0x3c) >> 2);
    if (d == 64) break;
    *(dest++) = ((c & 0x03) << 6) | d;
  }
  *dest = 0; // Terminate the string
}


void URLDecode(char *ptr)
{
  while (*ptr) {
    if (*ptr == '+') {
      *ptr = ' ';
    } else if (*ptr == '%') {
      if (*(ptr+1) && *(ptr+2)) {
        byte a = *(ptr + 1);
        byte b = *(ptr + 2);
        if (a>='0' && a<='9') a -= '0';
        else if (a>='a' && a<='f') a = a - 'a' + 10;
        else if (a>='A' && a<='F') a = a - 'A' + 10;
        if (b>='0' && b<='9') b -= '0';
        else if (b>='a' && b<='f') b = b - 'a' + 10;
        else if (b>='A' && b<='F') b = b - 'A' + 10;
        *ptr = ((a&0x0f)<<4) | (b&0x0f);
        // Safe strcpy the rest of the string back
        char *p1 = ptr + 1;
        char *p2 = ptr + 3;
        while (*p2) { *p1 = *p2; p1++; p2++; }
        *p1 = 0;
      }
      // OTW this is a bad encoding, just pass unchanged
    }
    ptr++;
  }
}

// Only GET allowed
bool WebReadRequest(WiFiClient *client, char **urlStr, char **paramStr, bool authReq)
{
  static char NUL = 0; // Get around writable strings...
  char hdrBuff[256];
  char authBuff[256];

  *urlStr = NULL;
  *paramStr = NULL;
  
  int timeout = 2000; // Max delay before we timeout
  while (!client->available() && timeout) { delay(1); timeout--; }
  if (!timeout) {
    client->flush();
    return false;
  }
  int wlen = client->readBytesUntil('\r', reqBuff, sizeof(reqBuff)-1);
  reqBuff[wlen] = 0;

  int hlen = 0;
  authBuff[0] = 0; // Start w/o authorization hdr
  // Parse through all headers until \r\n\r\n
  do {
    uint8_t newline;
    client->read(&newline, 1); // Get rid of \n
    hlen = (byte)client->readBytesUntil('\r', hdrBuff, sizeof(hdrBuff)-1);
    hdrBuff[hlen] = 0;
    if (!strncmp("Authorization: Basic ", hdrBuff, 21)) {
      strcpy(authBuff, hdrBuff);
    }
  } while (hlen > 0);

  // Check for no password...
  bool empty = true;
  for (unsigned int i=0; i<sizeof(settings.uiSalt); i++) if (settings.uiSalt[i]) empty = false;
  if (authReq && settings.uiUser[0] && !empty) {
    Base64Decode(authBuff+21);
    char *user = authBuff+21;
    char *pass = user;
    while (*pass && *pass != ':') pass++; // Advance to the : or \0
    if (*pass) { *pass = 0; pass++; } // Skip the :, end the user string
    bool matchUser = !strcmp(user, settings.uiUser);
    bool matchPass = VerifyPassword(pass);
    if (!authBuff[0] || !matchUser || !matchPass) {
      WebError(client, "401 Unauthorized", "WWW-Authenticate: Basic realm=\"PsychoPlug\"\r\n", "Login required.");
      return false;
    }
  }
  
  // Delete HTTP version (well, anything after the 2nd space)
  char *ptr = reqBuff;
  while (*ptr && *ptr!=' ') ptr++;
  if (*ptr) ptr++;
  while (*ptr && *ptr!=' ') ptr++;
  *ptr = 0;

  URLDecode(reqBuff);

  char *url;
  char *qp;
  if (!memcmp(reqBuff, "GET ", 4)) {
    client->flush(); // Don't need anything here...
    
    // Break into URL and form data
    url = reqBuff+4;
    while (*url && *url=='/') url++; // Strip off leading /s
    qp = strchr(url, '?');
    if (qp) {
      *qp = 0; // End URL
      qp++;
    } else {
      qp = &NUL;
    }
  } else if (!memcmp(reqBuff, "POST ", 5)) {
    uint8_t newline;
    client->read(&newline, 1); // Get rid of \n

    url = reqBuff+5;
    while (*url && *url=='/') url++; // Strip off leading /s
    qp = strchr(url, '?');
    if (qp) *qp = 0; // End URL @ ?
    // In a POST the params are in the body
    int sizeleft = sizeof(reqBuff) - strlen(reqBuff) - 1;
    qp = reqBuff + strlen(reqBuff) + 1;
    int wlen = client->readBytesUntil('\r', qp, sizeleft-1);
    qp[wlen] = 0;
    client->flush();
    URLDecode(qp);
  } else {
    // Not a GET or POST, error
    WebError(client, "405 Method Not Allowed", "Allow: GET, POST\r\n", "Only GET or POST requests supported.");
    return false;
  }

  if (urlStr) *urlStr = url;
  if (paramStr) *paramStr = qp;

  return true;
}

// Scan out and update a pointeinto the param string, returning the name and value or false if done
bool ParseParam(char **paramStr, char **name, char **value)
{
  char *data = *paramStr;
 
  if (*data==0) return false;
  
  char *namePtr = data;
  while ((*data != 0) && (*data != '=') && (*data != '&')) data++;
  if (*data) { *data = 0; data++; }
  char *valPtr = data;
  if  (*data == '=') data++;
  while ((*data != 0) && (*data != '=') && (*data != '&')) data++;
  if (*data) { *data = 0; data++;}
  
  *paramStr = data;
  *name = namePtr;
  *value = valPtr;

  return true;
}


bool IsIndexHTML(const char *url)
{
  if (!url) return false;
  if (*url==0 || !strcmp("/", url) || !strcmp("/index.html", url) || !strcmp("index.html", url)) return true;
  else return false;
}

void WebFormText(WiFiClient *client, const char *label, const char *name, const char *value, bool enabled)
{
  WebPrintf(client, "%s: <input type=\"text\" name=\"%s\" id=\"%s\" value=\"%s\" %s><br>\n", label, name, name, value, !enabled?"disabled":"");
}
void WebFormText(WiFiClient *client, const char *label, const char *name, const int value, bool enabled)
{
  WebPrintf(client, "%s: <input type=\"text\" name=\"%s\" id=\"%s\" value=\"%d\" %s><br>\n", label, name, name, value, !enabled?"disabled":"");
}
void WebFormCheckbox(WiFiClient *client, const char *label, const char *name, bool checked, bool enabled)
{
  WebPrintf(client, "<input type=\"checkbox\" name=\"%s\" id=\"%s\" %s %s> %s<br>\n", name, name, checked?"checked":"", !enabled?"disabled":"", label);
}
void WebFormCheckboxDisabler(WiFiClient *client, const char *label, const char *name, bool invert, bool checked, bool enabled, const char *ids[])
{
  WebPrintf(client,"<input type=\"checkbox\" name=\"%s\" id=\"%s\" onclick=\"", name, name);
  if (invert) WebPrintf(client, "var x = true; if (this.checked) { x = false; }\n")
  else WebPrintf(client, "var x = false; if (this.checked) { x = true; }\n");
  for (byte i=0; ids[i][0]; i++ ) {
    WebPrintf(client, "document.getElementById('%s').disabled = x;\n", ids[i]);
  }
  WebPrintf(client, "\" %s %s> %s<br>\n", checked?"checked":"", !enabled?"disabled":"", label);
}

// We do the sort in the browser because it has more memory than us. :(
void WebTimezonePicker(WiFiClient *client)
{
  bool reset = true;
  WebPrintf(client, "Timezone: <select name=\"timezone\" id=\"timezone\">\n");
  char str[64];
  while ( GetNextTZ(reset, str, sizeof(str)) ) {
      reset = false;
      WebPrintf(client, "<option value=\"%s\"%s>%s</option>\n", str, !strcmp(str, settings.timezone)?" selected":"", str);
  }
  WebPrintf(client, "</select><br>\n");

  WebPrintf(client, "<script type=\"text/javascript\">\n");
  WebPrintf(client, "function sortSelect(selElem) { \n");
  WebPrintf(client, "  var tmpAry = new Array();\n");
  WebPrintf(client, "  for (var i=0;i<selElem.options.length;i++) {\n");
  WebPrintf(client, "      tmpAry[i] = new Array();\n");
  WebPrintf(client, "      tmpAry[i][0] = selElem.options[i].text;\n");
  WebPrintf(client, "      tmpAry[i][1] = selElem.options[i].value;\n");
  WebPrintf(client, "  }\n");
  WebPrintf(client, "  tmpAry.sort();\n");
  WebPrintf(client, "  while (selElem.options.length > 0) {\n");
  WebPrintf(client, "      selElem.options[0] = null;\n");
  WebPrintf(client, "  }\n");
  WebPrintf(client, "  var toSel = 2;\n");
  WebPrintf(client, "  for (var i=0;i<tmpAry.length;i++) {\n");
  WebPrintf(client, "      var op = new Option(tmpAry[i][0], tmpAry[i][1]);\n");
  WebPrintf(client, "      selElem.options[i] = op;\n");
  WebPrintf(client, "  }\n");
  WebPrintf(client, "  return;\n");
  WebPrintf(client, "}\n");
  WebPrintf(client, "sortSelect(document.getElementById('timezone'));\n");
  WebPrintf(client, "function selectItemByValue(elmnt, value) {\n");
  WebPrintf(client, "  for(var i=0; i < elmnt.options.length; i++) {\n");
  WebPrintf(client, "    if(elmnt.options[i].value === value) { elmnt.selectedIndex = i; break; }\n");
  WebPrintf(client, "  }\n");
  WebPrintf(client, "}\n");
  WebPrintf(client, "setTimeout(function(){selectItemByValue(document.getElementById('timezone'), '%s');}, 500);\n", settings.timezone );
  WebPrintf(client, "</script>\n");

}

// Setup web page
void SendSetupHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>PsychoPlug Setup</title>%s</head>\n", encoding);
  WebPrintf(client, "<body><h1>PsychoPlug Setup</h1>\n");
  WebPrintf(client, "<form action=\"config.html\" method=\"POST\">\n");

  WebPrintf(client, "<br><h1>WiFi Network</h1>\n");
  WebFormText(client, "SSID", "ssid", settings.ssid, true);
  WebFormText(client, "Password", "pass", settings.psk, true);
  WebFormText(client, "Hostname", "hostname", settings.hostname, true);
  const char *ary1[] = {"ip", "netmask", "gw", "dns", ""};
  WebFormCheckboxDisabler(client, "DHCP Networking", "usedhcp", false, settings.useDHCP, true, ary1 );
  WebFormText(client, "IP", "ip", PrintIP(settings.ip), !settings.useDHCP);
  WebFormText(client, "Netmask", "netmask", PrintIP(settings.netmask), !settings.useDHCP);
  WebFormText(client, "Gateway", "gw", PrintIP(settings.gateway), !settings.useDHCP);
  WebFormText(client, "DNS", "dns", PrintIP(settings.dns), !settings.useDHCP);
  WebFormText(client, "UDP Log Server", "logsvr", PrintIP(settings.logsvr), true);
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebFormText(client, "NTP Server", "ntp", settings.ntp, true);
  WebTimezonePicker(client);
  WebFormCheckbox(client, "12hr Time Format", "use12hr", settings.use12hr, true);
  WebFormCheckbox(client, "DD/MM/YY Date Format", "usedmy", settings.usedmy, true);

  WebPrintf(client, "<br><h1>Power</h1>\n");
  WebFormText(client, "Mains Voltage", "voltage", settings.voltage, true);
  WebFormCheckbox(client, "Start powered up after power loss", "onafterpfail", settings.onAfterPFail, true);

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  const char *ary2[] = { "mqtthost", "mqttport", "mqttssl", "mqttuser", "mqttpass", "mqtttopic", "mqttclientid", "" };
  WebFormCheckboxDisabler(client, "Enable MQTT", "mqttEnable", true, settings.mqttEnable, true, ary2 );
  WebFormText(client, "Host", "mqtthost", settings.mqttHost, settings.mqttEnable);
  WebFormText(client, "Port", "mqttport", settings.mqttPort, settings.mqttEnable);
  WebFormCheckbox(client, "Use SSL", "mqttssl", settings.mqttSSL, settings.mqttEnable);
  WebFormText(client, "User", "mqttuser", settings.mqttUser, settings.mqttEnable);
  WebFormText(client, "Pass", "mqttpass", settings.mqttPass, settings.mqttEnable);
  WebFormText(client, "ClientID", "mqttclientid", settings.mqttClientID, settings.mqttEnable);
  WebFormText(client, "Topic", "mqtttopic", settings.mqttTopic, settings.mqttEnable);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  WebFormText(client, "Admin User", "uiuser", settings.uiUser, true);
  WebFormText(client, "Admin Password", "uipass", "*****", true);

  WebPrintf(client, "<input type=\"submit\" value=\"Submit\">\n");
  WebPrintf(client, "</form></body></html>\n");
}


// Status Web Page
void SendStatusHTML(WiFiClient *client)
{
  bool curPower = !GetRelay();
  char buff[64];
  
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>PsychoPlug Status</title>%s</head>\n", encoding);
  WebPrintf(client, "<body>\n");
  WebPrintf(client, "Current Time: %s<br>\n", AscTime(now(), settings.use12hr, settings.usedmy, buff, sizeof(buff)));
  WebPrintf(client, "Power: %s <a href=\"%s\">Toggle</a><br>\n",curPower?"OFF":"ON", curPower?"on.html":"off.html");
  WebPrintf(client, "Current: %dmA (%dW @ %dV)<nr>\n", GetCurrentMA(), (GetCurrentMA()* settings.voltage) / 1000, settings.voltage);

  WebPrintf(client, "<table border=\"1px\">\n");
  WebPrintf(client, "<tr><th>#</th><th>Sun</th><th>Mon</th><th>Tue</th><th>Wed</th><th>Thu</th><th>Fri</th><th>Sat</th><th>Time</th><th>Action</th><th>EDIT</th></tr>");
  for (byte i=0; i<MAXEVENTS; i++) {
    WebPrintf(client, "<tr><td>%d.</td>", i+1);
    for (byte j=0; j<7; j++) {
      WebPrintf(client, "<td><input type=\"checkbox\" disabled%s></td>", (settings.event[i].dayMask & (1<<j))?" checked":"");
    }
    WebPrintf(client, "<td>%d:%02d %s</td>", (settings.event[i].hour)?settings.event[i].hour%12:12, settings.event[i].minute, (settings.event[i].hour<12)?"AM":"PM");
    WebPrintf(client, "<td>%s</td>", actionString[settings.event[i].action]);
    WebPrintf(client, "<td><a href=\"edit.html?id=%d\">Edit</a></td></tr>\r\n", i);
  }
  WebPrintf(client, "</table><br>\n");
  WebPrintf(client, "<a href=\"reconfig.html\">Change System Configuration</a><br\n");
  WebPrintf(client, "</body>\n");
}

// Edit Rule
void SendEditHTML(WiFiClient *client, int id)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>PsychoPlug Rule Edit</title>%s</head>\n", encoding);
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
    WebPrintf(client, "<td><input type=\"checkbox\" id=\"%c\" name=\"%c\" %s></td>\n", 'a'+j, 'a'+j, settings.event[id].dayMask & (1<<j)?" checked":"");
  }
  WebPrintf(client, "<td><select name=\"hr\">");
  for (int j=0; j<12; j++) {
    if (settings.event[id].hour) WebPrintf(client, "<option %s>%d</option>", (settings.event[id].hour%12)==j?"selected":"", j)
    else WebPrintf(client, "<option %s>%d</option>", j==0?"selected":"", j+1);
  }
  WebPrintf(client, "</select>:<select name=\"mn\">");
  for (int j=0; j<60; j++) WebPrintf(client, "<option %s>%02d</option>", (settings.event[id].minute==j)?"selected":"", j);
  WebPrintf(client, "</select> <select name=\"ampm\"><option %s>AM</option><option %s>PM</option></select></td>", settings.event[id].hour<12?"selected":"", settings.event[id].hour>=12?"selected":"");
  WebPrintf(client, "<td>\n<select name=\"action\">");
  for (int j=0; j<=ACTION_MAX; j++) WebPrintf(client, "<option %s>%s</option>", settings.event[id].action==j?"selected":"", actionString[j]);
  WebPrintf(client, "</select></td></table><br>\n");
  WebPrintf(client, "<input type=\"submit\" value=\"Submit\">\n");
  WebPrintf(client, "</form></body></html>\n");
}

// Success page, auto-refresh in 1 sec to index
void SendSuccessHTML(WiFiClient *client)
{
  WebHeaders(client, "Refresh: 1; url=index.html\r\n");
  WebPrintf(client, "<html><head><title>Success</title>%s</head><body><h1><a href=\"index.html\">Success.  Click here if not auto-refreshed</a></h1></body></html>\n", encoding);
}


void setup()
{
  Serial.begin(9600);
  Log("Starting up...\n");
  delay(100);

  StartRelay();
  StartButton();
  StartLED();
  StartPowerMonitor();
  delay(1);
  
  Log("Loading Settings\n");
  
  bool ok = LoadSettings(RawButton());
  SetRelay(settings.onAfterPFail?true:false); 

  if (ok) {
    isSetup = true;
    StartSTA();
  } else {
    isSetup = false;
    StartSetupAP();
  }

  

  Log("End setup()\n");
}


// Scan an integer from a string, place it into dest, and then return # of bytes scanned
int ParseInt(char *src, int *dest)
{
  byte count = 0;
  bool neg = false;
  int res = 0;
  if (!src) return 0;
  if (src[0] == '-') {neg = true; src++; count++;}
  while (*src && (*src>='0') && (*src<='9')) {
    res = res * 10;
    res += *src - '0';
    src++;
    count++;
  }
  if (neg) res *= -1;
  if (dest) *dest = res;
  return count;
}

void Read4Int(char *str, byte *p)
{
  int i;
  str += ParseInt(str, &i); p[0] = i; if (*str) str++;
  str += ParseInt(str, &i); p[1] = i; if (*str) str++;
  str += ParseInt(str, &i); p[2] = i; if (*str) str++;
  str += ParseInt(str, &i); p[3] = i;
}


#define ParamText(name, dest)     { if (!strcmp(namePtr, (name))) strlcpy((dest), valPtr, sizeof(dest)); }
#define ParamCheckbox(name, dest) { if (!strcmp(namePtr, (name))) (dest) = !strcmp("on", valPtr); }
#define ParamInt(name, dest)      { if (!strcmp(namePtr, (name))) ParseInt(valPtr, &dest); }
#define Param4Int(name, dest)     { if (!strcmp(namePtr, (name))) Read4Int(valPtr, (dest)); }

void ParseSetupForm(char *params)
{
  char *valPtr;
  char *namePtr;
  
  // Checkboxes don't actually return values if they're unchecked, so by default these get false
  settings.useDHCP = false;
  settings.onAfterPFail = false;
  settings.mqttEnable = false;
  settings.mqttSSL = false;
  
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamText("ssid", settings.ssid);
    ParamText("pass", settings.psk);
    ParamText("hostname", settings.hostname);
    ParamCheckbox("usedhcp", settings.useDHCP);
    Param4Int("ip", settings.ip);
    Param4Int("netmask", settings.netmask);
    Param4Int("gw", settings.gateway);
    Param4Int("dns", settings.dns);
    Param4Int("logsvr", settings.logsvr);
    
    ParamText("ntp", settings.ntp);
    ParamText("timezone", settings.timezone);
    ParamCheckbox("use12hr", settings.use12hr);
    ParamCheckbox("usedmy", settings.usedmy);

    ParamCheckbox("onafterpfail", settings.onAfterPFail);

    ParamCheckbox("mqttEnable", settings.mqttEnable);
    ParamText("mqtthost", settings.mqttHost);
    ParamInt("mqttport", settings.mqttPort);
    int v;
    ParamInt("voltage", v);
    if ((v<80) || (v>255)) v=120; // Sanity-check
    settings.voltage = v;
    ParamCheckbox("mqttssl", settings.mqttSSL);
    ParamText("mqtttopic", settings.mqttTopic);
    ParamText("mqttclientid", settings.mqttClientID);
    ParamText("mqttuser", settings.mqttUser);
    ParamText("mqttpass", settings.mqttPass);

    ParamText("uiuser", settings.uiUser);
    char tempPass[64];
    ParamText("uipass", tempPass);
    if (strcmp("*****", tempPass)) {
      // Was changed, regenerate salt and store it
      HashPassword(tempPass); // This will set settings.uiPassEnc
    }
    memset(tempPass, 0, sizeof(tempPass)); // I think I'm paranoid
  }
}

void SendRebootHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>Setting Configuration</title>%s</head><body>\n", encoding);
  WebPrintf(client, "<h1>Setting Configuration</h1>");
  WebPrintf(client, "<br>\n");
  PrintSettings(client);
  if (isSetup) WebPrintf(client, "<hr><h1><a href=\"index.html\">Click here to reconnect after 5 seconds.</a></h1>\n")
  else WebPrintf(client, "<br><h1>PsychoPlug will now reboot and connect to given network</h1>");
  WebPrintf(client, "</body>");
}

void SendResetHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>Resetting PsychoPlug</title>%s</head><body>\n", encoding);
  WebPrintf(client, "<h1>Resetting the plug, please manually reconnect in 5 seconds.</h1>");
  WebPrintf(client, "</body>");
}

void Reset()
{
  // Will hang if you just did serial upload.  Needs powercycle once after upload to function properly.
  ESP.restart();
}

void loop()
{
  // Want to know current year to set the timezone properly, wait until setup() has completed
  static bool settz = false;
  if (!settz) {
    settz = true;
    SetTZ(settings.timezone);
  }

  // Let the button toggle the relay always
  ManageButton();

  // Blink the LED appropriate to the state
  ManageLED(isSetup ? LED_CONNECTED :  LED_AWAITSETUP);
  
  char *url;
  char *params;
  char *namePtr;
  char *valPtr;

  if (!isSetup) {
    WiFiClient client = webSetup.available();
    if (!client) return;
    
    if (WebReadRequest(&client, &url, &params, false)) {
      if (IsIndexHTML(url)) {
        SendSetupHTML(&client);
      } else if (!strcmp(url, "config.html") && *params) {
        ParseSetupForm(params);
        SendRebootHTML(&client);
        SaveSettings();
        isSetup = true;
        delay(500);
        client.stop();
        delay(500);
        webSetup.stop();
        delay(500);
        WiFi.mode(WIFI_OFF);
        delay(500);
        Reset(); // Restarting safer than trying to change wifi/mqtt/etc.
      } else {
        WebError(&client, "404", NULL, "Not Found");
      }
    } else {
      WebError(&client, "400", NULL, "Bad Request");
    }
  } else {
    ManageMQTT();
    ManageSchedule();
    ManagePowerMonitor();
    
    WiFiClient client = webIface.available();
    if (!client) return;

    if (WebReadRequest(&client, &url, &params, true)) {
      if (IsIndexHTML(url)) {
        SendStatusHTML(&client);
      } else if (!strcmp("on.html", url)) {
        SetRelay(true);
        SendSuccessHTML(&client);
      } else if (!strcmp("off.html", url)) {
        SetRelay(false);
        SendSuccessHTML(&client);
      } else if (!strcmp(url, "hang.html")) {
        SendResetHTML(&client);
        delay(500);
        client.stop();
        delay(500);
        webIface.stop();
        delay(500);
        WiFi.mode(WIFI_OFF);
        delay(500);
        Reset(); // Restarting safer than trying to change wifi/mqtt/etc.
      } else if (!strcmp("edit.html", url) && *params) {
        int id = -1;
        while (ParseParam(&params, &namePtr, &valPtr)) {
          ParamInt("id", id);
        }
        if (id >=0 && id < MAXEVENTS) {
          SendEditHTML(&client, id);
        } else {
          WebError(&client, "400", NULL, "Bad Request");
        }
      } else if (!strcmp("update.html", url) && *params) {
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
          if (!strcmp(namePtr, "ampm")) { if (!strcmp("AM", valPtr)) ampm=0; else if (!strcmp("PM", valPtr)) ampm=1; }
          if (!strcmp(namePtr, "action")) {
            for (int i=0; i<=ACTION_MAX; i++) if (!strcmp(valPtr, actionString[i])) action = i;
          }
          if (namePtr[0]>='a' && namePtr[0]<='g' && namePtr[1]==0) {
            if (!strcmp(valPtr, "on")) mask |= 1<<(namePtr[0]-'a');
          }
        }
        bool err = false;
        // Check settings are good
        if (id < 0 || id >= MAXEVENTS) err = true;
        if (hr < 0 || hr > 12) err = true;
        if (mn < 0 || mn >= 60) err = true;
        if (ampm < 0 || ampm > 1) err = true;
        if (action < 0 || action > ACTION_MAX ) err = true;
        
        if (hr==12 && ampm==0) { hr = 0; }
        if (err) {
          WebError(&client, "400", NULL, "Bad Request");
        } else {
          // Update the entry, send refresh page to send back to index
          settings.event[id].dayMask = mask;
          settings.event[id].hour = hr + 12 * ampm;
          settings.event[id].minute = mn;
          settings.event[id].action = action;
          SaveSettings(); // Store in flash
          SendSuccessHTML(&client);
        }
      } else if (!strcmp("reconfig.html", url)) {
        SendSetupHTML(&client);
      } else if (!strcmp(url, "config.html") && *params) {
        ParseSetupForm(params);
        SendRebootHTML(&client);
        SaveSettings();
        client.stop();
        delay(500);
        client.stop();
        delay(500);
        webIface.stop();
        delay(500);
        WiFi.mode(WIFI_OFF);
        delay(500);
        Reset(); // Restarting safer than trying to change wifi/mqtt/etc.
      } else {
        WebError(&client, "404", NULL, "Not Found");
      }
    }
  }
}

