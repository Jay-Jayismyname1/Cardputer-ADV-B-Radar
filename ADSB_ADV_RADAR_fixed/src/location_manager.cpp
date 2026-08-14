#include "location_manager.h"
#include "config.h"
#include <TinyGPSPlus.h>
#include <HardwareSerial.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>

namespace LocationManager {

namespace {
    Preferences prefs;

    TinyGPSPlus gps;
    HardwareSerial gpsSerial(1);


    bool gpsEnabled = false;
    uint8_t gpsPinIndex = 0;
    bool gpsSerialStarted = false;

    double lastLat = 0, lastLon = 0;
    bool havePersisted = false;

    bool ipLookupDone = false;
    uint32_t lastIpLookupAttemptMs = 0;
    constexpr uint32_t IP_LOOKUP_RETRY_MS = 15000;

    Source source = Source::None;
    SourcePref sourcePref = SourcePref::Auto;

    void startGpsSerialIfNeeded() {
        if (!gpsEnabled || gpsSerialStarted) return;
        const auto& pins = Config::GPS_PIN_CANDIDATES[gpsPinIndex];
        gpsSerial.begin(Config::GPS_BAUD, SERIAL_8N1, pins.rx, pins.tx);
        gpsSerialStarted = true;
    }

    void persistLocation(double lat, double lon) {
        prefs.putDouble("homeLat", lat);
        prefs.putDouble("homeLon", lon);
        lastLat = lat;
        lastLon = lon;
        havePersisted = true;
    }
}

void init() {
    prefs.begin("adsb_radar", false);
    gpsEnabled = prefs.getBool("gpsEn", false);
    gpsPinIndex = prefs.getUChar("gpsPinIdx", 0);
    if (gpsPinIndex >= Config::GPS_PIN_CANDIDATE_COUNT) gpsPinIndex = 0;
    sourcePref = static_cast<SourcePref>(prefs.getUChar("locSrc", static_cast<uint8_t>(SourcePref::Auto)));

    double lat = prefs.getDouble("homeLat", 0.0);
    double lon = prefs.getDouble("homeLon", 0.0);
    if (lat != 0.0 || lon != 0.0) {
        lastLat = lat;
        lastLon = lon;
        havePersisted = true;
        // If the user had explicitly chosen Manual last session, report it
        // as such right away rather than the more generic "Persisted",
        // which read the same whether the coords came from GPS, IP, or a
        // manual entry.
        source = (sourcePref == SourcePref::Manual) ? Source::Manual : Source::Persisted;
    }

    startGpsSerialIfNeeded();
}

void update() {
    if (!gpsEnabled) return;
    startGpsSerialIfNeeded();

    while (gpsSerial.available() > 0) {
        gps.encode(gpsSerial.read());
    }

    if (gps.location.isValid() && gps.location.isUpdated()) {
        persistLocation(gps.location.lat(), gps.location.lng());
        source = Source::GpsFix;
    }
}

void requestIpLookupIfNeeded() {
    // User explicitly wants the saved coordinates only - never let a
    // background IP lookup silently overwrite them.
    if (sourcePref == SourcePref::Manual) return;
    if (ipLookupDone) return;
    if (gps.location.isValid()) return;
    if (WiFi.status() != WL_CONNECTED) return;

    uint32_t now = millis();

    if (lastIpLookupAttemptMs != 0 && now - lastIpLookupAttemptMs < IP_LOOKUP_RETRY_MS) {
        return;
    }
    lastIpLookupAttemptMs = now;

    WiFiClient client;
    HTTPClient http;
    char url[96];
    snprintf(url, sizeof(url), "http://%s%s", Config::IP_GEO_HOST, Config::IP_GEO_PATH);

    if (!http.begin(client, url)) return;

    http.setTimeout(5000);

    int code = http.GET();
    if (code != HTTP_CODE_OK) { http.end(); return; } 

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err) return;

    const char* status = doc["status"] | "";
    if (strcmp(status, "success") != 0) return;

    double lat = doc["lat"] | 0.0;
    double lon = doc["lon"] | 0.0;
    if (lat == 0.0 && lon == 0.0) return;

    persistLocation(lat, lon);
    source = Source::IpGeolocation;
    ipLookupDone = true;
}

void getHomeLocation(double& lat, double& lon) {
    if (gps.location.isValid()) {
        lat = gps.location.lat();
        lon = gps.location.lng();
        return;
    }
    if (havePersisted) {
        lat = lastLat;
        lon = lastLon;
        return;
    }
}

Source currentSource() { return source; }

void setManualLocation(double lat, double lon) {
    persistLocation(lat, lon);
    source = Source::Manual;
    sourcePref = SourcePref::Manual;
    prefs.putUChar("locSrc", static_cast<uint8_t>(sourcePref));
}

void setSourceOverride(SourcePref pref) {
    sourcePref = pref;
    prefs.putUChar("locSrc", static_cast<uint8_t>(sourcePref));

    if (pref == SourcePref::Manual) {
        if (havePersisted) source = Source::Manual;
    } else {
        // Switching back to Auto/IP - drop any cached one-shot lookup
        // state so a fresh IP fix is attempted promptly, instead of
        // leaving whatever result (possibly stale, possibly never
        // attempted) was cached while Manual was in effect.
        ipLookupDone = false;
        lastIpLookupAttemptMs = 0;
    }
}

SourcePref sourcePreference() { return sourcePref; }

void setGpsEnabled(bool enabled) {
    gpsEnabled = enabled;
    prefs.putBool("gpsEn", enabled);
    if (enabled) {
        gpsSerialStarted = false;
        startGpsSerialIfNeeded();
    }
}

bool isGpsEnabled() { return gpsEnabled; }

void cycleGpsPinPair() {
    gpsPinIndex = (gpsPinIndex + 1) % Config::GPS_PIN_CANDIDATE_COUNT;
    prefs.putUChar("gpsPinIdx", gpsPinIndex);
    gpsSerialStarted = false;
    if (gpsEnabled) startGpsSerialIfNeeded();
}

const char* currentGpsPinLabel() {
    return Config::GPS_PIN_CANDIDATES[gpsPinIndex].label;
}

bool hasGpsFix() { return gps.location.isValid(); }

}