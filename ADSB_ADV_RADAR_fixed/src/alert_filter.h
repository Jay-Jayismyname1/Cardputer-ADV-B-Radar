#pragma once

// Pure predicate for the proximity beep, deliberately free of Arduino/ESP32
// dependencies so it can be exercised by the native Unity test env (see
// platformio.ini [env:native]) without pulling in the rest of the firmware.
namespace AlertFilter {

    // Emergency squawks (7500/7600/7700) always alert regardless of range -
    // that takes priority over the distance/altitude thresholds.
    inline bool shouldAlert(float distanceKm, float altitudeFt, bool isEmergency,
                             float thresholdKm, float thresholdAltFt) {
        if (isEmergency) return true;
        return distanceKm <= thresholdKm && altitudeFt <= thresholdAltFt;
    }

}
