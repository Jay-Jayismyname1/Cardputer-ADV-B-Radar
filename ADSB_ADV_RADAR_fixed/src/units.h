#pragma once
#include <Arduino.h>
#include "unit_math.h"

namespace Units {

    enum class Distance : uint8_t { Km = 0, NauticalMiles = 1, Miles = 2 };
    enum class Altitude : uint8_t { Feet = 0, Meters = 1 };

    // Re-exported from unit_math.h so callers only need to include units.h.
    constexpr float KM_PER_NM   = UnitMath::KM_PER_NM;
    constexpr float KM_PER_MILE = UnitMath::KM_PER_MILE;
    constexpr float FT_PER_M    = UnitMath::FT_PER_M;

    void init();

    Distance current();
    void toggleDistance(); // cycles Km -> NauticalMiles -> Miles -> Km

    Altitude currentAltitude();
    void toggleAltitude(); // cycles Feet -> Meters -> Feet

    // Formatiert eine in km übergebene Distanz passend zur aktuell
    // gewählten Einheit, z.B. "42km", "23nm" oder "26mi".
    void formatDistance(float km, char* buf, size_t bufSize);

    // Ground speed follows the same Distance unit rather than being its own
    // separate setting - km/h with Km, kt (unchanged) with Nautical Miles,
    // mph with Miles. A knot is already a nautical-mile-per-hour, so "kt"
    // pairs naturally with the Nautical Miles distance unit the same way
    // VS already pairs with the Altitude unit. Input is ground speed in
    // knots (the unit the ADS-B feed reports it in), e.g. "250kt", "463km/h"
    // or "288mph".
    void formatSpeed(float knots, char* buf, size_t bufSize);

    // Nur das Einheiten-Kürzel ("km" / "nm" / "mi"), z.B. für eigene Labels.
    const char* distSuffix();

    // Nur das Einheiten-Kürzel ("ft" / "m") für Höhenangaben.
    const char* altSuffix();

    // Full unit names ("Kilometers" / "Nautical Miles" / "Statute Miles",
    // "Feet" / "Meters") for UI spots with room to spell it out, e.g. the
    // Units selection screen. Everywhere else (HUD, range scale, Prox Beep
    // screen) should keep using the short suffixes above.
    const char* distFullName();
    const char* altFullName();
}
