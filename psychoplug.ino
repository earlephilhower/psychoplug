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

// Global way of writing out dynamic HTML to socket
// snprintf guarantees a null termination
#define WebPrintf(c, fmt, ...) { char webBuff[192]; snprintf_P(webBuff, sizeof(webBuff), PSTR(fmt), ## __VA_ARGS__); (c)->print(webBuff); }

// Web request line (URL, PARAMs parsed in-line)
static char reqBuff[384];

// All it does is redirect to https://<given url>
static WiFiServer redirector(80);

// HTTPS interface
static const uint8_t rsakey[]  ICACHE_RODATA_ATTR = {
  0x30, 0x82, 0x01, 0x39, 0x02, 0x01, 0x00, 0x02, 0x41, 0x00, 0xaa, 0x35,
  0xe9, 0x39, 0x5d, 0x84, 0xe5, 0x3d, 0x85, 0xb0, 0x77, 0xbb, 0xe8, 0x34,
  0xef, 0x3b, 0x7e, 0xb7, 0x45, 0x2c, 0x43, 0xc1, 0x9c, 0xb1, 0x98, 0x78,
  0xb4, 0xc3, 0xf2, 0x11, 0x41, 0x5d, 0x17, 0xe9, 0x32, 0x5f, 0xf6, 0xa8,
  0x01, 0x8f, 0x1d, 0x73, 0x3a, 0x77, 0x8f, 0xf2, 0x6a, 0xd7, 0x66, 0xcc,
  0xec, 0xf4, 0x0c, 0xc0, 0x0a, 0xe8, 0x6c, 0x6c, 0x2f, 0xe5, 0xa0, 0x04,
  0x53, 0x2f, 0x02, 0x03, 0x01, 0x00, 0x01, 0x02, 0x40, 0x1f, 0xb9, 0x72,
  0xa1, 0xcd, 0x8b, 0x72, 0x8a, 0xb7, 0xfb, 0x3f, 0x25, 0xd7, 0xce, 0xdd,
  0xf4, 0x11, 0xd7, 0x28, 0xde, 0x6a, 0x36, 0x90, 0xf5, 0x0c, 0xfd, 0x1a,
  0x7c, 0x17, 0x07, 0x1e, 0x4e, 0x64, 0xcf, 0x36, 0x32, 0x33, 0x30, 0xd7,
  0xce, 0xcf, 0xb8, 0xc2, 0xd8, 0x3b, 0x89, 0x3a, 0xa0, 0x21, 0xd7, 0x46,
  0x47, 0x6c, 0x94, 0x4e, 0x29, 0x31, 0xb1, 0x85, 0xff, 0xde, 0xc8, 0x22,
  0xb9, 0x02, 0x21, 0x00, 0xd3, 0xb1, 0x28, 0x42, 0x08, 0xcd, 0xd8, 0x7d,
  0x17, 0xd2, 0x3f, 0xaf, 0x94, 0x5a, 0xbb, 0x2b, 0x5f, 0x72, 0x73, 0x5c,
  0xbf, 0x6c, 0xd3, 0xc5, 0xfa, 0x3c, 0x6f, 0x2e, 0xd4, 0x66, 0x42, 0xc5,
  0x02, 0x21, 0x00, 0xcd, 0xd6, 0x1a, 0x79, 0xf2, 0x66, 0x2e, 0x06, 0x10,
  0x9e, 0xa4, 0x34, 0x59, 0x08, 0xaa, 0xa2, 0x70, 0xf3, 0x91, 0xc8, 0x5e,
  0x08, 0xbc, 0x95, 0x67, 0x20, 0x70, 0x65, 0x36, 0x0a, 0x8d, 0x63, 0x02,
  0x20, 0x31, 0xa9, 0xbb, 0x27, 0x1a, 0xc7, 0xa8, 0x05, 0x58, 0xe4, 0x53,
  0x5a, 0x8e, 0xb4, 0x66, 0x7a, 0xbb, 0x6e, 0xaf, 0x4f, 0xcf, 0xfc, 0xe9,
  0x05, 0xe6, 0x39, 0xed, 0x5f, 0x90, 0x26, 0xd9, 0x7d, 0x02, 0x20, 0x51,
  0x42, 0xd2, 0x67, 0x0c, 0x54, 0x5c, 0x9d, 0x16, 0x58, 0x9a, 0xf8, 0xf3,
  0xa1, 0x61, 0x44, 0x0a, 0x15, 0x8e, 0x9b, 0x2c, 0xcc, 0x84, 0x2e, 0xf3,
  0x6c, 0xb2, 0x12, 0xb2, 0xf8, 0x0d, 0x83, 0x02, 0x20, 0x34, 0xe5, 0x70,
  0x54, 0x82, 0xbb, 0x00, 0x87, 0x1d, 0x7b, 0x1e, 0x89, 0x53, 0xea, 0xea,
  0xa1, 0x34, 0x89, 0xad, 0x10, 0x3d, 0x94, 0xa7, 0x2d, 0x3b, 0xfe, 0x90,
  0x25, 0x2a, 0xf2, 0xc1, 0xd6
};

