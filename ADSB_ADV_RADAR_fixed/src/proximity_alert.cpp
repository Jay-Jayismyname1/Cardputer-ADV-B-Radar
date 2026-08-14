#include "proximity_alert.h"
#include "config.h"
#include "alert_filter.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace ProximityAlert {

namespace {
    Preferences prefs;
    float thresholdKm = Config::DEFAULT_PROXIMITY_ALERT_KM;
    float thresholdAltFt = Config::DEFAULT_PROXIMITY_ALERT_ALT_FT;
    bool beepEnabled = true;
}

void init() {
    prefs.begin("adsb_radar", false);
    thresholdKm = prefs.getFloat("alertKm", Config::DEFAULT_PROXIMITY_ALERT_KM);
    thresholdAltFt = prefs.getFloat("alertAltFt", Config::DEFAULT_PROXIMITY_ALERT_ALT_FT);
    beepEnabled = prefs.getBool("beepOn", true);
}

void setThresholdKm(float km) {
    thresholdKm = km;
    prefs.putFloat("alertKm", km);
}

float getThresholdKm() { return thresholdKm; }

void setThresholdAltFt(float ft) {
    thresholdAltFt = ft;
    prefs.putFloat("alertAltFt", ft);
}

float getThresholdAltFt() { return thresholdAltFt; }

void setBeepEnabled(bool enabled) {
    beepEnabled = enabled;
    prefs.putBool("beepOn", enabled);
}

bool isBeepEnabled() { return beepEnabled; }

void checkAndAlert(Aircraft* table, uint8_t count) {
    uint32_t now = millis();
    // Only sound one tone per call even if several aircraft qualify at
    // once (e.g. a busy approach corridor) - without this, every fetch
    // tick would fire a burst of back-to-back beeps, one per aircraft.
    // The per-aircraft alerted/alertedAtMs bookkeeping still runs for
    // every aircraft so re-arming after leaving/re-entering range works
    // the same as before.
    bool alreadyBeeped = false;

    for (uint8_t i = 0; i < count; i++) {
        Aircraft& a = table[i];
        if (!a.valid) continue;

        bool inRange = AlertFilter::shouldAlert(a.distanceKm, static_cast<float>(a.altBaroFt),
                                                 a.isEmergencySquawk(), thresholdKm, thresholdAltFt);
        bool cooldownExpired = (now - a.alertedAtMs) > Config::ALERT_RETRIGGER_COOLDOWN_MS;

        if (inRange && (!a.alerted || cooldownExpired)) {
            if (!alreadyBeeped) {
                beepOnce();
                alreadyBeeped = true;
            }
            a.alerted = true;
            a.alertedAtMs = now;
        } else if (!inRange) {
            // Reset so it can re-alert if it comes back in range later.
            a.alerted = false;
        }
    }
}

void beepOnce() {
    if (!beepEnabled) return;
    M5Cardputer.Speaker.tone(Config::ALERT_TONE_HZ, Config::ALERT_TONE_MS);
}

}
