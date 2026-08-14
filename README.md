<img width="3176" height="2080" alt="20260725_025920" src="https://github.com/user-attachments/assets/8102c91b-eb4e-4bf6-9eaf-009a7a0360db" />
# Cardputer-ABS-B-Radar
A flight tracking firmware for the Cardputer ADV, with proximity alerts

No GPS or external modules are required, this works on Wifi with the adsb.fi API, external modules can be used for precise tracking but they have not been tested due to lack of equipment

Controls:
Tab - Cycles between flights on radar to view information

Del - Opens up settings page to configure Wi-fi, Location, Units, Proximity Beep, Display brightness, LED Brightness, Volume for tone beeps, and the Flight Logbook

Esc - Exits settings (also backs out of a settings sub-screen one level at a time)

Space - Cycles between radar distance scales (10km, 25km, 50km, 100km)

Fn + arrow keys - Cycles between wifi networks

Fn + Esc - Exits Wifi setup

Settings sub-screens:
- Location: toggle GPS on/off, cycle the GPS RX/TX pin pair while GPS is on, or (while GPS is off) switch between "IP" (coarse location from your internet connection) and "Manual" (a saved lat/lon you enter yourself, via the `m` key). Manual mode is sticky - once picked, it's remembered across reboots and won't get silently overwritten by an IP lookup.
- Units: distance in km, nautical miles, or statute miles; altitude in feet or meters. Applies to the HUD, the range scale, and the Proximity Beep sub-screen.
- Proximity Beep: a max distance and a max height, both using whatever units you picked above - a plane only triggers the beep once it's within *both*. The height filter is barometric altitude (AMSL), not height above ground. Emergency squawks (7500/7600/7700) always beep regardless of these thresholds.

Features:
Selecting flight blips will show Flight number, Flight reg, Airline, distance from flight (your chosen distance unit), Altitude (your chosen altitude unit), Verticle speed (VS), Heading (HDG), Airplane type and estimated seats (Estimated souls on board)

Configuration:
On first boot, a folder will be created called "adsb_radar" in the root of your SD Card, to store aircraft types and airlines, in .csv files, these can be edited to add more to support other regions, but note that these are cached on boot so be mindful of file sizes, once updated, and uploaded onto the folder, these will automatically be loaded on boot, along with wifi credentials (while not secure) they are also saved onto SD card for convenience.

Scanning for wifi networks might cause a hang, just reset and try again, it should work

NeoPixel LED flash meaning:
White flash - This means the ADV is fetching updates from the adsb.fi API
Green flash - By default this is just a cosmetic feature when the heading is 0 degrees
Green, Amber, Yellow, Blue - Proximity zone indicator, this indicates that a plane is nearby and as the flash progresses, the plane is approaching visibility (purely visual - fixed distance bands, independent of your Proximity Beep settings)
Red (fast flash) - An emergency squawk (7500/7600/7700) is being tracked

The audible proximity beep is separate from the LED and is governed entirely by the Proximity Beep settings (max distance + max height) described above, plus emergency squawks which always beep.

INSTALLATION:
To install, there is a downloadable .bin in the release section of this repository, or an even easier approach would be to download and flash from M5Burner or LaucnherHUB on Launcher on your ADV, by searching for "ADV-S Flight Radar" (I am aware that it's ADS-B, but I wanted to try some wordplay with the ADV and ADS, and mixed up the S and B in the process).

Or alternatively you can manually compile the .zip file if you would like to make changes to the code...
  Requirements:
    VSCode
    PlatformIO
Download and extract the .zip file, open VSCode and click on the platformIO extension, and "Pick a folder", browse your directory until you find the folder and open, look on the bottom left of VSCode until you find a terminal icon and click on it and type "pio run -e m5stack-stamps3" to build the project, navigate to .pio --> build --> m5stack-stamps3 and you will see a file called "firmware.bin" flash it or put it on your SD card.

Thank you for the support.
There's a Ko-Fi link if you'd like to donate towards a LoRa cap
