#include "adsb_client.h"
#include "radar_math.h"
#include <WiFiClientSecure.h>
#include <WiFiClient.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <time.h>
#include <cstdint>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <atomic>
#include <string.h>

namespace AdsbClient {

namespace {
    const char* kNoValidate = nullptr;

    // Reused across fetches instead of being constructed fresh each call.
    // A fresh WiFiClientSecure/HTTPClient pair means a brand-new TLS
    // handshake on every single fetch - that handshake is typically the
    // single biggest contributor to the multi-second UI freeze during a
    // fetch cycle. Keeping the client alive lets the underlying TLS
    // session/connection be reused where the server supports it.
    WiFiClientSecure persistentClient;
    bool clientConfigured = false;

    // --- Data source (adsb.fi vs. user's own tar1090/readsb) --------------
    Preferences prefs;
    DataSource dataSource = DataSource::AdsbFi;
    char customHostBuf[Config::TAR1090_HOST_MAX_LEN] = "";
    uint16_t customPortVal = Config::DEFAULT_TAR1090_PORT;

    // --- Background task state --------------------------------------------
    // The fetch task runs on core 0 and writes into workTable/workResult.
    // The main loop (core 1) only ever reads them, and only after
    // readyFlag is true - the task sets that flag last (with release
    // ordering), so by the time the main loop sees it true (via an
    // acquire load) the task is guaranteed done writing. That's a simple,
    // race-free single-producer/single-consumer hand-off with no extra
    // mutex needed, as long as only one fetch is ever in flight at a time
    // (enforced by busyFlag).
    TaskHandle_t fetchTaskHandle = nullptr;
    SemaphoreHandle_t startSem = nullptr;

    std::atomic<bool> busyFlag{false};
    std::atomic<bool> readyFlag{false};

    struct Request { double lat; double lon; float radiusKm; };
    Request pendingRequest;

    Aircraft workTable[Config::MAX_TRACKED_AIRCRAFT];
    FetchResult workResult;

    void fetchTaskFn(void*) {
        for (;;) {
            xSemaphoreTake(startSem, portMAX_DELAY);
            // pendingRequest was fully written by requestFetch() strictly
            // before it gave this semaphore, so it's safe to read here.
            workResult = fetch(pendingRequest.lat, pendingRequest.lon,
                                pendingRequest.radiusKm,
                                workTable, Config::MAX_TRACKED_AIRCRAFT);
            readyFlag.store(true, std::memory_order_release);
        }
    }

    constexpr int32_t OFFSET_LOOKUP_FAILED = INT32_MIN;

    // Looks up the current UTC offset (in seconds, already accounting for
    // whatever DST is currently in effect at that location) for wherever
    // this device's internet connection is right now. This is what makes
    // local-time timestamps (e.g. in the flight logbook) automatically
    // correct for every user of this firmware, worldwide - instead of a
    // single hardcoded country/timezone that would only be right for one
    // region and wrong for everyone else.
    int32_t fetchUtcOffsetSeconds() {
        if (WiFi.status() != WL_CONNECTED) return OFFSET_LOOKUP_FAILED;

        WiFiClient client;
        HTTPClient http;
        http.setTimeout(Config::HTTP_TIMEOUT_MS);
        if (!http.begin(client, "http://ip-api.com/json/?fields=status,offset")) {
            return OFFSET_LOOKUP_FAILED;
        }

        int code = http.GET();
        if (code != HTTP_CODE_OK) {
            http.end();
            return OFFSET_LOOKUP_FAILED;
        }

        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, http.getStream());
        http.end();

        if (err) return OFFSET_LOOKUP_FAILED;

        const char* status = doc["status"] | "";
        if (strcmp(status, "success") != 0) return OFFSET_LOOKUP_FAILED;

        return doc["offset"] | OFFSET_LOOKUP_FAILED;
    }

    // --- Shared aircraft-feed parsing (adsb.fi and tar1090) ----------------
    // Every field extracted below has the identical name in both adsb.fi's
    // "ac" array and tar1090/readsb's "aircraft" array - confirmed against
    // live tar1090 data during research. Only the transport, URL and this
    // top-level array key actually differ between the two sources, so both
    // fetch paths share this same parsing logic.

    void parseAircraftFields(JsonObjectConst ac, Aircraft& a) {
        a = Aircraft{}; // reset

        const char* hex = ac["hex"] | "";
        strncpy(a.hex, hex, sizeof(a.hex) - 1);

        const char* flight = ac["flight"] | "";
        strncpy(a.callsign, flight, sizeof(a.callsign) - 1);

        const char* reg = ac["r"] | "";
        strncpy(a.reg, reg, sizeof(a.reg) - 1);

        const char* type = ac["t"] | "";
        strncpy(a.typeCode, type, sizeof(a.typeCode) - 1);

        const char* squawk = ac["squawk"] | "";
        strncpy(a.squawk, squawk, sizeof(a.squawk) - 1);

        a.lat = ac["lat"] | 0.0f;
        a.lon = ac["lon"] | 0.0f;

        if (ac["alt_baro"].is<const char*>()) {
            a.altBaroFt = 0;
        } else {
            a.altBaroFt = ac["alt_baro"] | 0;
        }

        a.vertRateFtMin = ac["baro_rate"] | 0;
        a.groundSpeedKt = ac["gs"] | 0.0f;
        a.headingDeg    = ac["track"] | 0.0f;

        a.lastSeenMs = millis();
        a.valid = (a.lat != 0.0f || a.lon != 0.0f);
    }

