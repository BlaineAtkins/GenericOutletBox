<h2>Description</h2>
This is a multipurpouse smart outlet which can be switched by an onboard button, a radio signal, or an MQTT command. You can select your own MQTT topic and radio address so that it can be switched from your website or other DIY IOT devices. Connect it to other devices to switch it using motion, light sensors, time, auditory cues, current draw, etc. Download the GERBER files to manufacture PCBs for easy assembly. You can get 5 boards for $2 from JLCPCB.com plus shipping.

<h2>Assembly</h2>
See parts list at the bottom of this document.
<ul>
<li>For space efficiency, the ESP8266 is mounted above the relay. This can be accomplished using the risers that come with many of the boards.</li>
<li>There are two optional spots for capacitors. They are only needed if you are experiancing issues with the radio or current sensor.</li>
<li>A wiring diagram is included in the ino sketch file. You can also order PCBs using the GERBER files so you only have to mount the components</li>
<li>AC power in goes to the pads labled "L" (live) and "N" (neutral). N_out and L_out go to the outlet's live and neutral.</li>
</ul>

<h2>Initial Setup</h2>
When you use a module for the first time, flash the sketch initialEEPROMFlash.ino and let it run once before loading the full sketch. This will configure your EEPROM with valid values, otherwise it may crash when you try to use it.

<h2>Indicator LED</h2>
<ul>
<li>Green -- Connected to a wifi network and the MQTT broker</li>
<li>Orange -- In configuration mode. Look for a Smart Outlet wifi network on your computer or phone. Portal will time out after 1 minute of no connections. See Configuration section for more details.</li>
<li>Blinking Orange & Green -- Connected to wifi & MQTT broker, while also broadcasting a configuration network.</li>
<li>Red -- Busy and cannot receive commands (likely booting, or lost connection to MQTT broker and is trying to reconnect)</li>
<li>Off -- Not connected to wifi (radio and button will still work)</li>
<li>Flashing red -- Overcurrent event occored. Unplug the device which is too high power, then reset the outlet by unplugging it and plugging it back in. See Rated Current section for more details.</li>
</ul>

<h2>Use</h2>
<h3>Switching the outlet</h3>
You can switch the outlet by pressing the button on the side, or sending a radio or MQTT command of "1","0", or "t" for on/off/toggle respectively. To set the radio address and MQTT Topic, see the next section on configuration.

<h3>Configuration</h3>
The Configuration Portal allows you to select a wifi network to automatically connect to, as well as setting 10 different custom parameters (including radio address, MQTT topics, and automatic on/off settings). The portal is active if the status LED is orange. The portal launches automatically for 1 minute if no known network is found on boot, or can be manually launched by holding down the on-board button.
<br>To access the portal, look for a wifi network on your computer or phone named something like "multipurpouse smart outlet". If it does not automatically launch the captive portal upon connection, go to http://192.168.4.1. WiFi and custom parameters can be set by clicking "Configure Wifi" and pressing save when done.

<h2>Rated Current</h2>
Due to the restricted width of the traces carrying the AC power, the board is only rated for 5A. Therefore, there are two conditions which will trigger an overcurrrent event.
<br>1) The current has averaged above 5A for the last 5 seconds
<br>2) The current has been above 8A for the last 1 second
<br>In an overcurrent event, the outlet will switch itself off and blink the indicator LED red rapidly. The outlet is disabled until unplugged and plugged back in.

However, the onboard relay is only rated for 10A. If you draw more than 10A, the relay may fuse and be unable to break the current. Therefore, it is recommended to additionally fuse the outlet at 10A using <a href="https://www.amazon.com/dp/B0BF9LDW1P?psc=1&ref=ppx_yo2ov_dt_b_product_details">something like this</a>.

<h2>Parts List:</h2>
<ol>
<li>ACS720 current sensor -- 15A version</li>
<li>ESP8266 D1 Mini</li>
<li>SRD05VDC relay</li>
<li>5VDC PSU module, at least 500mA -- <a href="https://www.amazon.com/gp/product/B093GW6SZ1/ref=ppx_yo_dt_b_search_asin_title?ie=UTF8&psc=1">I used this one</a></li>
<li>RGB common-cathode LED</li>
<li>NRF24 module</li>
<li>one 2N2222 transistor or similar</li>
<li>three 470 ohm resistors</li>
<li>any diode (1N4007 works)</li>
<li>m2 bolts for mounting</li>
</ol>
