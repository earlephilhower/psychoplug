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
#include <ESP8266WiFi.h>

#include "log.h"
#include "web.h"
#include "password.h"
#include "timezone.h"




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


void WebError(WiFiClient *client, int code, const char *headers, bool usePMEM)
{
  LogPrintf("+WebError: Begin, free=%d\n", ESP.getFreeHeap());
  LogPrintf(" Sending headers...\n");
  WebPrintf(client, "HTTP/1.1 %d\r\n", code);
  WebPrintf(client, "Server: PsychoPlug\r\n");
  WebPrintf(client, "Content-type: text/html\r\n");
  WebPrintf(client, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
  WebPrintf(client, "Pragma: no-cache\r\n");
  WebPrintf(client, "Expires: 0\r\n");
  WebPrintf(client, "Connection: close\r\n");
  LogPrintf("+WebError: Writing error headers: %08x\n", headers);
  if (headers) {
    if (!usePMEM) {
      WebPrintf(client, "%s", headers);
    } else {
      WebPrintfPSTR(client, headers);
    }
  }
  WebPrintf(client, "\r\n\r\n");
  WebPrintf(client, DOCTYPE);
  WebPrintf(client, "<html><head><title>");
  WebPrintError(client, code);
  WebPrintf(client, "</title>" ENCODING "</head>\n");
  WebPrintf(client, "<body><h1>");
  WebPrintError(client, code);
  WebPrintf(client, "</h1></body></html>\r\n");
  LogPrintf("-WebError\n");
}



void WebHeaders(WiFiClient *client, PGM_P /*const char **/headers)
{
  WebPrintf(client, "HTTP/1.1 200 OK\r\n");
  WebPrintf(client, "Server: PsychoPlug\r\n");
  WebPrintf(client, "Content-type: text/html\r\n");
  WebPrintf(client, "Cache-Control: no-cache, no-store, must-revalidate\r\n");
  WebPrintf(client, "Pragma: no-cache\r\n");
  WebPrintf(client, "Connection: close\r\n");
  WebPrintf(client, "Expires: 0\r\n");
  if (headers) {
    WebPrintfPSTR(client, headers);
  }
  WebPrintf(client, "\r\n");
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



// Parse (authenticated) HTTP request, request authentication if not authorized
bool WebReadRequest(WiFiClient *client, char **urlStr, char **paramStr, bool authReq, const char *uiUser, const char *uiSalt, const char *uiPassEnc)
{
  static char NUL = 0; // Get around writable strings...
  char hdrBuff[128];
  char authBuff[128];
  char reqBuff[384];

  *urlStr = NULL;
  *paramStr = NULL;

  LogPrintf("+WebReadRequest @ %d\n", millis());
  unsigned long timeoutMS = millis() + 5000; // Max delay before we timeout
  while (!client->available() && millis() < timeoutMS) { delay(10); }
  if (!client->available()) {
    LogPrintf("-WebReadRequest: Timeout @ %d\n", millis());
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
      strncpy(authBuff, hdrBuff, sizeof(authBuff));
    }
  } while (hlen > 0);

  // Check for no password...
  bool empty = true;
  for (unsigned int i=0; uiSalt!=NULL && i<SALTLEN; i++) if (uiSalt[i]) empty = false;
  if (authReq && uiUser && uiUser[0] && !empty) {
    Base64Decode(authBuff+21);
    char *user = authBuff+21;
    char *pass = user;
    while (*pass && *pass != ':') pass++; // Advance to the : or \0
    if (*pass) { *pass = 0; pass++; } // Skip the :, end the user string
    bool matchUser = !strcmp(user, uiUser);
    bool matchPass = VerifyPassword(pass, uiSalt, uiPassEnc);
    if (!authBuff[0] || !matchUser || !matchPass) {
      LogPrintf("WebReadRequest: Unauthenticated\n");
      WebError(client, 401, PSTR("WWW-Authenticate: Basic realm=\"PsychoPlug\""));
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
  if (!memcmp_P(reqBuff, PSTR("GET "), 4)) {
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
  } else if (!memcmp_P(reqBuff, PSTR("POST "), 5)) {
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
    WebError(client, 405, PSTR("Allow: GET, POST"));
    LogPrintf("-WebReadRequest(): Illegal command\n");
    return false;
  }

  if (urlStr) *urlStr = url;
  if (paramStr) *paramStr = qp;

  LogPrintf("-WebReadRequest(): Success\n");
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





void WebFormText(WiFiClient *client, /*const char **/ PGM_P label, const char *name, const char *value, bool enabled)
{
  WebPrintfPSTR(client, label);
  WebPrintf(client, ": <input type=\"text\" name=\"%s\" id=\"%s\" value=\"%s\" %s><br>\n", name, name, value, !enabled?"disabled":"");
}
void WebFormText(WiFiClient *client, /*const char **/ PGM_P label, const char *name, const int value, bool enabled)
{
  WebPrintfPSTR(client, label);
  WebPrintf(client, ": <input type=\"text\" name=\"%s\" id=\"%s\" value=\"%d\" %s><br>\n", name, name, value, !enabled?"disabled":"");
}
void WebFormCheckbox(WiFiClient *client, /*const char **/ PGM_P label, const char *name, bool checked, bool enabled)
{
  WebPrintf(client, "<input type=\"checkbox\" name=\"%s\" id=\"%s\" %s %s> ", name, name, checked?"checked":"", !enabled?"disabled":"");
  WebPrintfPSTR(client, label);
  WebPrintf(client, "<br>\n");
}
void WebFormCheckboxDisabler(WiFiClient *client, PGM_P /*const char **/label, const char *name, bool invert, bool checked, bool enabled, const char *ids[])
{
  WebPrintf(client,"<input type=\"checkbox\" name=\"%s\" id=\"%s\" onclick=\"", name,name);
  if (invert) WebPrintf(client, "var x = true; if (this.checked) { x = false; }\n")
  else WebPrintf(client, "var x = false; if (this.checked) { x = true; }\n");
  for (byte i=0; ids[i][0]; i++ ) {
    WebPrintf(client, "document.getElementById('%s').disabled = x;\n", ids[i]);
  }
  WebPrintf(client, "\" %s %s> ", checked?"checked":"", !enabled?"disabled":"")
  WebPrintfPSTR(client, label);
  WebPrintf(client, "<br>\n");
}

// We do the sort in the browser because it has more memory than us. :(
void WebTimezonePicker(WiFiClient *client, const char *timezone)
{
  bool reset = true;
  WebPrintf(client, "Timezone: <select name=\"tz\" id=\"tz\">\n");
  char str[64];
  char temp[128];
  char *buff = (char *)alloca(1400); // We'll combine a bunch of TZs into a single packet this way, way faster to send
  if (buff) {
    buff[0] = 0;
    int len = 0;
    while ( GetNextTZ(reset, str, sizeof(str)) ) {
        reset = false;
        snprintf_P(temp, sizeof(temp), PSTR("<option value=\"%s\" %s>%s</option>\n"), str, !strcmp(str, timezone)?"selected":"", str);
        if (len + strlen(temp) > 1399) {
          client->print(buff);
          buff[0] = 0;
          len = 0;
        }
        strcpy(buff+len, temp);
        len += strlen(temp);
    }
    if (len) {
      client->print(buff);
    }
  } else { // Can't allocate buff, fall back to standard way
    while ( GetNextTZ(reset, str, sizeof(str)) ) {
      reset = false;
      WebPrintf(client, "<option value=\"%s\" %s>%s</option>\n", str, !strcmp(str, timezone)?"selected":"", str);
    }
  }
  WebPrintf(client, "</select><br>\n");

  // Javascript to sort alphabetically...
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
  WebPrintf(client, "sortSelect(document.getElementById('tz'));\n");
  WebPrintf(client, "function selectItemByValue(elmnt, value) {\n");
  WebPrintf(client, "  for(var i=0; i < elmnt.options.length; i++) {\n");
  WebPrintf(client, "    if(elmnt.options[i].value === value) { elmnt.selectedIndex = i; break; }\n");
  WebPrintf(client, "  }\n");
  WebPrintf(client, "}\n");
  WebPrintf(client, "setTimeout(function(){selectItemByValue(document.getElementById('tz'), '%s');}, 500);\n", timezone );
  WebPrintf(client, "</script>\n");
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




