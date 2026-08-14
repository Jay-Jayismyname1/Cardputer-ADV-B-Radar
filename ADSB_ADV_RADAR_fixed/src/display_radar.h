#pragma once
#include <Arduino.h>
#include "aircraft.h"

namespace DisplayRadar {

    void init();
    void render(const Aircraft* aircraftList, uint8_t count,
                float rangeKm, uint8_t selectedIndex,
                bool wifiConnected, int batteryPct,
                const char* locationLabel, int lastHttpCode);

    void tickSweep(uint32_t deltaMs);
    void cycleRange();
    float currentRangeKm();
    float currentSweepAngle();

    // Display rotation - reorients the whole radar so "up" doesn't have to
    // mean North, e.g. setRotationDeg(135) puts South-East at the top.
    // Persisted across reboots; degrees are always normalized to [0, 360).
    void setRotationDeg(uint16_t deg);
    void cycleRotation(int16_t stepDeg); // stepDeg may be negative
    uint16_t currentRotationDeg();

}