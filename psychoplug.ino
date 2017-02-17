/*
  Psychoplug v0.2
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
#include <Hash.h>
#include <MQTTClient.h>

WiFiUDP ntpUDP;

// Global way of writing out dynamic HTML to socket
// snprintf guarantees a null termination
char webBuff[4096]; // Too large for stack, put it on the heap
#define WebPrintf(c, fmt, ...) { snprintf(webBuff, sizeof(webBuff), fmt, ## __VA_ARGS__); (c)->print(webBuff); /*Serial.print(webBuff); Serial.flush();*/}

// Web request line (URL, PARAMs parsed in-line)
char reqBuff[256];

// HTTP interface
WiFiServer webSetup(80);
WiFiServer webIface(80);

// MQTT interface
WiFiClient wifiMQTT;
WiFiClientSecure wifiMQTTSSL;
MQTTClient mqttClient;


const int PIN_RELAY = 15;
const int PIN_LED = 2;
const int PIN_BUTTON = 13;

const int PIN_SDA = 12;
const int PIN_SCL = 0;




static const char *hex="0123456789ABCDEF";
static bool isSetup = false;

const int ACTION_NONE = 0;
const int ACTION_ON = 1;
const int ACTION_OFF = 2;
const int ACTION_TOGGLE = 3;
const int ACTION_PULSEOFF = 4;
const int ACTION_PULSEON = 5;
const char *actionString[] = { "None", "On", "Off", "Toggle", "Pulse Off", "Pulse On" };
const int MAXEVENTS = 24;

typedef struct {
  byte dayMask; // binary flags per-day
  byte hour;
  byte minute;
  byte action;
} Event;

const byte SETTINGSVERSION = 4;
typedef struct {
  byte version;
  char ssid[33];
  char psk[64];
  char hostname[33];
  bool useDHCP;
  byte ip[4];
  byte dns[4];
  byte gateway[4];
  byte netmask[4];
  char ntp[65];
  int utc;
  bool onAfterPFail;
  
  // MQTT Configuration
  bool mqttEnable;
  char mqttHost[33];
  int mqttPort;
  bool mqttSSL;
  char mqttUser[32];
  char mqttPass[64];

  // Web Interface
  bool uiEnable;
  char uiUser[32];
  char uiPassEnc[20];
  char uiSalt[32];
  bool uiHTTPS;

  // Events to process
  Event event[MAXEVENTS];
} Settings;
static Settings settings;


