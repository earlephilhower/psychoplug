#PsychoPlug- ESP8266 outlet firmware with standalone HTTP scheduler and MQTT integration

This is a replacement firmware for ESP8266-based WIFI controlled outlets.  Built-in NTP driven event management means no MQTT server is needed (but is fully supported).  No cloud connection is required, only NTP access required to ensure accurate timekeeping.


##DISCLAIMER and WARNINGS
Use of this software and procedure are completely at your own risk.  This project involves working on devices that control household current.  At no time should the remote control outlet be connected to an outlet/power source while either (a) opened, which would possibly expose you to *lethal* voltages, or (b) connected to your computer, which could do *fatal damage* to your computer and attached devices.  Perform any disassembly and reassembly with great care ensuring no spare parts or wires are left inside the plug after your intial upload.  IF YOU HAVE ANY DOUBT ABOUT PERFORMING THESE OPERATIONS PLEASE DO NOT CONTINUE.

DO NOT NAT-FORWARD THIS TO THE PUBLIC INTERNET! While I have strived to make this a robust and safe web interface, there is always the possibilty that there are bugs known or unknown which might allow someone other than yourself to control the outlet without authorization.  Even without any bugs, the HTTP basic authentication used provides a *cleartext username and password* combination for anyone on the Internet to see.  Unfortunately there does not yet exist an Arduino ESP2866 SSL server, which would securely encrypt this username and password.  If you've got working code for one in the Arduino toolchain, drop me a line, please!

INSTEAD, use a VPN into your house or a web-based, SSL encrypted MQTT broker to control things from outside your home.  Either way does not require any holes in your NAT firewall on inbound connections, and will fully encrypt any username and passwords send over the public Internet.


##Key features
Here's a summary of features and operating instructions.  They're thin now, but probably more detailed than the ones you got in the box with your plug.

