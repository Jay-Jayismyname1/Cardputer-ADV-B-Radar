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

    // Queued beeps drained one-per-tick by tick() - see checkAndAlert().
    uint8_t pendingBeeps = 0;
    uint32_t lastBeepMs = 0;
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
    // Count how many aircraft are newly qualifying this pass - one queued
    // beep per aircraft, capped at MAX_ALERT_BEEPS so a busy approach
    // corridor can't turn into a continuous buzz. Actually playing them
    // happens in tick(), spaced out, so this can't stall the render loop.
    uint8_t newlyQualifying = 0;

    for (uint8_t i = 0; i < count; i++) {
        Aircraft& a = table[i];
        if (!a.valid) continue;

        bool inRange = AlertFilter::shouldAlert(a.distanceKm, static_cast<float>(a.altBaroFt),
                                                 a.isEmergencySquawk(), thresholdKm, thresholdAltFt);
        bool cooldownExpired = (now - a.alertedAtMs) > Config::ALERT_RETRIGGER_COOLDOWN_MS;

        if (inRange && (!a.alerted || cooldownExpired)) {
            if (newlyQualifying < Config::MAX_ALERT_BEEPS) newlyQualifying++;
            a.alerted = true;
            a.alertedAtMs = now;
        } else if (!inRange) {
            // Reset so it can re-alert if it comes back in range later.
            a.alerted = false;
        }
    }

    // Overwrite rather than accumulate: checkAndAlert() only runs once per
    // successful fetch (Config::FETCH_INTERVAL_MS apart, ~8s), while even
    // a full queue of MAX_ALERT_BEEPS drains in well under a second at
    // ALERT_BEEP_GAP_MS apart - there's no realistic way for a previous
    // batch to still be draining when a new one arrives.
    pendingBeeps = newlyQualifying;
}

void tick(uint32_t now) {
    if (pendingBeeps == 0) return;
    if (now - lastBeepMs < Config::ALERT_BEEP_GAP_MS) return;

    beepOnce();
    pendingBeeps--;
    lastBeepMs = now;
}

void beepOnce() {
    if (!beepEnabled) return;
    M5Cardputer.Speaker.tone(Config::ALERT_TONE_HZ, Config::ALERT_TONE_MS);
}

}
