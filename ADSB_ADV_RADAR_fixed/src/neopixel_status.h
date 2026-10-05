#pragma once
#include <Arduino.h>
#include "aircraft.h"

namespace NeopixelStatus {
    void init();
    void update(const Aircraft* table, uint8_t count, int selectedIndex = -1);
    void notifySweepAngle(float sweepAngleDeg);
    void tick(uint32_t now);
    void setBrightnessPercent(uint8_t percent);
    uint8_t getBrightnessPercent();
    void setOff();
    // Solid white while an ADS-B fetch is in flight (fetch() is a blocking
    // call, so this is just a static "busy" indicator, not an animation).
    // Normal update()/tick() calls right after the fetch overwrite it with
    // whatever the LED should actually show.
    void flashFetching();
}
