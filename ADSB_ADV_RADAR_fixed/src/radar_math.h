#pragma once
#include <cstdint>

namespace RadarMath {

    struct PolarCoord {
        float distanceKm;
        float bearingDeg; // 0-360, 0 = North, clockwise
    };

    struct ScreenPoint {
        int16_t x;
        int16_t y;
    };

    struct ScreenVector {
        float dx;
        float dy;
    };

    PolarCoord toPolar(double lat0, double lon0, double lat1, double lon1);

    ScreenPoint toScreen(const PolarCoord& polar, int16_t centerX, int16_t centerY,
                         int16_t radiusPx, float rangeKm);

    // Rotates a local-space vector (lx, ly) by angleDeg using this
    // codebase's screen convention: 0deg = "up" (-Y), clockwise-positive,
    // matching toScreen()'s dx=sin/dy=-cos bearing mapping and the compass
    // (0=N/90=E/180=S/270=W). Used to orient shapes drawn at an arbitrary
    // heading - e.g. the aircraft heading-arrow triangle - so a vertex
    // offset from the shape's "nose forward" (0, -len) local origin ends up
    // pointing the same direction on screen that toScreen() would place an
    // aircraft at that bearing. Kept separate from toScreen() itself since
    // that one is specifically distance+bearing -> absolute screen point,
    // not a general local-vector rotation.
    ScreenVector rotateVector(float lx, float ly, float angleDeg);

    // Reorients a real-world bearing for a rotated display: subtracts
    // rotationDeg and wraps to [0, 360). Bearing/heading math above stays
    // untouched (pure compass geometry) - this is purely a display-rotation
    // helper, applied at the call site in display_radar.cpp to both an
    // aircraft's position bearing and its heading before they reach
    // toScreen()/the heading-arrow renderer.
    float applyRotation(float bearingDeg, float rotationDeg);

}
