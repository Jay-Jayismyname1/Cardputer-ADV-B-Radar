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

    constexpr float DEFAULT_PROXIMITY_ALERT_KM = 8.0f;
    constexpr float DEFAULT_PROXIMITY_ALERT_ALT_FT = 5000.0f;
    constexpr uint32_t ALERT_RETRIGGER_COOLDOWN_MS = 30000;
    constexpr uint16_t ALERT_TONE_HZ = 2400;
    constexpr uint16_t ALERT_TONE_MS = 180;

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