#include "units.h"
#include <Preferences.h>

namespace Units {

namespace {
    Preferences prefs;
    Distance unit = Distance::Km;
    Altitude unitAlt = Altitude::Feet; // matches the firmware's previous ft-only behavior
}

void init() {
    prefs.begin("adsb_radar", false);
    unit = static_cast<Distance>(prefs.getUChar("distUnit", static_cast<uint8_t>(Distance::Km)));
    unitAlt = static_cast<Altitude>(prefs.getUChar("altUnit", static_cast<uint8_t>(Altitude::Feet)));
}

Distance current() {
    return unit;
}

void toggleDistance() {
    switch (unit) {
        case Distance::Km:            unit = Distance::NauticalMiles; break;
        case Distance::NauticalMiles: unit = Distance::Miles;         break;
        case Distance::Miles:         unit = Distance::Km;            break;
    }
    prefs.putUChar("distUnit", static_cast<uint8_t>(unit));
}

Altitude currentAltitude() {
    return unitAlt;
}

void toggleAltitude() {
    unitAlt = (unitAlt == Altitude::Feet) ? Altitude::Meters : Altitude::Feet;
    prefs.putUChar("altUnit", static_cast<uint8_t>(unitAlt));
}

void formatDistance(float km, char* buf, size_t bufSize) {
    switch (unit) {
        case Distance::NauticalMiles:
            snprintf(buf, bufSize, "%.0fnm", UnitMath::kmToNm(km));
            break;
        case Distance::Miles:
            snprintf(buf, bufSize, "%.0fmi", UnitMath::kmToMiles(km));
            break;
        default:
            snprintf(buf, bufSize, "%.0fkm", km);
            break;
    }
}

void formatSpeed(float knots, char* buf, size_t bufSize) {
    switch (unit) {
        case Distance::Km:
            snprintf(buf, bufSize, "%.0fkm/h", UnitMath::knotsToKmh(knots));
            break;
        case Distance::Miles:
            snprintf(buf, bufSize, "%.0fmph", UnitMath::knotsToMph(knots));
            break;
        default:
            snprintf(buf, bufSize, "%.0fkt", knots);
            break;
    }
}

const char* distSuffix() {
    switch (unit) {
        case Distance::NauticalMiles: return "nm";
        case Distance::Miles:         return "mi";
        default:                       return "km";
    }
}

const char* altSuffix() {
    return unitAlt == Altitude::Meters ? "m" : "ft";
}

const char* distFullName() {
    switch (unit) {
        case Distance::NauticalMiles: return "Nautical Miles";
        case Distance::Miles:         return "Statute Miles";
        default:                       return "Kilometers";
    }
}

const char* altFullName() {
    return unitAlt == Altitude::Meters ? "Meters" : "Feet";
}

}
