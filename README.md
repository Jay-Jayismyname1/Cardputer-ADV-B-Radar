<img width="6804" height="4390" alt="20260804_110256" src="https://github.com/user-attachments/assets/b7baaccf-47d1-4f10-a167-03332592544b" />
# Cardputer-ADV-B-Radar
A flight tracking firmware for the Cardputer ADV, with proximity alerts

No GPS or external modules are required, this works on Wifi with the adsb.fi API, external modules can be used for precise tracking but they have not been tested due to lack of equipment

Controls:
Tab - Cycles between flights on radar to view information

Del - Opens up settings page to configure Wi-fi, GPS, Display brightness, LED Brightness (for Proximity alerts), Volume for tone beeps and proximity alerts

Esc - Exits settings

Space - Cycles between radar distance scales (10km, 25km, 50km, 100km)

Fn + arrow keys - Cycles between wifi networks

Fn + Esc - Exits Wifi setup

Features:
Selecting flight blips will show Flight number, Flight reg, Airline, distance from flight (in km), Altitude, Verticle speed (VS), Heading (HDG), Airplane type and estimated seats (Estimated souls on board)

Configuration:
On first boot, a folder will be created called "adsb_radar" in the root of your SD Card, to store aircraft types and airlines, in .csv files, these can be edited to add more to support other regions, but note that these are cached on boot so be mindful of file sizes, once updated, and uploaded onto the folder, these will automatically be loaded on boot, along with wifi credentials (while not secure) they are also saved onto SD card for convenience.

Scanning for wifi networks might cause a hang, just reset and try again, it should work

NeoPixel LED flash meaning:
White flash - This means the ADV is fetching updates from the adsb.fi API
Green flash - By default this is just a cosmetic feature when the heading is 0 degrees
Green, Amber, Yellow, Blue - Proximity alerts, this indicates that a plane is nearby and as the flash progresses, the plane is approaching visibility

INSTALLATION:
To install, there is a downloadable .bin in the release section of this repository, or an even easier approach would be to download and flash from M5Burner or LaucnherHUB on Launcher on your ADV, by searching for "ADV-S Flight Radar" (I am aware that it's ADS-B, but I wanted to try some wordplay with the ADV and ADS, and mixed up the S and B in the process).

Or alternatively you can manually compile the .zip file if you would like to make changes to the code...
  Requirements:
    VSCode
    PlatformIO
Download and extract the .zip file, open VSCode and click on the platformIO extension, and "Pick a folder", browse your directory until you find the folder and open, look on the bottom left of VSCode until you find a terminal icon and click on it and type "pio run -e m5stack-stamps3" to build the project, navigate to .pio --> build --> m5stack-stamps3 and you will see a file called "firmware.bin" flash it or put it on your SD card.

Thank you for the support.
There's a Ko-Fi link if you'd like to donate towards a LoRa cap