// Set the settings.uiPassEnc to the raw password and callthis to make a new salt and run encryption against it
// Output overwrites the uiPassEnc variable
void HashPassword(const char *pass)
{
  memset(settings.uiSalt, 0, sizeof(settings.uiSalt)); // Clear salt to start
  memset(settings.uiPassEnc, 0, sizeof(settings.uiPassEnc)); // Clear salt to start
  if (pass[0]==0) return; // No password
  for (int i=0; i<sizeof(settings.uiSalt); i++)
    settings.uiSalt[i] = RANDOM_REG32 & 0xff;

  // Now catenate the hash and raw password to temp storage
  char raw[128];
  memset(raw, 0, 128);
  memcpy(raw, settings.uiSalt, sizeof(settings.uiSalt));
  strncpy(raw+sizeof(settings.uiSalt), pass, 64);
  int len = strnlen(pass, 63)+1;
//  while (*pass) { *(pass++) = 0; } // clear out sent-in 
  sha1((uint8_t*)raw, sizeof(settings.uiSalt)+len, (uint8_t*)settings.uiPassEnc);
  memset(raw, 0, 128);
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

byte CalcSettingsChecksum()
{
  byte *p = (byte*)&settings;
  byte c = 0xef;
  for (int j=0; j<sizeof(settings); j++) c ^= *(p++);
  return c;
}

char *hx(byte p)
{
  static char a[3];
  a[0] = hex[(p>>4)&15];
  a[1] = hex[(p)&15];
  a[2] = 0;
  return a;
}

void SerHex(void *pv, int len)
{
  byte *p = (byte*)pv;
  while (len--) Serial.print(hx(*(p++)));
  Serial.println("");
}

void LoadSettings(bool reset)
{
  EEPROM.begin(4096);
  // Try and read from "EEPROM", if that fails use defaults
  byte *p = (byte *)&settings;
  for (int i=0; i<sizeof(settings); i++) {
    byte b = EEPROM.read(i);
    *(p++) = b;
  }
  byte chk = EEPROM.read(sizeof(settings));
  byte notChk = EEPROM.read(sizeof(settings)+1);

  byte calcChk = CalcSettingsChecksum();
  byte notCalcChk = ~calcChk;

  if ((chk != calcChk) || (notChk != notCalcChk) ||(settings.version != SETTINGSVERSION) || (reset)) {
    Serial.println("Setting checksum mismatch, generating default settings");
    Serial.flush();
    memset(&settings, 0, sizeof(settings));
    settings.version = SETTINGSVERSION;
    settings.ssid[0] = 0;
    settings.psk[0] = 0;
    strcpy(settings.hostname, "wifiplug");
    settings.useDHCP = true;
    memset(settings.ip, 0, 4);
    memset(settings.dns, 0, 4);
    memset(settings.gateway, 0, 4);
    memset(settings.netmask, 0, 4);
    strcpy(settings.ntp, "us.pool.ntp.org");
    strcpy(settings.uiUser, "admin");
    HashPassword("admin"); // This will set settings.uiPassEnc
    settings.utc =-7;
    settings.onAfterPFail = false;
    settings.mqttEnable = false;
    settings.uiEnable = true;
    isSetup = false;
  } else {
    Serial.print("Settings restored from EEPROM\n");
    Serial.flush();
    isSetup = true;
  }
  EEPROM.end();
  Serial.print("Salt: ");
  SerHex(settings.uiSalt, sizeof(settings.uiSalt));
  Serial.print("Pass: ");
  SerHex(settings.uiPassEnc, sizeof(settings.uiPassEnc));
}

void SaveSettings()
{
  EEPROM.begin(4096);
  byte *p = (byte *)&settings;
  for (int i=0; i<sizeof(settings); i++) EEPROM.write(i, *(p++));
  byte ck = CalcSettingsChecksum();
  EEPROM.write(sizeof(settings), ck);
  EEPROM.write(sizeof(settings)+1, ~ck);
  EEPROM.end();
    Serial.print("Salt: ");
  SerHex(settings.uiSalt, sizeof(settings.uiSalt));
  Serial.print("Pass: ");
  SerHex(settings.uiPassEnc, sizeof(settings.uiPassEnc));

}

// Return a *static* char * to an IP formatted string, so DO NOT USE MORE THAN ONCE PER LINE
const char *PrintIP(const byte ip[4])
{
  static char str[17]; // 255.255.255.255\0
  sprintf(str, "%d.%d.%d.%d", ip[0], ip[1], ip[2], ip[3]);
  return str;
}

inline const char *PrintBool(bool b) { return b ? "True" : "False"; }

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
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebPrintf(client, "NTP: %s<br>\n", settings.ntp);
  WebPrintf(client, "UTC Offset: %d<br>\n", settings.utc);

  WebPrintf(client, "<br><h1>Power Failure Recovery</h1>\n");
  WebPrintf(client, "On after power failure: %s<br>\n", PrintBool(settings.onAfterPFail));

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  WebPrintf(client, "Host: %s<br>\n", settings.mqttHost);
  WebPrintf(client, "Port: %d<br>\n", settings.mqttPort);
  WebPrintf(client, "Use SSL: %s<br>\n", PrintBool(settings.mqttSSL));
  WebPrintf(client, "User: %s<br>\n", settings.mqttUser);
  WebPrintf(client, "Pass: %s<br>\n", settings.mqttPass);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  WebPrintf(client, "Admin User: %s<br>\n", settings.uiUser);
  WebPrintf(client, "Admin Pass: %s<br>\n", "*HIDDEN*");
  WebPrintf(client, "Enable HTTPS Access: %s<br>\n", PrintBool(settings.uiHTTPS));
}

