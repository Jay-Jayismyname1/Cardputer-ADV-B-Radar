<img width="3176" height="2080" alt="20260725_025920" src="https://github.com/user-attachments/assets/8102c91b-eb4e-4bf6-9eaf-009a7a0360db" />
# Cardputer-ABS-B-Radar
A flight tracking firmware for the Cardputer ADV, with proximity alerts

No GPS or external modules are required, this works on Wifi with the adsb.fi API by default (switchable in Settings to adsb.lol, airplanes.live, or your own tar1090/readsb feeder), external modules can be used for precise tracking but they have not been tested due to lack of equipment

## Controls
- **Tab** - Cycles between flights on radar to view information
- **Enter** - With a flight selected, opens a full-detail panel for it (adds airline, seat estimate, squawk, heading, bearing, and lat/lon on top of what's already in the HUD); Tab still cycles flights while it's open, Esc returns to the radar
- **Del** - Opens up settings page to configure Wi-fi, Location, Units, Proximity Beep, Key Beep, Radar Rotation, Display brightness, LED Brightness, Volume for tone beeps, and the Flight Logbook
- **Esc** - Exits settings (also backs out of a settings sub-screen one level at a time)
- **Space** - Cycles between radar distance scales (10km, 25km, 50km, 100km)
- **Fn + arrow keys** - Cycles between wifi networks
- **Fn + Esc** - Exits Wifi setup

Scanning for wifi networks might cause a hang, just reset and try again, it should work

### Settings sub-screens
- **Location**: toggle Hardware GPS on/off. While it's on, cycle the GPS RX/TX pin pair and see the live fix (coordinates + satellite count, or "No lock" while searching). While it's off, pick an "Other Source" of IP (coarse location from your internet connection, shown below) or Manual - selecting Manual adds two more rows, Lat and Lon, each independently editable in place. Manual coordinates are stored separately from GPS/IP fixes, so switching to IP and back to Manual never loses what you entered.
- **Units**: distance in km, nautical miles, or statute miles; altitude in feet or meters. Applies to the HUD, the range scale, and the Proximity Beep sub-screen.
- **Proximity Beep**: a max distance and a max height, both using whatever units you picked above - a plane only triggers the beep once it's within *both*. The height filter is barometric altitude (AMSL), not height above ground. Emergency squawks (7500/7600/7700) always beep regardless of these thresholds.
- **Data Source**: cycle between adsb.fi, adsb.lol, airplanes.live, or your own tar1090/readsb server (Custom). Custom needs a host/IP or hostname, port, and HTTP/HTTPS scheme; there's a "Test Connection" action that checks it's actually reachable and looks like a real readsb/tar1090 instance before you switch to it.
- **Key Beep**: on/off toggle for the short UI-feedback tones the keyboard makes while navigating menus and switching between aircraft on the radar screen. Separate from the Proximity Beep alert, which always sounds regardless of this setting.
- **Radar Rotation**: rotates the whole radar display 0-359°, so "up" doesn't have to mean North - useful if you'd rather orient the display toward wherever you're actually facing. Cycle in 15° steps with `,`/`.`, or press `m` on that row to type an exact value directly, same as Location's manual Lat/Lon entry.

In every sub-screen and value-entry screen, toggle/cycle rows (GPS on/off, Other Source, Data Source, Scheme, Units, Beep on/off) respond to both Enter and the `,`/`.` adjust keys; Enter opens a sub-screen or a text/number entry field where that's what the row actually does (e.g. Data Source's Host/Port, or Location's Lat/Lon).

## Features
Selecting flight blips shows a 2-line HUD summary: Flight number, Flight reg, Airplane type and Speed on the first line; distance from flight, Altitude and Verticle speed (VS) on the second (all in your chosen units) - press Enter for the full detail panel (see Controls above), which adds Airline, estimated seats (Estimated souls on board), Squawk, Heading (HDG), Bearing and coordinates.

## Configuration
On first boot, a folder will be created called "adsb_radar" in the root of your SD Card, to store aircraft types and airlines, in .csv files, these can be edited to add more to support other regions, but note that these are cached on boot so be mindful of file sizes, once updated, and uploaded onto the folder, these will automatically be loaded on boot, along with wifi credentials (while not secure) they are also saved onto SD card for convenience.

## NeoPixel LED flash meaning
- White flash - This means the ADV is fetching updates from the adsb.fi API
- Green flash - By default this is just a cosmetic feature when the heading is 0 degrees
- Green, Amber, Yellow, Blue - Proximity zone indicator, this indicates that a plane is nearby and as the flash progresses, the plane is approaching visibility (purely visual - fixed distance bands, independent of your Proximity Beep settings)
- Red (fast flash) - An emergency squawk (7500/7600/7700) is being tracked

The audible proximity beep is separate from the LED and is governed entirely by the Proximity Beep settings (max distance + max height) described above, plus emergency squawks which always beep.

## Installation
To install, there is a downloadable .bin in the release section of this repository, or an even easier approach would be to download and flash from M5Burner or LaucnherHUB on Launcher on your ADV, by searching for "ADV-S Flight Radar" (I am aware that it's ADS-B, but I wanted to try some wordplay with the ADV and ADS, and mixed up the S and B in the process).

Or alternatively you can manually compile the .zip file if you would like to make changes to the code...

**Requirements**
- VSCode
- PlatformIO

Download and extract the .zip file, open VSCode and click on the platformIO extension, and "Pick a folder", browse your directory until you find the folder and open, look on the bottom left of VSCode until you find a terminal icon and click on it and type "pio run -e m5stack-stamps3" to build the project, navigate to .pio --> build --> m5stack-stamps3 and you will see a file called "firmware.bin" flash it or put it on your SD card.

Thank you for the support.
There's a Ko-Fi link if you'd like to donate towards a LoRa cap
