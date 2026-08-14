#pragma once
#include <Arduino.h>

namespace Config {
    constexpr const char* IP_GEO_HOST = "ip-api.com";
    constexpr const char* IP_GEO_PATH = "/json/?fields=status,lat,lon";

    struct GpsPinPair { uint8_t rx; uint8_t tx; const char* label; };
    constexpr GpsPinPair GPS_PIN_CANDIDATES[] = {
        {15, 13, "G15/G13 (GPS Cap)"}, {40, 14, "G40/G14"}, {39, 5, "G39/G5"}, {3, 4, "G3/G4"}
    };
    constexpr uint8_t GPS_PIN_CANDIDATE_COUNT = 4;
    constexpr uint32_t GPS_BAUD = 115200;

    constexpr float RANGE_STEPS_KM[] = {10.0f, 25.0f, 50.0f, 100.0f};
    constexpr uint8_t RANGE_STEP_COUNT = 4;
    constexpr uint8_t DEFAULT_RANGE_INDEX = 1;

    constexpr const char* ADSB_API_HOST = "opendata.adsb.fi";
    constexpr uint16_t ADSB_API_PORT = 443;
    constexpr uint32_t FETCH_INTERVAL_MS = 8000;
    constexpr uint32_t HTTP_TIMEOUT_MS = 6000;

    // Two more free, community-run feeders with the identical adsb.fi-style
    // "ac" JSON shape - confirmed live during research. adsb.lol uses the
    // same /v2/lat/{lat}/lon/{lon}/dist/{km} URL shape as adsb.fi; airplanes.live
    // uses a positional /v2/point/{lat}/{lon}/{km} instead. (globe.adsbexchange.com
    // was also tried and explicitly rejected the request - "403 Forbidden by
    // administrative rules" - and ADS-B Exchange's real API is a paid RapidAPI
    // product, so it was deliberately left out rather than worked around.)
    constexpr const char* ADSB_LOL_HOST = "api.adsb.lol";
    constexpr const char* AIRPLANES_LIVE_HOST = "api.airplanes.live";

    // A user-supplied tar1090/readsb instance is a further alternative to
    // all of the above (see AdsbClient::DataSource) - the user's own
    // host:port, either plain HTTP or HTTPS (self-signed certs accepted,
    // same as the hosted APIs above), with no built-in range filtering
    // server-side (unlike the hosted APIs' own radius query), so the app
    // filters by range client-side instead.
    constexpr uint16_t DEFAULT_TAR1090_PORT = 8080;
    constexpr uint8_t TAR1090_HOST_MAX_LEN = 64; // hostname or IP, e.g. "adsb.mydomain.com"
    constexpr const char* TAR1090_AIRCRAFT_PATH = "/data/aircraft.json";
    constexpr const char* TAR1090_RECEIVER_PATH = "/data/receiver.json"; // small, used by the settings "Test Connection" check

    constexpr float DEFAULT_PROXIMITY_ALERT_KM = 8.0f;
    constexpr float DEFAULT_PROXIMITY_ALERT_ALT_FT = 5000.0f;
    constexpr uint32_t ALERT_RETRIGGER_COOLDOWN_MS = 30000;
    constexpr uint16_t ALERT_TONE_HZ = 2400;
    constexpr uint16_t ALERT_TONE_MS = 180;
    // How many aircraft newly in range on one scan can each get their own
    // beep, hard-capped so a busy approach corridor can't turn into a
    // continuous buzz. Beeps are queued and played one per tick (see
    // ProximityAlert::tick()) rather than all at once, so this many tones
    // land ALERT_BEEP_GAP_MS apart instead of overlapping. Bumped +500ms
    // (was 250ms) - back to back beeps only ever happen when there's more
    // than one aircraft queued, and the tighter gap read as one continuous
    // buzz rather than distinct tones per aircraft.
    constexpr uint8_t MAX_ALERT_BEEPS = 5;
    constexpr uint32_t ALERT_BEEP_GAP_MS = 750;

    constexpr uint8_t MAX_TRACKED_AIRCRAFT = 40;

    constexpr const char* SD_ROOT_DIR           = "/adsb_radar";
    constexpr const char* SD_AIRLINES_CSV       = "/adsb_radar/airlines.csv";
    constexpr const char* SD_AIRCRAFT_TYPES_CSV = "/adsb_radar/aircraft_types.csv";
    constexpr const char* SD_LOG_DIR            = "/adsb_radar/logs";
    constexpr const char* SD_SETTINGS_FILE      = "/adsb_radar/settings.json";
    constexpr const char* SD_WIFI_CREDENTIALS_FILE = "/adsb_radar/wifi.txt";

    constexpr uint8_t SD_SPI_CS_PIN   = 12;
    constexpr uint8_t SD_SPI_MOSI_PIN = 14;
    constexpr uint8_t SD_SPI_MISO_PIN = 39;
    constexpr uint8_t SD_SPI_CLK_PIN  = 40;

    constexpr uint8_t NEOPIXEL_PIN   = 21;
    constexpr uint8_t NEOPIXEL_COUNT = 1;
    // Stamp-S3A (Cardputer-ADV) only: independent power-enable switch for
    // the RGB LED rail. Must be driven HIGH before the LED will light.
    constexpr uint8_t NEOPIXEL_POWER_PIN = 38;

    constexpr float ZONE_BLUE_KM   = 25.0f;
    constexpr float ZONE_YELLOW_KM = 10.0f;
    constexpr float ZONE_AMBER_KM  = 5.0f;
    constexpr float ZONE_VISUAL_KM = 2.0f;

    constexpr uint32_t FLASH_INTERVAL_MS = 350;
    constexpr uint32_t NEW_CONTACT_RADIUS_KM = 100;

    constexpr uint16_t COLOR_LOW_ALT_THRESHOLD_FT  = 10000; // Fixed overflow
    constexpr uint16_t COLOR_MID_ALT_THRESHOLD_FT  = 30000;
}