void WebError(WiFiClient *client, const char *ret, const char *headers, const char *body)
{
  WebPrintf(client, "HTTP/1.1 %s\r\n", ret);
  WebPrintf(client, "Server: WIFIPlug\r\n");
  //fprintf(fp, "Content-length: %d\r\n", strlen(errorPage));
  WebPrintf(client, "Content-type: text/html\r\n");
  WebPrintf(client, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
  WebPrintf(client, "Pragma: no-cache\r\n");
  WebPrintf(client, "Expires: 0\r\n");
  if (headers) WebPrintf(client, "%s\r\n", headers);
  WebPrintf(client, "<html><head><title>%s</title></head>\n", ret);
  WebPrintf(client, "<body><h1>%s</h1><p>%s</p></body></html>\r\n", ret, body);
}

// Check if button has been pressed (after debounce) and return one event for press and one for release
const byte BUTTON_NONE = 0;
const byte BUTTON_PRESS = 1;
const byte BUTTON_RELEASE = 2;

byte ManageButton()
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
      }
      ignoring = false;
    }
  }
  return event;
}

// Handle automated on/off simply on the assumption we don't lose any minutes
static int lastHour = -1;
static int lastMin = -1;
static int lastDOW = -1;
void ManageSchedule()
{

  // If this is a new h/m/dow then go through all events and see if we need to do an action.
  // Only execute after we scan everything, so only action done once even if there are
  // multiple entries for the same time

  time_t t = now();
  int newHour = hour(t);
  int newMin = minute(t);
  int newDOW = weekday(t)-1;
  if (newHour != lastHour || newMin != lastMin || newDOW != lastDOW) {
    Serial.print("ManageSchedule new time check: ");
    Serial.print(newHour); Serial.print(":"); Serial.print(newMin); Serial.print(" weekday "); Serial.println(newDOW);
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
    switch (action) {
      case ACTION_NONE: break;
      case ACTION_ON: digitalWrite(PIN_RELAY, HIGH); break;
      case ACTION_OFF: digitalWrite(PIN_RELAY, LOW); break;
      case ACTION_TOGGLE: digitalWrite(PIN_RELAY, digitalRead(PIN_RELAY)==LOW?HIGH:LOW); break;
      case ACTION_PULSEOFF: digitalWrite(PIN_RELAY, LOW); delay(500); digitalWrite(PIN_RELAY, HIGH); break;
      case ACTION_PULSEON: digitalWrite(PIN_RELAY, HIGH); delay(500); digitalWrite(PIN_RELAY, LOW); break;
    }

    lastHour = hour(t);
    lastMin = minute(t);
    lastDOW = weekday(t)-1; // Sunday=1 for weekday(), we need 0-index
  
  }
}

void WebHeaders(WiFiClient *client, const char *headers)
{
  WebPrintf(client, "HTTP/1.1 200 OK\r\n");
  WebPrintf(client, "Server: WIFIPlug\r\n");
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
  byte i = 0;

  Serial.print("Starting Setup AP Mode\n");
  isSetup = false;
  WiFi.macAddress(mac);
  memcpy(ssid, "WIFIPLUG-", i=9);
  for (byte j=3; j<6; j++) {
    ssid[i++] = hex[(mac[j]&0xf0)>>4];
    ssid[i++] = hex[mac[j]&0x0f];
  }
  ssid[i++] = 0;
  
  Serial.println(ssid);
  WiFi.softAP(ssid);

  Serial.print("Waiting for connection\n");
  webSetup.begin();
}