* Complete replacement for stock firmware on many kinds of ESP8266-based plugs (I'm using WorkChoice EcoPlugs from Walmart)
* No cloud connection required to work (NTP needed for timekeeping)
* NTP timekeeping
* Built-in events, up to 24 per day, configurable on a per-minute and day basis, no controller needed
* Integrates with home automation systems (WIP) using MQTT publishing and subscribing
* Web-based configuration and control with password access
* Power monitoring and email alerting (i.e. detect appliance trouble) ((TBD)
* Power button always works, even in unconnected state

##Prerequisites
* Ensure you have the Arduino ESP8266 IDE installed.  Note that for Ubuntu the Arduino IDE is *very* old and you'll need to install from http://arduino.cc to get access to the ESP8266 toolchain.
* Install the library "MQTT by Joel Gaehwiler" from the Arduino library manager or from https://github.com/256dpi/arduino-mqtt/
* Install the library "TimeLib by Paul Stoffregen" (https://github.com/PaulStoffregen/Time) manually
* Select your model and flash size (normally GenericESP8266 and 512K, no SPIFFS)

##Connecting the plug to your computer
![Programmer Connections](connections.jpg  "Programmer Connections")
You can follow the connections and directions given in [2]  to upload the image, but if you have a USB to Serial  adapter that provides both +5V and +3.3V outputs then you can actually use tha above image as a guide and only have to solder on 3 wires, not 5 (and for me it was a pain soldering the wires to the frame so this was a big win).
0. UNPLUG YOUR USB adaptor!
1. Connect the ground from your USB adapter
2. Connect The USB +5V output to the voltage regulator input.
3. Connect the USB TX to the 4th pin from the right as shown in the picture (this is the ESP8266 RX pin)
4. Connect the USB RX to the 5th pin from the right as shown (this is the ESP8266 TX pin)
5. Connect GPIO (the 1st pin from the right) to the ground ring.  You can just hold it to the ring or use a clip.
6. Plug in the USB adaptor to the PC
7. Push the pushbutton to enable the bootloader.  The red LED will light up (D8 in the image) and you'll hear a satisfying click as the relay engages.

At this point you should be able to upload images using the Arduino IDE.


##Upload instructions:
* DO NOT OPERATE WHILE PLUG IS POWERED FROM AN AC CIRCUIT!!!  UNPLUG AND WORK ON YOUR BENCH UNTIL INITIAL UPLOAD AND TESTS COMPLETED AND THE OUTLET COMPLETELY REASSEMBLED!!!
* Disassemble the plug and get access to the ESP8266 control board.
* Tack wires as discussed in [2] and hook to your USB/Serial adapter.
* Build and flash to your plug, it will reboot.


##Initial configuration:

* Connect your phone or laptop WIFI to the plug's configuration access point (PSYCHOPLUG-XXX)
* Enter your home's SSID/PSK and other network configurations
* Plug will report success and reboot, but because you've just doen a serial upload will hang in the bootloader
* Power cycle the plug (i.e. unplug the USB serial cable then re-plug it in to your computer) to ensure a clean reboot

The web interface is simple and text based and should be easy to use on any smartphone or tablet if a laptop with WiFi isn't available.  It even works in "links," the text-mode web browser.
![Setup Web Page](setup.png  "Setup Web Page")

##Schedule setup

* Use HTTP://plugname/ to configure schedules, check state, etc.
* You may need to consult your WiFi AP to find the plug IP (or use a static IP in configuration above) if there are DNS issues with your AP.
* Verify the time looks good on the initial web page, that the relay is working (toggle off and on from the main page)
![Main Schedule](schedule.png  "Main Schedule Web Page")


## Event configuration
* Be sure to check one or more days of the week are selected, and that the Action is not "None" to ensure the event actually triggers
* Actions Off and On are self-explanatory.  Toggle will toggle from whatever the current state is when the event trigger.  Pulse Off turns power off for 1/2 second then turns it back on.  Pulse On does the opposite.
![Editing a Rule](editrule.png  "Editing a Rule")

## Reassembly
* Unplug the USB to serial connector to remove power from the exposed outlet.
* Carefully and cleanly remove any wires attached to perform the upload.
* Double check for any possible shorts or exposed wires before closing.
* Carefully reassemble the plug in the reverse order of disassembly.
* Plug into it's final resting spot.  The web interface will become available as soon as the outlet reconnects to your AP.

##LED Status:
* Red LED lit = power to the controlled outler
* Blue LED fast blink = Setup mode, connect with your phone/laptop to PSYCHOPLUG-XXX and configure
* Blue LED slow blink = Attempting to connect to the WIFI AP specified
* Blue LED blink once every ~1.5 seconds = Operating mode, normal operation
If the outlet is unable to connect to the AP it will continually retry.  Should it not connect, you can unplug the outlet and get back into setup mode by holding down the button while re-plugging it in and connecting to its configuration AP.

##Network Reconfiguration
* After initial setup, the control web page also has a "Change Configuration" capability
* To reconfigure in case of lost password you can hold the power button down while plugging in the switch.

##Web CGI control:
* Simple web applications can be used to control the state of the outlet (be sure to use authentication!):
* wget --user=username --password=mypass "http://..../off.html"
* wget --user=username --password=mypass "http://..../on.html"

##MQTT:
* SSL and unencrypted MQTT connections are supported.  Be sure to use the correct port (1883 = no SSL, 8883 = SSL)

###MQTT topics published:
.../button (press,release) => When the button is physically pressed or released on the plug
.../powerstate (0,1) => When the controlled appliance is turned off or on
.../scheduledevent => When an event fires, records the event type in text (Off, On, Toggle, Pulse Low, Pulse High)
.../powerma => Current in mA reported every 10 seconds.  Very noisy, don't put too much faith in it in my experience.

###MQTT topics subscribed:
.../remotepower (0,1) => Turn the power off or on remotely

*Note that both the schedule and MQTT are operating in parallel.  So if you have a schedule that says "turn off @ 7:00pm" and you publish a .../remotepower=1 event at 6:59pm the outlet will be on for 1 minute and then turn back off at thescheduled time.  No schedules are required for full MQTT control.*

##UDP logging:
A basic UDP logging mechanism is included (mostly for code development).  When a UDP Log Server is entered in the configuration page, the outlet will log information that would normally go to the serial port to UDP-LOG-IP:9911.  This allows for safe debugging of code while the outlet is plugged in and operating.

Under Linux, simply use a NetCat instance to watch this log (it is NOT in syslog format!):
	nc -l -u 9911
If no IP is specified (IP=0.0.0.0), the serial port will be used (i.e. for desktop debugging)

##References and many thanks:
While the code in this repository is my own work, it wouldn't have been possible without the great hacking work done by Scott Gibson [1] and others in uncovering the GPIO and control points for these kinds of switches.
[1] http://thegreatgeekery.blogspot.ca/2016/02/ecoplug-wifi-switch-hacking.html
[2] http://www.hagensieker.com/blog/page/?post_id=44&title=esp8266-hack-of-inexpensive-wifi-outlet
[3] http://www.esp8266.com/viewtopic.php?f=6&t=8044
