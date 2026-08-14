#pragma once

// Pure unit-conversion math, deliberately free of Arduino/ESP32 dependencies
// so it can be exercised by the native Unity test env (see platformio.ini
// [env:native]) without pulling in the rest of the firmware. units.cpp wraps
// these with NVS persistence and display suffixes.
namespace UnitMath {

    constexpr float KM_PER_NM   = 1.852f;    // international nautical mile
    constexpr float KM_PER_MILE = 1.609344f; // international statute mile
    constexpr float FT_PER_M    = 3.28084f;

    inline float kmToNm(float km)    { return km / KM_PER_NM; }
    inline float kmToMiles(float km) { return km / KM_PER_MILE; }
    inline float ftToMeters(float ft) { return ft / FT_PER_M; }

}