void StartSTA()
{
  Serial.print("Starting STA Mode\n");
  if (settings.hostname[0]) WiFi.hostname(settings.hostname);
  if (!settings.useDHCP) {
    WiFi.config(settings.ip, settings.gateway, settings.netmask, settings.dns);
  } else {
    byte zero[] = {0,0,0,0};
    WiFi.config(zero, zero, zero, zero);
  }
  if (settings.psk[0]) WiFi.begin(settings.ssid, settings.psk);
  else WiFi.begin(settings.ssid);

  // Try for 15 seconds, then revert to config
  for (byte i=1; i<15 && WiFi.status() != WL_CONNECTED; i++) {
    digitalWrite(PIN_LED, LOW);
    delay(900);
    digitalWrite(PIN_LED, HIGH);
    delay(100);
  }
  if (WiFi.status() != WL_CONNECTED) {
    StartSetupAP();
  } else {
    // Start standard interface
    webIface.begin();
    // Enable NTP timekeeping
    ntpUDP.begin(8675); // 309
    setSyncProvider(getNtpTime);
    setSyncInterval(300);
  }
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
        else if (a>='a' && a<='f') a -= 'a' + 10;
        else if (a>='A' && a<='F') a -= 'A' + 10;
        if (b>='0' && b<='9') b -= '0';
        else if (b>='a' && b<='f') b -= 'a' + 10;
        else if (b>='A' && b<='F') b -= 'A' + 10;
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
  char authBuff[256];

  *urlStr = NULL;
  *paramStr = NULL;
  
  int timeout = 1000; // Max delay before we timeout
  while (!client->available() && timeout) { delay(1); timeout--; }
  if (!timeout) {
    client->flush();
    return false;
  }

  int wlen = (byte)client->readBytesUntil('\r', reqBuff, sizeof(reqBuff)-1);
  reqBuff[wlen] = 0;
  Serial.print("request: ");
  Serial.println(reqBuff);

  if (memcmp(reqBuff, "GET ", 4)) {
    // Not a GET, error
    WebError(client, "405 Method Not Allowed", "Allow: GET\r\n", "Only GET requests supported");
    return false;
  }

  bool empty = true;
  for (int i=0; i<sizeof(settings.uiSalt); i++) if (settings.uiSalt[i]) empty = false;
  if (authReq && settings.uiUser[0] && !empty) {
    int alen;
    do {
      uint8_t newline;
      client->read(&newline, 1); // Get rid of \n
      alen = (byte)client->readBytesUntil('\r', authBuff, sizeof(authBuff)-1);
      authBuff[alen] = 0;
      Serial.print("hdr: "); Serial.println(authBuff);
    } while (alen && strncmp("Authorization: Basic ", authBuff, 21));
    client->flush();
    Serial.print("auth: "); Serial.println(authBuff);
    Serial.print("decoding: "); Serial.println(authBuff+21);
    Base64Decode(authBuff+21);
    Serial.print("decoded: "); Serial.println(authBuff+21);
    char *user = authBuff+21;
    char *pass = user;
    while (*pass && *pass != ':') pass++; // Advance to the : or \0
    if (*pass) { *pass = 0; pass++; } // Skip the :, end the user string
    Serial.print("user: "); Serial.println(user);
    Serial.print("pass: "); Serial.println(pass);
    bool matchUser = !strcmp(user, settings.uiUser);
    bool matchPass = VerifyPassword(pass);
    Serial.print(matchUser?"user: match\n":"user: error\n");
    Serial.print(matchPass?"pass: match\n":"pass: error\n");
    
    if (!authBuff[0] || !matchUser || !matchPass) {
      WebError(client, "401 Unauthorized", "WWW-Authenticate: Basic realm=\"WIFIPlug\"\r\n", "Login required.");
      return false;
    }
  }
  
  // Delete HTTP version
  char *httpVer = strchr(reqBuff+4, ' ');
  if (httpVer) *httpVer = 0;

  URLDecode(reqBuff);

  // Break into URL and form data
  char *url = reqBuff+4;
  while (*url && *url=='/') url++; // Strip off leading /s
  char *qp = strchr(url, '?');
  if (qp) {
    *qp = 0; // End URL
    qp++;
  } else {
    qp = &NUL;
  }

  if (urlStr) *urlStr = url;
  if (paramStr) *paramStr = qp;

  return true;
}

