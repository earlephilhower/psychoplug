# psychoplug
ESP8266 power outlet replacement firmware with standalone HTTP scheduler and MQTT integration

This is a WIP replacement firmware for ESP8266-based WIFI controlled outlets.
Code is not nearly final, but works through basic testing on the bench for schedules and control.

Key features:
* Complete replacement for stock firmware on many kinds of ESP8266-based plugs
* Emphasis on security (no stored passwords, no "cloud" sync of SSIDs or PSKs, etc.)
* Built-in events, up to 24 per day, configurable on a per-minute and day basis, no controller needed
* Integrates with home automation systems (WIP) using MQTT
* Web-based configuration and control with password access
* Power monitoring and email alerting (i.e. detect appliance trouble) ((TBD)

Instructions:
* Ensure you have the Arduino ESP8266 IDE
* Select your model andflash size (normally GenericESP8266 and 512K, no SPIFFS)
* Build and flash to your plug
* Connect to plug's configuration WIFI access point
* Enter your honme's SSID/PSK and other network configurations
* Plug will reboot and connect to your main WIFI AP
* Use HTTP://<plugname>/ to configure schedules, check state, etc.

Reconfiguration:
* After initial setup, the control web page also has a "Change Configuration" capability
* To reconfigure in case of lost password you can hold the power button down while plugging in the switch.

I'm using "WorkChoice EcoPlugs" with this configuration, but the same hardware is
found in many other systems or home-built switches.

References:
http://thegreatgeekery.blogspot.ca/2016/02/ecoplug-wifi-switch-hacking.html
http://www.hagensieker.com/blog/page/?post_id=44&title=esp8266-hack-of-inexpensive-wifi-outlet
http://www.esp8266.com/viewtopic.php?f=6&t=8044

