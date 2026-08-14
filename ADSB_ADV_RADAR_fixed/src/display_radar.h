#pragma once
#include <Arduino.h>
#include <M5Cardputer.h>
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

    // The full-screen sprite this module owns, exposed so other screens
    // that are never shown at the same time as the radar (currently just
    // FlightDetail) can draw into it too instead of allocating their own.
    // This board has no PSRAM, so every extra 240x135x16bpp sprite is
    // ~65KB of scarce internal SRAM permanently gone - a 3rd one (on top
    // of this and SettingsMenu's own) was confirmed on real hardware to
    // starve TLS/JSON allocations elsewhere badly enough to make fetches
    // and the UI itself unreliable. Safe to share precisely because
    // Radar/FlightDetail/Settings are mutually exclusive screens - nothing
    // ever needs two of these buffers valid at once.
    M5Canvas& sprite();

}