static const uint8_t x509[] ICACHE_RODATA_ATTR = {
  0x30, 0x82, 0x01, 0x43, 0x30, 0x81, 0xee, 0x02, 0x09, 0x00, 0xa2, 0xe5,
  0xf5, 0x52, 0x83, 0xa0, 0xbc, 0x49, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86,
  0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x30, 0x29, 0x31,
  0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c, 0x0a, 0x70, 0x73,
  0x79, 0x63, 0x68, 0x6f, 0x70, 0x6c, 0x75, 0x67, 0x31, 0x12, 0x30, 0x10,
  0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x09, 0x31, 0x32, 0x37, 0x2e, 0x30,
  0x2e, 0x30, 0x2e, 0x31, 0x30, 0x1e, 0x17, 0x0d, 0x31, 0x37, 0x30, 0x32,
  0x32, 0x35, 0x31, 0x31, 0x32, 0x37, 0x30, 0x36, 0x5a, 0x17, 0x0d, 0x33,
  0x30, 0x31, 0x31, 0x30, 0x34, 0x31, 0x31, 0x32, 0x37, 0x30, 0x36, 0x5a,
  0x30, 0x29, 0x31, 0x13, 0x30, 0x11, 0x06, 0x03, 0x55, 0x04, 0x0a, 0x0c,
  0x0a, 0x70, 0x73, 0x79, 0x63, 0x68, 0x6f, 0x70, 0x6c, 0x75, 0x67, 0x31,
  0x12, 0x30, 0x10, 0x06, 0x03, 0x55, 0x04, 0x03, 0x0c, 0x09, 0x31, 0x32,
  0x37, 0x2e, 0x30, 0x2e, 0x30, 0x2e, 0x31, 0x30, 0x5c, 0x30, 0x0d, 0x06,
  0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00,
  0x03, 0x4b, 0x00, 0x30, 0x48, 0x02, 0x41, 0x00, 0xaa, 0x35, 0xe9, 0x39,
  0x5d, 0x84, 0xe5, 0x3d, 0x85, 0xb0, 0x77, 0xbb, 0xe8, 0x34, 0xef, 0x3b,
  0x7e, 0xb7, 0x45, 0x2c, 0x43, 0xc1, 0x9c, 0xb1, 0x98, 0x78, 0xb4, 0xc3,
  0xf2, 0x11, 0x41, 0x5d, 0x17, 0xe9, 0x32, 0x5f, 0xf6, 0xa8, 0x01, 0x8f,
  0x1d, 0x73, 0x3a, 0x77, 0x8f, 0xf2, 0x6a, 0xd7, 0x66, 0xcc, 0xec, 0xf4,
  0x0c, 0xc0, 0x0a, 0xe8, 0x6c, 0x6c, 0x2f, 0xe5, 0xa0, 0x04, 0x53, 0x2f,
  0x02, 0x03, 0x01, 0x00, 0x01, 0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48,
  0x86, 0xf7, 0x0d, 0x01, 0x01, 0x05, 0x05, 0x00, 0x03, 0x41, 0x00, 0x6d,
  0x3f, 0x2d, 0xcc, 0xb0, 0x4a, 0xd3, 0x08, 0x28, 0x17, 0x89, 0x72, 0x43,
  0xfb, 0x1b, 0x64, 0x88, 0xf1, 0xb4, 0xcd, 0xa6, 0xd8, 0x5f, 0xf9, 0x31,
  0x44, 0xff, 0x32, 0xc4, 0x90, 0xc6, 0xf0, 0xd5, 0xfd, 0xfb, 0x67, 0x4f,
  0x53, 0x69, 0xb0, 0x58, 0x2d, 0x8f, 0x95, 0xbc, 0x2b, 0x4c, 0x20, 0xf6,
  0x54, 0x09, 0x1c, 0xe8, 0x9a, 0xc0, 0xd6, 0xec, 0x33, 0x07, 0xfd, 0xed,
  0x82, 0x9f, 0xb8
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

const char *FormatBool(bool b)
{
  return b ? "True" : "False";
}

#define ENCODING "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=utf-8\"/>\n"

void PrintSettings(WiFiClient *client)
{
  char buff[16];

  WebPrintf(client, "Version: %d<br>\n", settings.version);
  
  WebPrintf(client, "<br><h1>WiFi Network</h1>\n");
  WebPrintf(client, "SSID: %s<br>\n", settings.ssid);
  WebPrintf(client, "PSK: %s<br>\n", settings.psk);
  WebPrintf(client, "Hostname: %s<br>\n", settings.hostname);
  WebPrintf(client, "DHCP: %s<br>\n", FormatBool(settings.useDHCP));
  if (!settings.useDHCP) {
    WebPrintf(client, "IP: %s<br>\n", FormatIP(settings.ip, buff, sizeof(buff)));
    WebPrintf(client, "Gateway: %s<br>\n", FormatIP(settings.gateway, buff, sizeof(buff)));
    WebPrintf(client, "Netmask: %s<br>\n", FormatIP(settings.netmask, buff, sizeof(buff)));
    WebPrintf(client, "DNS: %s<br>\n", FormatIP(settings.dns, buff, sizeof(buff)));
  }
  WebPrintf(client, "UDP Log Server: %s:9911 (nc -l -u 9911)<br>\n", FormatIP(settings.logsvr, buff, sizeof(buff)));
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebPrintf(client, "NTP: %s<br>\n", settings.ntp);
  WebPrintf(client, "Timezone: %s<br>\n", settings.timezone);
  WebPrintf(client, "12 hour time format: %s<br>\n", FormatBool(settings.use12hr));
  WebPrintf(client, "DD/MM/YY date format: %s<br>\n", FormatBool(settings.usedmy));
  
  WebPrintf(client, "<br><h1>Power Settings</h1>\n");
//  WebPrintf(client, "System Voltage: %d<br>\n", settings.voltage);
  WebPrintf(client, "On after power failure: %s<br>\n", FormatBool(settings.onAfterPFail));

  WebPrintf(client, "<br><H1>MQTT</h1>\n");
  WebPrintf(client, "Enabled: %s<br>\n", FormatBool(settings.mqttEnable));
  WebPrintf(client, "Host: %s<br>\n", settings.mqttHost);
  WebPrintf(client, "Port: %d<br>\n", settings.mqttPort);
  WebPrintf(client, "Use SSL: %s<br>\n", FormatBool(settings.mqttSSL));
  WebPrintf(client, "ClientID: %s<br>\n", settings.mqttClientID);
  WebPrintf(client, "Topic: %s<br>\n", settings.mqttTopic);
  WebPrintf(client, "User: %s<br>\n", settings.mqttUser);
  WebPrintf(client, "Pass: %s<br>\n", settings.mqttPass);

  WebPrintf(client, "<br><h1>Web UI</h1>\n");
  WebPrintf(client, "Admin User: %s<br>\n", settings.uiUser);
  WebPrintf(client, "Admin Pass: %s<br>\n", "*HIDDEN*");
}

void WebPrintError(WiFiClient *client, int code)
{
  switch(code) {
    case 301: WebPrintf(client, "301 Moved Permanently"); break;
    case 400: WebPrintf(client, "400 Bad Request"); break;
    case 401: WebPrintf(client, "401 Unauthorized"); break;
    case 404: WebPrintf(client, "404 Not Found"); break;
    case 405: WebPrintf(client, "405 Method Not Allowed"); break;
    default:  WebPrintf(client, "500 Server Error"); break;
  }
}

void WebError(WiFiClient *client, int code, const char *headers)
{
  WebPrintf(client, "HTTP/1.1 %d\r\n", code);
  WebPrintf(client, "Server: PsychoPlug\r\n");
  //fprintf(fp, "Content-length: %d\r\n", strlen(errorPage));
  WebPrintf(client, "Content-type: text/html\r\n");
  WebPrintf(client, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
  WebPrintf(client, "Pragma: no-cache\r\n");
  WebPrintf(client, "Expires: 0\r\n");
  if (headers) WebPrintf(client, "%s", headers);
  WebPrintf(client, "\r\n\r\n<html><head><title>");
  WebPrintError(client, code);
  WebPrintf(client, "</title>" ENCODING "</head>\n");
  WebPrintf(client, "<body><h1>");
  WebPrintError(client, code);
  WebPrintf(client, "</h1></body></html>\r\n");
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

  LogPrintf("Starting Setup AP Mode\n");

  isSetup = false;
  WiFi.macAddress(mac);
  snprintf_P(ssid, sizeof(ssid), PSTR("PSYCHOPLUG-%02X%02X%02X"), mac[3], mac[4], mac[5]);
  WiFi.softAP(ssid);

  LogPrintf("Waiting for connection\n");
  https.begin();
  redirector.begin();
}

void StartSTA()
{
  LogPrintf("Starting STA Mode\n");
  
  if (settings.hostname[0])
    WiFi.hostname(settings.hostname);
  
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
  }

  IPAddress ip = WiFi.localIP();
  LogPrintf("IP:%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

  StartMQTT();
    
  // Start standard interface
  https.begin();
  redirector.begin();
  StartNTP();
  StartLog();
  
  SetTZ(settings.timezone);
  (void)LocalTime(now());
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

bool WebReadRequest(WiFiClient *client, char **urlStr, char **paramStr, bool authReq)
{
  static char NUL = 0; // Get around writable strings...
  char hdrBuff[128];
  char authBuff[128];

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
    if (!strncmp_P(hdrBuff, PSTR("Authorization: Basic "), 21)) {
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
      WebError(client, 401, "WWW-Authenticate: Basic realm=\"PsychoPlug\"");
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
    WebError(client, 405, "Allow: GET, POST");
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
  if (*url==0 || !strcmp_P(url, PSTR("/")) || !strcmp_P(url, PSTR("/index.html")) || !strcmp_P(url, PSTR("index.html"))) return true;
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
  char buff[16];
  
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>PsychoPlug Setup</title>" ENCODING "</head>\n");
  WebPrintf(client, "<body><h1>PsychoPlug Setup</h1>\n");
  WebPrintf(client, "<form action=\"config.html\" method=\"POST\">\n");

  WebPrintf(client, "<br><h1>WiFi Network</h1>\n");
  WebFormText(client, "SSID", "ssid", settings.ssid, true);
  WebFormText(client, "Password", "pass", settings.psk, true);
  WebFormText(client, "Hostname", "hostname", settings.hostname, true);
  const char *ary1[] = {"ip", "netmask", "gw", "dns", ""};
  WebFormCheckboxDisabler(client, "DHCP Networking", "usedhcp", false, settings.useDHCP, true, ary1 );
  WebFormText(client, "IP", "ip", FormatIP(settings.ip, buff, sizeof(buff)), !settings.useDHCP);
  WebFormText(client, "Netmask", "netmask", FormatIP(settings.netmask, buff, sizeof(buff)), !settings.useDHCP);
  WebFormText(client, "Gateway", "gw", FormatIP(settings.gateway, buff, sizeof(buff)), !settings.useDHCP);
  WebFormText(client, "DNS", "dns", FormatIP(settings.dns, buff, sizeof(buff)), !settings.useDHCP);
  WebFormText(client, "UDP Log Server", "logsvr", FormatIP(settings.logsvr, buff, sizeof(buff)), true);
  
  WebPrintf(client, "<br><h1>Timekeeping</h1>\n");
  WebFormText(client, "NTP Server", "ntp", settings.ntp, true);
  WebTimezonePicker(client);
  WebFormCheckbox(client, "12hr Time Format", "use12hr", settings.use12hr, true);
  WebFormCheckbox(client, "DD/MM/YY Date Format", "usedmy", settings.usedmy, true);

  WebPrintf(client, "<br><h1>Power</h1>\n");
  //WebFormText(client, "Mains Voltage", "voltage", settings.voltage, true);
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
  WebPrintf(client, "<html><head><title>PsychoPlug Status</title>" ENCODING "</head>\n");
  WebPrintf(client, "<body>\n");
  WebPrintf(client, "Current Time: %s<br>\n", AscTime(now(), settings.use12hr, settings.usedmy, buff, sizeof(buff)));
  WebPrintf(client, "Power: %s <a href=\"%s\">Toggle</a><br>\n",curPower?"OFF":"ON", curPower?"on.html":"off.html");
//  WebPrintf(client, "Current: %dmA (%dW @ %dV)<br>\n", GetCurrentMA(), (GetCurrentMA()* settings.voltage) / 1000, settings.voltage);

  WebPrintf(client, "<table border=\"1px\">\n");
  WebPrintf(client, "<tr><th>#</th><th>Sun</th><th>Mon</th><th>Tue</th><th>Wed</th><th>Thu</th><th>Fri</th><th>Sat</th><th>Time</th><th>Action</th><th>EDIT</th></tr>\n");
  for (byte i=0; i<MAXEVENTS; i++) {
    WebPrintf(client, "<tr><td>%d.</td>", i+1);
    for (byte j=0; j<7; j++) {
      WebPrintf(client, "<td>%s</td>", (settings.event[i].dayMask & (1<<j))?"[X]":"[&nbsp;]");
    }
    if (settings.use12hr) {
      WebPrintf(client, "<td>%d:%02d %s</td>", (settings.event[i].hour)?settings.event[i].hour%12:12, settings.event[i].minute, (settings.event[i].hour<12)?"AM":"PM");
    } else {
      WebPrintf(client, "<td>%d:%02d</td>", settings.event[i].hour, settings.event[i].minute);
    }
    WebPrintf(client, "<td>%s</td>", actionString[settings.event[i].action]);
    WebPrintf(client, "<td><a href=\"edit.html?id=%d\">Edit</a></td></tr>\r\n", i);
  }
  WebPrintf(client, "</table><br>\n");
  WebPrintf(client, "<a href=\"reconfig.html\">Change System Configuration</a><br>\n");
  WebPrintf(client, "</body>\n");
}

// Edit Rule
void SendEditHTML(WiFiClient *client, int id)
{
  WebHeaders(client, NULL);
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
    WebPrintf(client, "<td><input type=\"checkbox\" id=\"%c\" name=\"%c\" %s></td>\n", 'a'+j, 'a'+j, settings.event[id].dayMask & (1<<j)?" checked":"");
  }
    WebPrintf(client, "<td><select name=\"hr\">");
  if (settings.use12hr) {
    for (int j=0; j<12; j++) {
      if (settings.event[id].hour) WebPrintf(client, "<option %s>%d</option>", (settings.event[id].hour%12)==j?"selected":"", j)
      else WebPrintf(client, "<option %s>%d</option>", j==0?"selected":"", j+1);
    }
  } else {
    for (int j=0; j<24; j++) {
      WebPrintf(client, "<option %s>%d</option>", settings.event[id].hour==j?"selected":"", j)
    }
  }
  WebPrintf(client, "</select>:<select name=\"mn\">");
  for (int j=0; j<60; j++) WebPrintf(client, "<option %s>%02d</option>", (settings.event[id].minute==j)?"selected":"", j);
  WebPrintf(client, "</select> ");
  if (settings.use12hr)
    WebPrintf(client, "<select name=\"ampm\"><option %s>AM</option><option %s>PM</option></select></td>", settings.event[id].hour<12?"selected":"", settings.event[id].hour>=12?"selected":"");
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
  WebPrintf(client, "<html><head><title>Success</title>" ENCODING "</head><body><h1><a href=\"index.html\">Success.  Click here if not auto-refreshed</a></h1></body></html>\n");
}


void setup()
{
  Serial.begin(115200);
  Serial.flush();
  delay(100);
  LogPrintf("Starting up...\n");
  delay(10);

  LogPrintf("Settings size=%d\n", sizeof(settings));
  
  StartButton();
  StartLED();
  StartPowerMonitor();
  
  LogPrintf("Loading Settings\n");
  
  bool ok = LoadSettings(RawButton());
  StartRelay(settings.onAfterPFail?true:false);

  // Load our certificate and key from FLASH before we start
  https.setRSAKey(rsakey, sizeof(rsakey));
  https.setX509Cert(x509, sizeof(x509));

  if (ok) {
    isSetup = true;
    StartSTA();
  } else {
    isSetup = false;
    StartSetupAP();
  }

  LogPrintf("End setup()\n");
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
  settings.use12hr = false;
  settings.usedmy = false;
  
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
//    int v;
//    ParamInt("voltage", v);
//    if ((v<80) || (v>255)) v=120; // Sanity-check
//    settings.voltage = v;
    ParamCheckbox("mqttssl", settings.mqttSSL);
    ParamText("mqtttopic", settings.mqttTopic);
    ParamText("mqttclientid", settings.mqttClientID);
    ParamText("mqttuser", settings.mqttUser);
    ParamText("mqttpass", settings.mqttPass);

    ParamText("uiuser", settings.uiUser);
    char tempPass[64];
    ParamText("uipass", tempPass);
    if (strcmp_P(tempPass, PSTR("*****"))) {
      // Was changed, regenerate salt and store it
      HashPassword(tempPass); // This will set settings.uiPassEnc
    }
    memset(tempPass, 0, sizeof(tempPass)); // I think I'm paranoid
  }
}

void SendRebootHTML(WiFiClient *client)
{
  WebHeaders(client, NULL);
  WebPrintf(client, "<html><head><title>Setting Configuration</title>" ENCODING "</head><body>\n");
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
  WebPrintf(client, "<html><head><title>Resetting PsychoPlug</title>" ENCODING "</head><body>\n");
  WebPrintf(client, "<h1>Resetting the plug, please manually reconnect in 5 seconds.</h1>");
  WebPrintf(client, "</body>");
}

void Reset()
{
  SaveSettings();
  StopSettings();

  // Will hang if you just did serial upload.  Needs powercycle once after upload to function properly.
  ESP.restart();

  // Should never hit here...
  delay(1000000);
}


void HandleConfigSubmit(WiFiClient *client, char *params)
{
  ParseSetupForm(params);
  SaveSettings();
  SendRebootHTML(client);
  Reset(); // Restarting safer than trying to change wifi/mqtt/etc.
}

void HandleUpdateSubmit(WiFiClient *client, char *params)
{
  char *namePtr;
  char *valPtr;

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
    if (settings.use12hr) {
      if (!strcmp_P(namePtr, PSTR("ampm"))) {
        if (!strcmp_P(valPtr, PSTR("AM"))) ampm=0;
        else ampm=1;
      }
    } else {
       ampm = 0;
    }
    if (!strcmp_P(namePtr, PSTR("action"))) {
      for (int i=0; i<=ACTION_MAX; i++)
        if (!strcmp(valPtr, actionString[i])) action = i;
    }
    if (namePtr[0]>='a' && namePtr[0]<='g' && namePtr[1]==0) {
      if (!strcmp_P(valPtr, PSTR("on"))) mask |= 1<<(namePtr[0]-'a');
    }
  }
  bool err = false;
  // Check settings are good
  if (id < 0 || id >= MAXEVENTS) err = true;
  if (settings.use12hr) {
    if (hr < 0 || hr > 12) err = true;
  } else {
    if (hr < 0 || hr > 23) err = true;
  }
  if (mn < 0 || mn >= 60) err = true;
  if (ampm < 0 || ampm > 1) err = true;
  if (action < 0 || action > ACTION_MAX ) err = true;
  
  if (hr==12 && ampm==0 && settings.use12hr) { hr = 0; }
  if (err) {
    WebError(client, 400, NULL);
  } else {
    // Update the entry, send refresh page to send back to index
    settings.event[id].dayMask = mask;
    settings.event[id].hour = hr + 12 * ampm; // !use12hr => ampm=0, so safe
    settings.event[id].minute = mn;
    settings.event[id].action = action;
    SaveSettings(); // Store in flash
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



void loop()
{
  static uint16_t cnt = 0;
  if (!cnt) LogPrintf("HeapFree=%d\n", ESP.getFreeHeap());
  cnt = (cnt+1) % 2048;
  
  // Let the button toggle the relay always
  ManageButton();

  // Blink the LED appropriate to the state
  ManageLED(isSetup ? LED_CONNECTED :  LED_AWAITSETUP);
  
  char *url;
  char *params;
  char *namePtr;
  char *valPtr;

  // Any HTTP request, send it to https:// on our IP
  WiFiClient redir = redirector.available();
  if (redir) {
    if (WebReadRequest(&redir, &url, &params, false)) {
      char newLoc[64];
      IPAddress ip = WiFi.localIP();
      if (!isSetup) ip = { 192, 168, 4, 1 };
      snprintf_P(newLoc, sizeof(newLoc), PSTR("Location: https://%d.%d.%d.%d/%s"), ip[0], ip[1], ip[2], ip[3], url[0]?url:"index.html");
      WebError(&redir, 301, newLoc);
      redir.stop();
    }
  } else if (!isSetup) {
    WiFiClientSecure client = https.available();
    if (!client) return;
    
    if (WebReadRequest(&client, &url, &params, false)) {
      if (IsIndexHTML(url)) {
        SendSetupHTML(&client);
      } else if (!strcmp_P(url, PSTR("config.html")) && *params) {
        HandleConfigSubmit(&client, params);
      } else {
        WebError(&client, 404, NULL);
      }
    } else {
      WebError(&client, 400, NULL);
    }
    client.stop();
  } else {
    ManageMQTT();
    ManageSchedule();
    ManagePowerMonitor();

    // Need to stop any mqtt connections during the web processing, only one SSL context available
    PauseMQTT(true);
    
    WiFiClientSecure client = https.available();
    if (client) {
      if (WebReadRequest(&client, &url, &params, true)) {
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
        } else {
          WebError(&client, 404, NULL);
        }
      }
      client.stop();
    }
    PauseMQTT(false);

  }
}

