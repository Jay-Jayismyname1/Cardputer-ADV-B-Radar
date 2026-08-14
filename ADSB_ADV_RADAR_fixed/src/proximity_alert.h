#pragma once
#include <Arduino.h>
#include "aircraft.h"

namespace ProximityAlert {
    void init();
    void setThresholdKm(float km);
    float getThresholdKm();
    void setThresholdAltFt(float ft);
    float getThresholdAltFt();

    // Scans the aircraft table for anything within the configured max
    // distance AND max height (or an emergency squawk, which always
    // qualifies), and beeps at most once per call - see proximity_alert.cpp
    // for why a burst of aircraft doesn't mean a burst of tones.
    void checkAndAlert(Aircraft* table, uint8_t count);
    void beepOnce();
    void setBeepEnabled(bool enabled);
    bool isBeepEnabled();
}
