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
#include <WiFiUdp.h>
#include <lwip/def.h>
#include "dns.h"
#include "log.h"

// Only active during setup to redirect everyone to the setup page
static WiFiUDP dns;
static IPAddress apIP;

#define DNS_QR_QUERY 0
#define DNS_QR_RESPONSE 1
#define DNS_OPCODE_QUERY 0

typedef struct DNSHeader
{
  uint16_t ID;               // identification number
  unsigned char RD : 1;      // recursion desired
  unsigned char TC : 1;      // truncated message
  unsigned char AA : 1;      // authoritive answer
  unsigned char OPCode : 4;  // message_type
  unsigned char QR : 1;      // query/response flag
  unsigned char RCode : 4;   // response code
  unsigned char Z : 3;       // its z! reserved
  unsigned char RA : 1;      // recursion available
  uint16_t QDCount;          // number of question entries
  uint16_t ANCount;          // number of answer entries
  uint16_t NSCount;          // number of authority entries
  uint16_t ARCount;          // number of resource entries
} DNSHeader;


void StartDNS(IPAddress *ip)
{
  apIP = *ip;
  dns.begin(53);
}


static void DumpDNSHeader(DNSHeader *h)
{
  LogPrintf("ID = %04x\n", h->ID);
  LogPrintf("RD = %d\n", h->RD);
  LogPrintf("TC = %d\n", h->TC);
  LogPrintf("AA = %d\n", h->AA);
  LogPrintf("OPCode = %d\n", h->OPCode);
  LogPrintf("QR = %d\n", h->QR);
  LogPrintf("RCode = %d\n", h->RCode);
  LogPrintf("Z = %d\n", h->Z);
  LogPrintf("RA = %d\n", h->RA);
  LogPrintf("QDCount = %d\n", ntohs(h->QDCount));
  LogPrintf("ANCount = %d\n", ntohs(h->ANCount));
  LogPrintf("NSCount = %d\n", ntohs(h->NSCount));
  LogPrintf("ARCount = %d\n", ntohs(h->ARCount));
}

static void ReplyWithIP(DNSHeader *hdr, unsigned char *buff, int len)
{
  if (buff== NULL) return;
  hdr->QR = DNS_QR_RESPONSE;
  hdr->ANCount = hdr->QDCount;
  hdr->RA = 0;  

  dns.beginPacket(dns.remoteIP(), dns.remotePort());
  dns.write(buff, len);

  dns.write((uint8_t)192); //  answer name is a pointer
  dns.write((uint8_t)12);  // pointer to offset at 0x00c

  dns.write((uint8_t)0);   // 0x0001  answer is type A query (host address)
  dns.write((uint8_t)1);

  dns.write((uint8_t)0);   //0x0001 answer is class IN (internet address)
  dns.write((uint8_t)1);

  uint32_t ttl = htonl(600);
  dns.write((unsigned char*)&ttl, 4);

  // Length of RData is 4 bytes (because, in this case, RData is IPv4)
  dns.write((uint8_t)0);
  dns.write((uint8_t)4);
  dns.write(apIP[0]);
  dns.write(apIP[1]);
  dns.write(apIP[2]);
  dns.write(apIP[3]);
  dns.endPacket();
}


void ManageDNS()
{
  int size = dns.parsePacket();
  if (size) {
    unsigned char *pkt = (unsigned char *)alloca(size);
    dns.read(pkt, size);
    struct DNSHeader *hdr = (struct DNSHeader *)pkt;
    if (hdr->QR == DNS_QR_QUERY && hdr->OPCode ==  DNS_OPCODE_QUERY) {
      if (ntohs(hdr->QDCount) == 1 && hdr->ANCount == 0 && hdr->NSCount == 0 && hdr->ARCount == 0) {
        LogPrintf("DNS: Single query for '");
        unsigned char *p = pkt+12;
        while (*p) {
          uint8_t cnt = *(p++);
          for (uint8_t i=0; i<cnt; i++) LogPrintf("%c", *(p++));
          if (*p) LogPrintf(".");
        }
        LogPrintf("', replying with my IP\n");
        ReplyWithIP(hdr, pkt, size);
      } else {
        LogPrintf("DNS: not parsed\n");
        
      }
    }
    
  }
}