    // Keeps the nearest `tableCapacity` valid, in-range aircraft seen so
    // far. Needed because tar1090's /data/aircraft.json has no server-side
    // range filtering at all (unlike adsb.fi's own radius query) - it
    // always returns every aircraft the receiver currently sees, which can
    // be in the hundreds. Applied uniformly to both sources: harmless for
    // adsb.fi (which rarely returns anywhere near tableCapacity entries
    // since it's already radius-filtered server-side) and required for
    // tar1090 so the fixed-size table holds the closest aircraft rather
    // than whichever ones happened to come first in the feed's array.
    void considerAircraft(Aircraft* table, uint8_t tableCapacity, uint8_t& count,
                           const Aircraft& candidate, double homeLat, double homeLon,
                           float radiusKm) {
        if (!candidate.valid) return;

        RadarMath::PolarCoord polar =
            RadarMath::toPolar(homeLat, homeLon, candidate.lat, candidate.lon);
        if (polar.distanceKm > radiusKm) return;

        if (count < tableCapacity) {
            table[count] = candidate;
            // Stashed only so the "replace the farthest" comparison below
            // has something to compare against - AircraftTable::postFetchUpdate()
            // recomputes this properly (same inputs) right after consumeResult().
            table[count].distanceKm = polar.distanceKm;
            count++;
            return;
        }

        uint8_t farthestIdx = 0;
        float farthestKm = table[0].distanceKm;
        for (uint8_t i = 1; i < tableCapacity; i++) {
            if (table[i].distanceKm > farthestKm) {
                farthestKm = table[i].distanceKm;
                farthestIdx = i;
            }
        }
        if (polar.distanceKm < farthestKm) {
            table[farthestIdx] = candidate;
            table[farthestIdx].distanceKm = polar.distanceKm;
        }
    }

    // Parses a JSON aircraft feed from `stream`, filtered to only the
    // fields parseAircraftFields() reads, keeping the nearest
    // `tableCapacity` valid aircraft within `radiusKm` of home.
    // `arrayKey` is "ac" for adsb.fi, "aircraft" for tar1090/readsb.
    FetchResult parseAircraftFeed(Stream& stream, const char* arrayKey,
                                   double homeLat, double homeLon, float radiusKm,
                                   Aircraft* table, uint8_t tableCapacity) {
        FetchResult result;

        JsonDocument filter;
        JsonObject filterAc = filter[arrayKey].add<JsonObject>();
        filterAc["hex"]      = true;
        filterAc["flight"]   = true;
        filterAc["r"]        = true;   // registration
        filterAc["t"]        = true;   // type code
        filterAc["squawk"]   = true;   // transponder code, for emergency (7500/7600/7700) detection
        filterAc["lat"]      = true;
        filterAc["lon"]      = true;
        filterAc["alt_baro"] = true;
        filterAc["baro_rate"]= true;
        filterAc["gs"]       = true;
        filterAc["track"]    = true;

        JsonDocument doc;
        DeserializationError err = deserializeJson(
            doc, stream, DeserializationOption::Filter(filter));

        if (err) {
            result.ok = false;
            return result;
        }

        JsonArray acArray = doc[arrayKey].as<JsonArray>();
        uint8_t count = 0;
        for (JsonObject ac : acArray) {
            Aircraft candidate;
            parseAircraftFields(ac, candidate);
            considerAircraft(table, tableCapacity, count, candidate, homeLat, homeLon, radiusKm);
        }

        result.ok = true;
        result.aircraftCount = count;
        return result;
    }

    FetchResult fetchAdsbFi(double homeLat, double homeLon, float radiusKm,
                             Aircraft* table, uint8_t tableCapacity) {
        FetchResult result;

        if (WiFi.status() != WL_CONNECTED) return result;

        if (!clientConfigured) {
            persistentClient.setInsecure();
            persistentClient.setTimeout(Config::HTTP_TIMEOUT_MS);
            clientConfigured = true;
        }

        HTTPClient http;
        char url[160];
        snprintf(url, sizeof(url),
                 "https://%s/api/v3/lat/%.5f/lon/%.5f/dist/%.0f",
                 Config::ADSB_API_HOST, homeLat, homeLon, radiusKm);

        http.setTimeout(Config::HTTP_TIMEOUT_MS);
        if (!http.begin(persistentClient, url)) return result;
        http.setReuse(true);

        int code = http.GET();
        result.httpCode = code;

        if (code != HTTP_CODE_OK) {
            http.end();
            return result;
        }

        result = parseAircraftFeed(http.getStream(), "ac", homeLat, homeLon, radiusKm,
                                    table, tableCapacity);
        result.httpCode = code;
        http.end();
        return result;
    }

