#include "flight_logbook.h"
#include "sd_storage.h"
#include "config.h"
#include <Arduino.h>
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include <cstring>

namespace FlightLogbook {

namespace {
    // How many distinct aircraft we remember having already logged "today"
    // before we start forgetting the oldest ones. Sized well above
    // Config::MAX_TRACKED_AIRCRAFT so a normal day of traffic doesn't
    // overflow it. If a very busy day does overflow it, the oldest entries
    // simply get evicted and could end up logged a second time later -
    // a harmless duplicate line, not a crash or lost data.
    constexpr uint8_t MAX_REMEMBERED = 96;

    struct SeenEntry {
        char hex[7] = {0};
        int16_t loggedYday = -1; // day-of-year this hex was logged, -1 = empty slot
    };

    SeenEntry seen[MAX_REMEMBERED];
    uint8_t nextSlot = 0; // ring buffer write position, for eviction when full

    // Tracks which day-of-year we last loaded the "seen today" list for.
    // On the first update() call of a new calendar day (including right
    // after a reboot on the same day as before), we try to restore
    // already-logged hex codes from the SD card instead of starting from
    // an empty list - this is what prevents re-logging the same aircraft
    // after every reboot within the same day.
    int loadedForYday = -1;

    Preferences prefs;
    bool enabled = true;

    struct tm currentDateTm() {
        time_t now = time(nullptr);
        struct tm tmNow;
        localtime_r(&now, &tmNow);
        return tmNow;
    }

    void seenFilePath(const struct tm& t, char* buf, size_t bufSize) {
        snprintf(buf, bufSize, "%s/seen-%04d-%02d-%02d.txt",
                 Config::SD_LOG_DIR, t.tm_year + 1900, t.tm_mon + 1, t.tm_mday);
    }

    bool alreadyLoggedToday(const char* hex, int today) {
        for (auto& e : seen) {
            if (e.loggedYday == today && strncmp(e.hex, hex, sizeof(e.hex)) == 0) {
                return true;
            }
        }
        return false;
    }

    void rememberInMemory(const char* hex, int today) {
        strncpy(seen[nextSlot].hex, hex, sizeof(seen[nextSlot].hex) - 1);
        seen[nextSlot].hex[sizeof(seen[nextSlot].hex) - 1] = '\0';
        seen[nextSlot].loggedYday = today;
        nextSlot = (nextSlot + 1) % MAX_REMEMBERED;
    }

    // Restores today's already-logged hex codes from the SD card, in case
    // this is a reboot rather than the actual first boot of the day.
    void loadSeenForToday(const struct tm& t, int today) {
        if (!SdStorage::isMounted()) return;

        char path[64];
        seenFilePath(t, path, sizeof(path));

        File f = SD.open(path, FILE_READ);
        if (!f) return; // no file yet today - nothing to restore, that's fine

        uint16_t restored = 0;
        while (f.available()) {
            String line = f.readStringUntil('\n');
            line.trim();
            if (line.length() == 0) continue;
            rememberInMemory(line.c_str(), today);
            restored++;
        }
        f.close();

        if (restored > 0) {
            Serial.printf("[Logbook] %u bereits heute geloggte Flugzeuge von SD wiederhergestellt\n",
                          restored);
        }
    }

    void appendSeenFile(const struct tm& t, const char* hex) {
        if (!SdStorage::isMounted()) return;
        char path[64];
        seenFilePath(t, path, sizeof(path));
        File f = SD.open(path, FILE_APPEND);
        if (!f) return;
        f.println(hex);
        f.close();
    }

    void writeLogLine(const struct tm& t, const Aircraft& a) {
        char line[128];
        // Distance is always logged in km, regardless of the km/nm
        // display toggle in Settings - keeps the logbook file consistent
        // and easy to analyze later (e.g. in a spreadsheet), even if the
        // display unit gets switched between sightings.
        snprintf(line, sizeof(line), "%02d:%02d:%02d,%s,%s,%s,%s,%.1f,%ld",
                 t.tm_hour, t.tm_min, t.tm_sec,
                 a.hex,
                 a.callsign[0] ? a.callsign : "",
                 a.reg[0] ? a.reg : "",
                 a.typeCode[0] ? a.typeCode : "",
                 a.distanceKm,
                 (long)a.altBaroFt);
        SdStorage::logEvent(line);
    }
}

void init() {
    prefs.begin("adsb_radar", false);
    enabled = prefs.getBool("logbookOn", true);
}

bool isEnabled() { return enabled; }

void setEnabled(bool e) {
    enabled = e;
    prefs.putBool("logbookOn", enabled);
}

void update(const Aircraft* table, uint8_t count) {
    if (!enabled) return;

    if (!SdStorage::isMounted()) {
        Serial.println("[Logbook] SD not mounted, skipping");
        return;
    }

    struct tm t = currentDateTm();
    int today = t.tm_yday;

    if (today != loadedForYday) {
        loadSeenForToday(t, today);
        loadedForYday = today;
    }

    for (uint8_t i = 0; i < count; i++) {
        const Aircraft& a = table[i];
        if (!a.valid || a.hex[0] == '\0') continue;
        if (alreadyLoggedToday(a.hex, today)) continue;

        Serial.printf("[Logbook] logging new aircraft: %s\n", a.hex);
        writeLogLine(t, a);
        rememberInMemory(a.hex, today);
        appendSeenFile(t, a.hex);
    }
}

}