// Scan out and update a pointer into the param string, returning the name and value or false if done
bool ParseParam(char **paramStr, char **name, char **value)
{
  char *data = *paramStr;
  bool done = false;

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

// Setup web page
void SendSetupHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>WIFIPlug Setup</title></head>\n");
  WebPrintf(client, "<body><h1>WIFIPlug Setup</h1>\n");
  WebPrintf(client, "<form action=\"config.html\">\n");

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
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebFormText(client, "NTP Server", "ntp", settings.ntp, true);
  WebFormText(client, "UTC Offset", "utcoffset", settings.utc, true);

  WebPrintf(client, "<br><h1>Power Failure Recovery</h1>\n");
  WebFormCheckbox(client, "Start powered up after power loss", "onafterpfail", settings.onAfterPFail, true);

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  const char *ary2[] = { "mqtthost", "mqttport", "mqttssl", "mqttuser", "mqttpass", "" };
  WebFormCheckboxDisabler(client, "Enable MQTT", "mqttEnable", true, settings.mqttEnable, true, ary2 );
  WebFormText(client, "Host", "mqtthost", settings.mqttHost, settings.mqttEnable);
  WebFormText(client, "Port", "mqttport", settings.mqttPort, settings.mqttEnable);
  WebFormCheckbox(client, "Use SSL", "mqttssl", settings.mqttSSL, settings.mqttEnable);
  WebFormText(client, "User", "mqttuser", settings.mqttUser, settings.mqttEnable);
  WebFormText(client, "Pass", "mqttpass", settings.mqttPass, settings.mqttEnable);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  const char *ary3[] = { "uiuser", "uipass", "uihttps", "" };
  WebFormCheckboxDisabler(client, "Enable Web UI", "uienable", true, settings.uiEnable, true, ary3 );
  WebFormText(client, "Admin User", "uiuser", settings.uiUser, settings.uiEnable);
  WebFormText(client, "Admin Password", "uipass", "", settings.uiEnable);
  WebFormCheckbox(client, "Enable HTTPS", "uihttps", settings.uiHTTPS, settings.uiEnable);

  WebPrintf(client, "<input type=\"submit\" value=\"Submit\">\n");
  WebPrintf(client, "</form></body></html>\n");

  
}


// Status Web Page
void SendStatusHTML(WiFiClient *client)
{
  bool curPower = digitalRead(PIN_RELAY)==LOW;
  
  time_t t = now();
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>WIFIPlug Status</title></head>\n");
  WebPrintf(client, "<body>\n");
  WebPrintf(client, "Current Time: %d:%02d:%02d %d/%d/%d<br>\n", hour(t), minute(t), second(t), month(t), day(t), year(t));
  WebPrintf(client, "Power: %s <a href=\"%s\">Toggle</a><br>\n",curPower?"OFF":"ON", curPower?"on.html":"off.html");

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
  WebPrintf(client, "<html><head><title>WIFIPlug Rule Edit</title></head>\n");
  WebPrintf(client, "<body>\n");
  WebPrintf(client, "<h1>Editing rule %d</h1>\n", id+1);

  WebPrintf(client, "<form action=\"update.html\">\n");
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
  for (int j=0; j<sizeof(actionString)/sizeof(actionString[0]); j++) WebPrintf(client, "<option %s>%s</option>", settings.event[id].action==j?"selected":"", actionString[j]);
  WebPrintf(client, "</select></td></table><br>\n");
  WebPrintf(client, "<input type=\"submit\" value=\"Submit\">\n");
  WebPrintf(client, "</form></body></html>\n");
}