    // Plain HTTP, no TLS - matches the transport tar1090/readsb instances
    // actually serve on (confirmed against two live instances during
    // research), and mirrors the plain WiFiClient+HTTPClient pattern
    // already used elsewhere in this codebase for the IP-geolocation
    // lookup above, rather than the WiFiClientSecure setup adsb.fi needs.
    FetchResult fetchTar1090(double homeLat, double homeLon, float radiusKm,
                              Aircraft* table, uint8_t tableCapacity) {
        FetchResult result;

        if (WiFi.status() != WL_CONNECTED) return result;
        if (customHostBuf[0] == '\0') return result; // not configured yet

        WiFiClient client;
        HTTPClient http;
        char url[160];
        snprintf(url, sizeof(url), "http://%s:%u%s",
                 customHostBuf, customPortVal, Config::TAR1090_AIRCRAFT_PATH);

        http.setTimeout(Config::HTTP_TIMEOUT_MS);
        if (!http.begin(client, url)) return result;

        int code = http.GET();
        result.httpCode = code;

        if (code != HTTP_CODE_OK) {
            http.end();
            return result;
        }

        result = parseAircraftFeed(http.getStream(), "aircraft", homeLat, homeLon, radiusKm,
                                    table, tableCapacity);
        result.httpCode = code;
        http.end();
        return result;
    }
}

void primeTime() {
    // First get a roughly-correct UTC clock running via NTP, so there's
    // *a* valid time even if the timezone lookup below fails or times out.
    configTime(0, 0, "pool.ntp.org", "time.nist.gov");
    time_t now = time(nullptr);
    uint32_t start = millis();
    while (now < 8 * 3600 * 2 && millis() - start < 5000) {
        delay(100);
        now = time(nullptr);
    }

    // Then look up this device's actual local UTC offset (via IP
    // geolocation, same service already used for home location) and
    // re-sync the clock with it, so timestamps show local time instead of
    // UTC. If the lookup fails for any reason, time simply stays in UTC -
    // still valid and internally consistent, just not localized.
    int32_t offsetSec = fetchUtcOffsetSeconds();
    if (offsetSec != OFFSET_LOOKUP_FAILED) {
        configTime(offsetSec, 0, "pool.ntp.org", "time.nist.gov");
    }
}

void init() {
    prefs.begin("adsb_radar", false);
    dataSource = static_cast<DataSource>(
        prefs.getUChar("dataSrc", static_cast<uint8_t>(DataSource::AdsbFi)));

    String host = prefs.getString("t1090Host", "");
    strncpy(customHostBuf, host.c_str(), sizeof(customHostBuf) - 1);
    customHostBuf[sizeof(customHostBuf) - 1] = '\0';

    customPortVal = prefs.getUShort("t1090Port", Config::DEFAULT_TAR1090_PORT);
}

void setDataSource(DataSource src) {
    dataSource = src;
    prefs.putUChar("dataSrc", static_cast<uint8_t>(dataSource));
}

DataSource currentDataSource() { return dataSource; }

void setCustomHost(const char* host) {
    strncpy(customHostBuf, host, sizeof(customHostBuf) - 1);
    customHostBuf[sizeof(customHostBuf) - 1] = '\0';
    prefs.putString("t1090Host", customHostBuf);
}

const char* customHost() { return customHostBuf; }

void setCustomPort(uint16_t port) {
    customPortVal = port;
    prefs.putUShort("t1090Port", customPortVal);
}

uint16_t customPort() { return customPortVal; }

FetchResult fetch(double homeLat, double homeLon, float radiusKm,
                   Aircraft* table, uint8_t tableCapacity) {
    if (dataSource == DataSource::CustomTar1090) {
        return fetchTar1090(homeLat, homeLon, radiusKm, table, tableCapacity);
    }
    return fetchAdsbFi(homeLat, homeLon, radiusKm, table, tableCapacity);
}

void startBackgroundTask() {
    if (fetchTaskHandle) return; // already started
    startSem = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(fetchTaskFn, "adsbFetch",
                             12288, nullptr, 1, &fetchTaskHandle,
                             0 /* core 0 - opposite the Arduino loop task */);
}

bool requestFetch(double homeLat, double homeLon, float radiusKm) {
    if (busyFlag.load(std::memory_order_acquire)) return false;
    pendingRequest = { homeLat, homeLon, radiusKm };
    busyFlag.store(true, std::memory_order_release);
    xSemaphoreGive(startSem);
    return true;
}

bool resultReady() {
    return readyFlag.load(std::memory_order_acquire);
}

FetchResult consumeResult(Aircraft* outTable, uint8_t outCapacity) {
    FetchResult r = workResult;
    uint8_t n = outCapacity < Config::MAX_TRACKED_AIRCRAFT
                    ? outCapacity : Config::MAX_TRACKED_AIRCRAFT;
    memcpy(outTable, workTable, n * sizeof(Aircraft));
    readyFlag.store(false, std::memory_order_release);
    busyFlag.store(false, std::memory_order_release);
    return r;
}

}