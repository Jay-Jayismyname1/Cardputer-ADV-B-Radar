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

    // GPS/IP-derived location - whatever the device figured out on its own.
    double lastLat = 0, lastLon = 0;
    bool havePersisted = false;

    // Manual entry, kept in its own prefs keys and its own in-memory copy so
    // a background IP lookup (which only ever touches lastLat/lastLon above)
    // can never overwrite it. Without this split, switching Source to IP and
    // back to Manual would silently lose whatever the user had typed in.
    double manualLat = 0, manualLon = 0;
    bool haveManualLocation = false;

    bool ipLookupDone = false;
    uint32_t lastIpLookupAttemptMs = 0;
    constexpr uint32_t IP_LOOKUP_RETRY_MS = 15000;

    // TinyGPSPlus's location.isValid() latches true forever once any fix
    // has ever been parsed - it never resets on its own if the antenna
    // later loses lock. location.age() (ms since the last successful fix)
    // is what actually reflects "is this still current" - a fix older than
    // this is treated as lost, not just "the last one we happened to see".
    // 10s gives real GPS modules (typically ~1Hz updates) generous margin
    // over a couple of missed sentences without masking an actual lock loss.
    constexpr uint32_t GPS_FIX_STALE_MS = 10000;

    Source source = Source::None;
    SourcePref sourcePref = SourcePref::Auto;

    void startGpsSerialIfNeeded() {
        if (!gpsEnabled || gpsSerialStarted) return;
        const auto& pins = Config::GPS_PIN_CANDIDATES[gpsPinIndex];
        gpsSerial.begin(Config::GPS_BAUD, SERIAL_8N1, pins.rx, pins.tx);
        gpsSerialStarted = true;
    }

    // Releases the UART peripheral and RX/TX pins entirely, rather than just
    // leaving update() to stop draining it (which it already does via its
    // own gpsEnabled check). There's no software-controlled power pin to
    // this module - the header supplies it constant power regardless of
    // what the ESP32 side does - so this is the most "off" a software
    // toggle can make it: the ESP32 stops listening on those pins at all.
    void stopGpsSerialIfNeeded() {
        if (!gpsSerialStarted) return;
        gpsSerial.end();
        gpsSerialStarted = false;
    }

    void persistLocation(double lat, double lon) {
        prefs.putDouble("homeLat", lat);
        prefs.putDouble("homeLon", lon);
        lastLat = lat;
        lastLon = lon;
        havePersisted = true;
    }

    void persistManualLocation(double lat, double lon) {
        prefs.putDouble("manualLat", lat);
        prefs.putDouble("manualLon", lon);
        manualLat = lat;
        manualLon = lon;
        haveManualLocation = true;
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
    }

    double mLat = prefs.getDouble("manualLat", 0.0);
    double mLon = prefs.getDouble("manualLon", 0.0);
    if (mLat != 0.0 || mLon != 0.0) {
        manualLat = mLat;
        manualLon = mLon;
        haveManualLocation = true;
    } else if (sourcePref == SourcePref::Manual && havePersisted) {
        // One-time migration: firmware versions before the manual/auto
        // location split stored manual entries in the same "homeLat"/
        // "homeLon" keys as GPS/IP fixes. If this is that older state (no
        // manualLat/Lon saved yet, but the user's preference was already
        // Manual), seed the new manual keys from it so the upgrade doesn't
        // look like the manual coordinates were lost.
        persistManualLocation(lastLat, lastLon);
    }

    if (sourcePref == SourcePref::Manual && haveManualLocation) {
        // If the user had explicitly chosen Manual last session, report it
        // as such right away rather than the more generic "Persisted",
        // which read the same whether the coords came from GPS, IP, or a
        // manual entry.
        source = Source::Manual;
    } else if (havePersisted) {
        source = Source::Persisted;
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
    // hasGpsFix(), not a raw isValid() check - see its own comment for why
    // isValid() alone can't tell "GPS was turned off" or "lock was lost"
    // apart from "still fine". Without this, either case would permanently
    // block IP lookups from ever kicking in as a fallback.
    if (hasGpsFix()) return;
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
    // hasGpsFix() rather than a raw isValid() check - see its comment.
    // Otherwise a device that ever had a GPS fix would keep reporting that
    // frozen position forever, whether Hardware GPS was later disabled or
    // just lost lock, ignoring whatever Manual/IP source should take over.
    if (hasGpsFix()) {
        lat = gps.location.lat();
        lon = gps.location.lng();
        return;
    }
    if (sourcePref == SourcePref::Manual && haveManualLocation) {
        lat = manualLat;
        lon = manualLon;
        return;
    }
    if (havePersisted) {
        lat = lastLat;
        lon = lastLon;
        return;
    }
}

Source currentSource() {
    // `source` is only ever written to GpsFix by update() when a fresh fix
    // actually arrives - nothing resets it back if that fix later goes
    // stale (see hasGpsFix()'s comment on why isValid() can't tell). Without
    // this check, the HUD/Location screen would keep reporting "GPS" as the
    // active source long after getHomeLocation() had already fallen back to
    // IP/Manual/a last-known position - reporting one source while actually
    // using another.
    if (source == Source::GpsFix && !hasGpsFix()) {
        return (sourcePref == SourcePref::Manual && haveManualLocation) ? Source::Manual
               : havePersisted ? Source::Persisted : Source::None;
    }
    return source;
}

void setManualLocation(double lat, double lon) {
    persistManualLocation(lat, lon);
    source = Source::Manual;
    sourcePref = SourcePref::Manual;
    prefs.putUChar("locSrc", static_cast<uint8_t>(sourcePref));
}

bool hasManualLocation() { return haveManualLocation; }

void getManualLocation(double& lat, double& lon) {
    lat = manualLat;
    lon = manualLon;
}

void setSourceOverride(SourcePref pref) {
    sourcePref = pref;
    prefs.putUChar("locSrc", static_cast<uint8_t>(sourcePref));

    if (pref == SourcePref::Manual) {
        source = haveManualLocation ? Source::Manual : Source::None;
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
    } else {
        // Every read above (getHomeLocation()/hasGpsFix()/
        // requestIpLookupIfNeeded()) already gates on gpsEnabled before
        // trusting gps.location.isValid(), so they stop treating a past fix
        // as current the instant this flag flips. `source` and the IP
        // lookup cache still need an explicit nudge here though, so the
        // reported source - and a fresh IP fix, if that's the active
        // preference - land immediately instead of waiting on the next
        // update()/retry tick.
        source = (sourcePref == SourcePref::Manual && haveManualLocation) ? Source::Manual
                 : havePersisted ? Source::Persisted : Source::None;
        ipLookupDone = false;
        lastIpLookupAttemptMs = 0;
        stopGpsSerialIfNeeded();
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

bool hasGpsFix() {
    // gpsEnabled: see setGpsEnabled()'s comment - update() stops feeding
    // gps new sentences the instant this goes false, but isValid() itself
    // never resets on its own, so it isn't enough by itself.
    //
    // age() <= GPS_FIX_STALE_MS: isValid() has the same problem for lock
    // *loss* while still enabled - it latches true forever after the first
    // fix and has no idea the antenna went indoors five minutes ago. age()
    // is what actually says whether that fix is still current.
    return gpsEnabled && gps.location.isValid() && gps.location.age() <= GPS_FIX_STALE_MS;
}

uint32_t satelliteCount() {
    return (gpsEnabled && gps.satellites.isValid()) ? gps.satellites.value() : 0;
}

}