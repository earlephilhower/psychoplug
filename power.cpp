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


#include <Arduino.h>
#include <Wire.h>
#include "psychoplug.h"
#include "settings.h"
#include "mqtt.h"

// Noisy current measurement, better than nothing I guess
int lastCurrentMa = 0;
const int PIN_SDA = 12;
const int PIN_SCL = 0;


static void ReadPowerMonitor();


void StartPowerMonitor()
{
  // Power Monitoring
  pinMode(PIN_SDA, INPUT_PULLUP);
  Wire.begin(PIN_SDA, PIN_SCL);
}

void ManagePowerMonitor()
{
  static int lastReadMS = 0;
  if (millis() > lastReadMS+10000) {
    lastReadMS = millis();
    ReadPowerMonitor();
    MQTTPublishInt("powerma", lastCurrentMa);
  }
}

void StopPowerMonitor()
{
  lastCurrentMa = 0;
}

static void ReadPowerMonitor()
{
  char d[16];
  uint32_t rawPwr[4];
  byte count = 0;
  int timeout = millis() + 1000;
  Wire.requestFrom(0, 12); //16);
  while (count < 12/*16*/ && millis()<timeout) {
    if (Wire.available()) d[count++] = Wire.read();
    else yield();
  }
  if (millis() >= timeout) {
    rawPwr[0] = rawPwr[1] = rawPwr[2] = rawPwr[3] = 0;
  } else {
//    rawPwr = ((uint32_t)d[0]<<24) | ((uint32_t)d[1]<<16) | ((uint32_t)d[2]<<8) | ((uint32_t)d[3]);
//    rawPwr /= 32; //Lower 5 bits looks like noise
/*    
 *     rawPwr = ((uint32_t)d[0]<<24) | ((uint32_t)d[1]<<16) | ((uint32_t)d[2]<<8) | ((uint32_t)d[3]);
    rawPwr /= 32; //Lower 5 bits looks like noise
 
RawPwr: 0 (0x00000000)
RawPwr: 1 (0x00000001)
RawPwr: 0 (0x00000000)

RawPwr: 161 (0x000000a1)
RawPwr: 169 (0x000000a9)
1.70A
RawPwr: 169 (0x000000a9)
RawPwr: 169 (0x000000a9)
RawPwr: 1104 (0x00000450)
RawPwr: 169 (0x000000a9)
RawPwr: 169 (0x000000a9)
RawPwr: 1104 (0x00000450)
RawPwr: 169 (0x000000a9)
RawPwr: 1104 (0x00000450)
RawPwr: 169 (0x000000a9)
RawPwr: 169 (0x000000a9)
RawPwr: 169 (0x000000a9)
RawPwr: 169 (0x000000a9)
RawPwr: 177 (0x000000b1)
RawPwr: 577 (0x00000241)
2RawPwr: 2244 (0x000008c4)
.25A
2.25A
RawPwr: 2241 (0x000008c1)
RawPwr: 2241 (0x000008c1)
RawPwr: 92 (0x0000005c)
RawPwr: 2241 (0x000008c1)
RawPwr: 2140 (0x0000085c)
RawPwr: 96 (0x00000060)
RawPwr: 2233 (0x000008b9)
RawPwr: 2233 (0x000008b9)
RawPwr: 92 (0x0000005c)
RawPwr: 2232 (0x000008b8)
RawPwr: 2241 (0x000008c1)
RawPwr: 1212 (0x000004bc)
5.64A
RawPwr: 2881 (0x00000b41)
5.6A
RawPwr: 2873 (0x00000b39)
RawPwr: 2873 (0x00000b39)
RawPwr: 2857 (0x00000b29)
RawPwr: 2873 (0x00000b39)
RawPwr: 1436 (0x0000059c)
RawPwr: 2873 (0x00000b39)
RawPwr: 2873 (0x00000b39)
RawPwr: 3520 (0x00000dc0)
7.71A
RawPwr: 6473 (0x00001949)
RawPwr: 6481 (0x00001951)
RawPwr: 6481 (0x00001951)
RawPwr: 6489 (0x00001959)
RawPwr: 6465 (0x00001941)
RawPwr: 6465 (0x00001941)
RawPwr: 6457 (0x00001939)
RawPwr: 6945 (0x00001b21)
RawPwr: 2128 (0x00000850)
9.26A
RawPwr: 6313 (0x000018a9)
9.23A
RawPwr: 6305 (0x000018a1)
RawPwr: 2128 (0x00000850)
RawPwr: 6305 (0x000018a1)
RawPwr: 6305 (0x000018a1)
RawPwr: 8793 (0x00002259)
RawPwr: 11089 (0x00002b51)
12.75A
RawPwr: 11089 (0x00002b51)
RawPwr: 3792 (0x00000ed0)
RawPwr: 11089 (0x00002b51)
RawPwr: 11089 (0x00002b51)
RawPwr: 5544 (0x000015a8)
RawPwr: 11097 (0x00002b59)

 */
    rawPwr[0] = ((uint32_t)d[0]<<24) | ((uint32_t)d[1]<<16) | ((uint32_t)d[2]<<8) | ((uint32_t)d[3]);
    rawPwr[1] = ((uint32_t)d[4]<<24) | ((uint32_t)d[5]<<16) | ((uint32_t)d[6]<<8) | ((uint32_t)d[7]);
    rawPwr[2] = ((uint32_t)d[8]<<24) | ((uint32_t)d[9]<<16) | ((uint32_t)d[10]<<8) | ((uint32_t)d[11]);
    //rawPwr[3] = ((uint32_t)d[12]<<24) | ((uint32_t)d[13]<<16) | ((uint32_t)d[14]<<8) | ((uint32_t)d[15]);
  
/*  
 *   
RawPwr[0]: 0 10898 (0x00002a92)
RawPwr[1]: 1 697237 (0x800aa395)
RawPwr[2]: 1 21 (0x80000015)
RawPwr[3]: 0 167625 (0x00028ec9)
1.7a

RawPwr[0]: 0 5668 (0x00001624)
RawPwr[1]: 0 466219 (0x00071d2b)
RawPwr[2]: 0 4261931 (0x0041082b)
RawPwr[3]: 1 170185 (0x800298c9)

RawPwr[0]: 0 5412 (0x00001524)
RawPwr[1]: 0 219179 (0x0003582b)
RawPwr[2]: 0 4217387 (0x00405a2b)
RawPwr[3]: 0 340114 (0x00053092)

RawPwr[0]: 0 5412 (0x00001524)
RawPwr[1]: 0 476203 (0x0007442b)
RawPwr[2]: 0 4260395 (0x0041022b)
RawPwr[3]: 0 329929 (0x000508c9)

RawPwr[0]: 0 5412 (0x00001524)
RawPwr[1]: 0 471083 (0x0007302b)
RawPwr[2]: 0 4263467 (0x00410e2b)
RawPwr[3]: 0 334994 (0x00051c92)

RawPwr[0]: 0 5412 (0x00001524)
RawPwr[1]: 0 481067 (0x0007572b)
RawPwr[2]: 0 4260907 (0x0041042b)
RawPwr[3]: 0 329874 (0x00050892)

RawPwr[0]: 0 35602 (0x00008b12)
RawPwr[1]: 1 237845 (0x8003a115)
RawPwr[2]: 1 2143765 (0x8020b615)
RawPwr[3]: 0 167369 (0x00028dc9)

RawPwr[0]: 0 5412 (0x00001524)
RawPwr[1]: 0 475947 (0x0007432b)
RawPwr[2]: 0 4220971 (0x0040682b)
RawPwr[3]: 0 334738 (0x00051b92)
144w
RawPwr[0]: 0 2834 (0x00000b12)
RawPwr[1]: 1 240405 (0x8003ab15)
RawPwr[2]: 1 2139925 (0x8020a715)
RawPwr[3]: 0 164969 (0x00028469)
201va
RawPwr[0]: 0 5412 (0x00001524)
RawPwr[1]: 0 475691 (0x0007422b)
RawPwr[2]: 0 4224043 (0x0040742b)
RawPwr[3]: 0 334738 (0x00051b92)
.71pf
RawPwr[0]: 0 5668 (0x00001624)
RawPwr[1]: 0 475947 (0x0007432b)
RawPwr[2]: 0 4211755 (0x0040442b)
RawPwr[3]: 0 334738 (0x00051b92)

RawPwr[0]: 0 5668 (0x00001624)
RawPwr[1]: 0 485931 (0x00076a2b)
RawPwr[2]: 0 4223787 (0x0040732b)
RawPwr[3]: 0 329618 (0x00050792)

RawPwr[0]: 0 34706 (0x00008792)
RawPwr[1]: 1 235413 (0x80039795)
RawPwr[2]: 1 2143509 (0x8020b515)
RawPwr[3]: 0 167369 (0x00028dc9)


12.75RawPwr[0]: 0 356644 (0x00057124)
RawPwr[1]: 0 80171 (0x0001392b)
RawPwr[2]: 0 418347 (0x0006622b)
RawPwr[3]: 0 350098 (0x00055792)
A
12.75A
RawPwr[0]: 0 355108 (0x00056b24)
RawPwr[1]: 0 80277 (0x00013995)
RawPwr[2]: 1 230677 (0x80038515)
RawPwr[3]: 0 177636 (0x0002b5e4)

RawPwr[0]: 0 354852 (0x00056a24)
RawPwr[1]: 0 75051 (0x0001252b)
RawPwr[2]: 0 397611 (0x0006112b)
RawPwr[3]: 0 350098 (0x00055792)
1440w
1440va
RawPwr[0]: 0 354852 (0x00056a24)
RawPwr[1]: 0 75051 (0x0001252b)
RawPwr[2]: 0 408363 (0x00063b2b)
RawPwr[3]: 0 354962 (0x00056a92)
pf1
RawPwr[0]: 0 354852 (0x00056a24)
RawPwr[1]: 0 80277 (0x00013995)
RawPwr[2]: 1 210837 (0x80033795)
RawPwr[3]: 0 177513 (0x0002b569)

RawPwr[0]: 0 354596 (0x00056924)
RawPwr[1]: 0 80171 (0x0001392b)
RawPwr[2]: 0 425515 (0x00067e2b)
RawPwr[3]: 0 360082 (0x00057e92)

RawPwr[0]: 0 348964 (0x00055324)
RawPwr[1]: 0 103061 (0x00019295)
RawPwr[2]: 1 233109 (0x80038e95)
RawPwr[3]: 0 177481 (0x0002b549)

RawPwr[0]: 0 224036 (0x00036b24)
RawPwr[1]: 0 72491 (0x00011b2b)
RawPwr[2]: 0 411435 (0x0006472b)
RawPwr[3]: 0 354962 (0x00056a92)


7.7a
RawPwr[0]: 0 103186 (0x00019312)
RawPwr[1]: 1 38805 (0x80009795)
RawPwr[2]: 1 330261 (0x80050a15)
RawPwr[3]: 0 174921 (0x0002ab49)
888w
888va
RawPwr[0]: 0 206628 (0x00032724)
RawPwr[1]: 0 77611 (0x00012f2b)
RawPwr[2]: 0 672811 (0x000a442b)
RawPwr[3]: 0 344722 (0x00054292)
pf1
RawPwr[0]: 0 206628 (0x00032724)
RawPwr[1]: 0 77611 (0x00012f2b)
RawPwr[2]: 0 683563 (0x000a6e2b)
RawPwr[3]: 0 344722 (0x00054292)

RawPwr[0]: 0 103186 (0x00019312)
RawPwr[1]: 1 38677 (0x80009715)
RawPwr[2]: 1 393674 (0x800601ca)
RawPwr[3]: 1 120114 (0x8001d532)

RawPwr[0]: 0 144164 (0x00023324)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 685355 (0x000a752b)
RawPwr[3]: 0 175076 (0x0002abe4)



5.60a
RawPwr[0]: 0 91428 (0x00016524)
RawPwr[1]: 0 211243 (0x0003392b)
RawPwr[2]: 0 1267499 (0x0013572b)
RawPwr[3]: 0 344722 (0x00054292)
461w
656va

RawPwr[0]: 0 91428 (0x00016524)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 1325099 (0x0014382b)
RawPwr[3]: 0 334738 (0x00051b92)
.70pf

RawPwr[0]: 0 91428 (0x00016524)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 1314859 (0x0014102b)
RawPwr[3]: 0 339602 (0x00052e92)

RawPwr[0]: 0 45714 (0x0000b292)
RawPwr[1]: 1 77717 (0x80012f95)
RawPwr[2]: 1 638485 (0x8009be15)
RawPwr[3]: 0 169956 (0x000297e4)

RawPwr[0]: 0 91684 (0x00016624)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 1262123 (0x0013422b)
RawPwr[3]: 0 339602 (0x00052e92)

RawPwr[0]: 0 91684 (0x00016624)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 1263915 (0x0013492b)
RawPwr[3]: 0 339602 (0x00052e92)

RawPwr[0]: 0 91684 (0x00016624)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 1325355 (0x0014392b)
RawPwr[3]: 0 339858 (0x00052f92)

RawPwr[0]: 0 45842 (0x0000b312)
RawPwr[1]: 1 75413 (0x80012695)
RawPwr[2]: 1 635157 (0x8009b115)
RawPwr[3]: 0 167273 (0x00028d69)

RawPwr[0]: 0 91428 (0x00016524)
RawPwr[1]: 0 216107 (0x00034c2b)
RawPwr[2]: 0 1323307 (0x0014312b)
RawPwr[3]: 0 334482 (0x00051a92)

RawPwr[0]: 0 25106 (0x00006212)
RawPwr[1]: 1 70165 (0x80011215)
RawPwr[2]: 1 632469 (0x8009a695)
RawPwr[3]: 0 167268 (0x00028d64)


1.69a
143wRawPwr[0]: 0 5668 (0x00001624)
RawPwr[1]: 0 205867 (0x0003242b)
RawPwr[2]: 0 4195883 (0x0040062b)
RawPwr[3]: 0 334738 (0x00051b92)

201va
143w
RawPwr[0]: 0 5668 (0x00001624)
RawPwr[1]: 0 201003 (0x0003112b)
RawPwr[2]: 0 4198187 (0x00400f2b)
RawPwr[3]: 0 329618 (0x00050792)
.71pf
RawPwr[0]: 0 5668 (0x00001624)
RawPwr[1]: 0 206123 (0x0003252b)
RawPwr[2]: 0 4211755 (0x0040442b)
RawPwr[3]: 0 169828 (0x00029764)

RawPwr[0]: 0 26404 (0x00006724)
RawPwr[1]: 0 205867 (0x0003242b)
RawPwr[2]: 0 4203029 (0x00402215)
RawPwr[3]: 0 167295 (0x00028d7f)

RawPwr[0]: 0 292 (0x00000124)
RawPwr[1]: 0 148011 (0x0002422b)
RawPwr[2]: 0 43 (0x0000002b)
RawPwr[3]: 0 334490 (0x00051a9a)

 */
  
  
  }

//  LogPrintf("RawPwr: %d (0x%08x)\n", rawPwr, rawPwr);

//for (int i=0; i<4; i++) { LogPrintf("RawPwr[%d]: %d %d (0x%08x)\n", i, rawPwr[i]&0x80000000?1:0, rawPwr[i]&0x7fffffff, rawPwr[i]); }
//  LogPrintf("\nRawPwr[0]: %d (0x%08x)\n", rawPwr[0],rawPwr[0]);
//  LogPrintf("RawPwr[1]: %d (0x%08x)\n", rawPwr[1],rawPwr[1]);
//  LogPrintf("RawPwr[2]: %d (0x%08x)\n", rawPwr[2],rawPwr[2]);
//  LogPrintf("RawPwr[3]: %d (0x%08x)\n", rawPwr[3],rawPwr[3]);

  // Current measurement via curve-fit from samples.  YMMV, but fits well for me
  // Needs Vin to get power (well, and PF, but that's more than we can measure)
  double x, y;
  x = (double) rawPwr[0];
  y = 29125.43749 * x / (x + 103257.9289) - 12813.31867 * x / (x + 48884.1186); 
  if (y<0) y=0;
//y = rawPwr[0];
  lastCurrentMa = (int)y;
}

