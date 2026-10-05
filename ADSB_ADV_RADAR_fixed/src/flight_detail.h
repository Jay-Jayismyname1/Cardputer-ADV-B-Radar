#pragma once
#include <Arduino.h>
#include "aircraft.h"

// Full-screen detail panel for a single selected aircraft - reached from
// the radar via Enter on a selected aircraft, Esc/backtick returns to the
// radar. Shows every field the radar HUD carries (in full, not the HUD's
// abbreviated 2-line form) plus lat/lon, which the HUD never shows at all.
// main.cpp's fetch/update cycle keeps running while this screen is open
// (not gated to ScreenMode::Radar), so calling render() every frame just
// reflects whatever's currently in the aircraft table - no separate
// polling/caching needed here.
namespace FlightDetail {

    void init();
    void render(const Aircraft& a);

}