// Success page, auto-refresh in 1 sec to index
void SendSuccessHTML(WiFiClient *client)
{
  WebHeaders(client, "Refresh: 1; url=index.html\r\n");
  WebPrintf(client, "<html><head><title>Success</title></head><body><h1><a href=\"index.html\">Success.  Click here if not auto-refreshed</a></h1></body></html>\n");
}

void setup()
{
  Serial.begin(9600);
  Serial.print("Starting up...\n");
  delay(1000);
  pinMode(PIN_BUTTON, INPUT_PULLUP);
  digitalWrite(PIN_LED, HIGH);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_RELAY, LOW);
  pinMode(PIN_RELAY, OUTPUT);

  // Power Monitoring
  pinMode(PIN_SDA, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL);

  delay(1);
  Serial.print("Loading Settings\n");
  Serial.flush();
  LoadSettings(digitalRead(PIN_BUTTON)==LOW?true:false);
  if (settings.onAfterPFail) digitalWrite(PIN_RELAY, HIGH);

  if (!isSetup) StartSetupAP();
  else StartSTA();

  Serial.print("End setup()\n");
}


int lastReadMS = 0;
uint32_t rawPwr[4];
void ReadPowerMonitor()
{
  char d[16];
  byte count = 0;
  Wire.requestFrom(0, 16);
  lastReadMS = millis();
  while (count < 16) {
    if (Wire.available()) d[count++] = Wire.read();
    else yield();
  }
  rawPwr[0] = ((uint32_t)d[0]<<24) | ((uint32_t)d[1]<<16) | ((uint32_t)d[2]<<8) | ((uint32_t)d[3]);
  rawPwr[1] = ((uint32_t)d[4]<<24) | ((uint32_t)d[5]<<16) | ((uint32_t)d[6]<<8) | ((uint32_t)d[7]);
  rawPwr[2] = ((uint32_t)d[8]<<24) | ((uint32_t)d[9]<<16) | ((uint32_t)d[10]<<8) | ((uint32_t)d[11]);
  rawPwr[3] = ((uint32_t)d[12]<<24) | ((uint32_t)d[13]<<16) | ((uint32_t)d[14]<<8) | ((uint32_t)d[15]);
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
  settings.mqttEnable = false;
  settings.uiEnable = false;
  
  while (ParseParam(&params, &namePtr, &valPtr)) {
    ParamText("ssid", settings.ssid);
    ParamText("pass", settings.psk);
    ParamText("hostname", settings.hostname);
    ParamCheckbox("usedhcp", settings.useDHCP);
    Param4Int("ip", settings.ip);
    Param4Int("netmask", settings.netmask);
    Param4Int("gw", settings.gateway);
    Param4Int("dns", settings.dns);

    ParamText("ntp", settings.ntp);
    ParamInt("utcoffset", settings.utc);

    ParamCheckbox("onafterpfail", settings.onAfterPFail);

    ParamCheckbox("mqttEnable", settings.mqttEnable);
    ParamText("mqtthost", settings.mqttHost);
    ParamInt("mqttport", settings.mqttPort);
    ParamCheckbox("mqttssl", settings.mqttSSL);
    ParamText("mqttuser", settings.mqttUser);
    ParamText("mqttpass", settings.mqttPass);

    ParamCheckbox("uienable", settings.uiEnable);
    ParamText("uiuser", settings.uiUser);
    char tempPass[64];
    ParamText("uipass", tempPass);
    HashPassword(tempPass); // This will set settings.uiPassEnc
    memset(tempPass, 0, sizeof(tempPass));
    ParamCheckbox("uihttps", settings.uiHTTPS);
  }
}

void SendRebootHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>Setting Configuration</title></head><body>\n");
  WebPrintf(client, "<h1>Setting Configuration</h1>");
  WebPrintf(client, "<br>\n");
  PrintSettings(client);
  if (isSetup) WebPrintf(client, "<hr><h1><a href=\"index.html\">Click here to reconnect</a></h1>\n")
  else WebPrintf(client, "<br><h1>WIFIPlug will now reboot and connect to given network</h1>");
  WebPrintf(client, "</body>");
}

const byte ledSetup[20] = {0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1,0,1};
const byte ledActive[20] = {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};
void loop()
{
  // Let the button toggle the relay always
  byte happen = ManageButton();
  if (happen==BUTTON_PRESS) {
    Serial.println("button pressed");
    digitalWrite(PIN_RELAY, digitalRead(PIN_RELAY)==LOW?HIGH:LOW);
  }
  if (happen==BUTTON_RELEASE) Serial.println("button released");

  if (!isSetup) {
    digitalWrite(PIN_LED, ledSetup[(millis()/100)%20]?LOW:HIGH);
  } else {
    digitalWrite(PIN_LED, ledActive[(millis()/100)%20]?LOW:HIGH);
  }
  
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
        client.stop();
        webSetup.stop();
        WiFi.mode(WIFI_OFF);
        StartSTA();
      } else {
        WebError(&client, "404", NULL, "Not Found");
      }
    } else {
      WebError(&client, "400", NULL, "Bad Request");
    }
  } else {
    if (timeStatus() != timeNotSet) {
      if (lastHour == -1) {
        time_t t = now();
        lastHour = hour(t);
        lastMin = minute(t);
        lastDOW = weekday(t) - 1; // We 0-index, weekday() 1-indexes
      }
      ManageSchedule();
    }
    WiFiClient client = webIface.available();
    if (!client) return;

    // All pages lead to the one
    if (WebReadRequest(&client, &url, &params, true)) {
      if (IsIndexHTML(url)) {
        SendStatusHTML(&client);
      } else if (!strcmp("on.html", url)) {
        digitalWrite(PIN_RELAY, HIGH);
        SendSuccessHTML(&client);
      } else if (!strcmp("off.html", url)) {
        digitalWrite(PIN_RELAY, LOW);
        SendSuccessHTML(&client);
      } else if (!strcmp(url, "hang.html")) {
        while (1) ; // WDT will fire and we'll reboot
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
            for (int i=0; i<sizeof(actionString)/sizeof(actionString[0]); i++) if (!strcmp(valPtr, actionString[i])) action = i;
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
        if (action < 0 || action >= sizeof(actionString)/sizeof(actionString[0])) err = true;
        Serial.print("id=");Serial.println(id);Serial.print("hr:"); Serial.println(hr);Serial.print("mn:"); Serial.println(mn); Serial.print("ampm:");Serial.println(ampm);
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
        ntpUDP.stop();
        webIface.stop();
        WiFi.mode(WIFI_OFF);
        // Cause new time to be retrieved.
        lastHour = -1;
        lastMin = -1;
        lastDOW = -1;
        StartSTA();
      } else {
        WebError(&client, "404", NULL, "Not Found");
      }
    }
  }
}


/*-------- NTP code ----------*/
/* Taken from the ESP8266 WebClient NTP sample */
const int NTP_PACKET_SIZE = 48; // NTP time is in the first 48 bytes of message
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets

time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address

  while (ntpUDP.parsePacket() > 0) ; // discard any previously received packets
  Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(settings.ntp, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = ntpUDP.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      ntpUDP.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + settings.utc * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // return 0 if unable to get the time
}

// send an NTP request to the time server at the given address
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  ntpUDP.beginPacket(address, 123); //NTP requests are to port 123
  ntpUDP.write(packetBuffer, NTP_PACKET_SIZE);
  ntpUDP.endPacket();
}

