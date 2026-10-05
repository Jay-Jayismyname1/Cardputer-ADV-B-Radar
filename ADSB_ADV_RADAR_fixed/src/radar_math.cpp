#include "radar_math.h"
#include <math.h>

namespace RadarMath {

namespace {
    constexpr double EARTH_RADIUS_KM = 6371.0088;
    constexpr double DEG2RAD = M_PI / 180.0;
    constexpr double RAD2DEG = 180.0 / M_PI;
}

PolarCoord toPolar(double lat0, double lon0, double lat1, double lon1) {
    double phi1 = lat0 * DEG2RAD;
    double phi2 = lat1 * DEG2RAD;
    double dPhi = (lat1 - lat0) * DEG2RAD;
    double dLambda = (lon1 - lon0) * DEG2RAD;

    // Haversine
    double a = sin(dPhi / 2) * sin(dPhi / 2) +
               cos(phi1) * cos(phi2) * sin(dLambda / 2) * sin(dLambda / 2);
    double c = 2 * atan2(sqrt(a), sqrt(1 - a));
    double distanceKm = EARTH_RADIUS_KM * c;

    // Initial bearing
    double y = sin(dLambda) * cos(phi2);
    double x = cos(phi1) * sin(phi2) - sin(phi1) * cos(phi2) * cos(dLambda);
    double bearing = atan2(y, x) * RAD2DEG;
    bearing = fmod(bearing + 360.0, 360.0);

    return PolarCoord{ static_cast<float>(distanceKm), static_cast<float>(bearing) };
}

ScreenPoint toScreen(const PolarCoord& polar, int16_t centerX, int16_t centerY,
                     int16_t radiusPx, float rangeKm) {
    float clampedKm = polar.distanceKm > rangeKm ? rangeKm : polar.distanceKm;
    float r = (clampedKm / rangeKm) * radiusPx;

    // bearing 0 = North = "up" on screen = negative Y direction.
    double rad = polar.bearingDeg * DEG2RAD;
    float dx = r * sin(rad);
    float dy = -r * cos(rad);

    return ScreenPoint{
        static_cast<int16_t>(centerX + dx),
        static_cast<int16_t>(centerY + dy)
    };
}

float applyRotation(float bearingDeg, float rotationDeg) {
    float result = fmodf(bearingDeg - rotationDeg, 360.0f);
    if (result < 0.0f) result += 360.0f;
    return result;
}

ScreenVector rotateVector(float lx, float ly, float angleDeg) {
    double rad = angleDeg * DEG2RAD;
    double s = sin(rad), c = cos(rad);
    // Standard 2D rotation matrix [c -s; s c] applied to (lx, ly). This is
    // the same "0=up, clockwise-positive" convention as toScreen(): at
    // angleDeg=90 (East), local "forward" (0,-1) maps to (+1, 0) - i.e.
    // matches toScreen()'s dx=sin(90)=+1, dy=-cos(90)=0 for a bearing-90
    // aircraft. (An earlier version of this had the two sign flips
    // swapped, which is a mirror, not a rotation - it left North/South
    // headings looking correct by coincidence, since sin(0)=sin(180)=0,
    // but silently pointed every East/West-leaning heading arrow to the
    // wrong side of the radar.)
    return ScreenVector{
        static_cast<float>(lx * c - ly * s),
        static_cast<float>(lx * s + ly * c)
    };
}

} // namespace RadarMath
