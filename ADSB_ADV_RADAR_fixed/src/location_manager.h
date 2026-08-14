#pragma once
#include <Arduino.h>

namespace LocationManager {

    enum class Source { GpsFix, IpGeolocation, Manual, Persisted, None };

    // The user's preference for where a non-GPS location comes from, once
    // GPS is off: Auto lets requestIpLookupIfNeeded() run as normal, Manual
    // means "only ever use the saved lat/lon, never overwrite it with an IP
    // lookup". This is distinct from `Source`, which just reports where the
    // *current* location actually came from.
    enum class SourcePref : uint8_t { Auto = 0, Manual = 1 };

    void init();
    void update();
    void requestIpLookupIfNeeded();
    void getHomeLocation(double& lat, double& lon);

    Source currentSource();
    void setManualLocation(double lat, double lon);
    void setGpsEnabled(bool enabled);
    bool isGpsEnabled();

    void setSourceOverride(SourcePref pref);
    SourcePref sourcePreference();

    // Manual coordinates are stored separately from the GPS/IP-derived
    // ("auto") location, so switching the source to IP and back to Manual
    // never loses what the user typed in - see setManualLocation()'s
    // implementation comment for why.
    bool hasManualLocation();
    void getManualLocation(double& lat, double& lon);

    void cycleGpsPinPair();
    const char* currentGpsPinLabel();

    bool hasGpsFix();
    uint32_t satelliteCount();

}
