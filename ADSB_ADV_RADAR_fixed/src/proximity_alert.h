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
    // qualifies). Queues up to Config::MAX_ALERT_BEEPS beeps - one per
    // newly-qualifying aircraft - to be played out over subsequent tick()
    // calls rather than all at once.
    void checkAndAlert(Aircraft* table, uint8_t count);

    // Call once per loop() iteration - plays at most one queued beep every
    // Config::ALERT_BEEP_GAP_MS, so a busy scan's beeps are audible and
    // non-blocking instead of a single call stalling the render loop.
    void tick(uint32_t now);

    void beepOnce();
    void setBeepEnabled(bool enabled);
    bool isBeepEnabled();
}
