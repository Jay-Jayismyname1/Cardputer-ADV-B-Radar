#include "flight_detail.h"
#include "display_radar.h"
#include "units.h"
#include <M5Cardputer.h>
#include <cstring>

namespace FlightDetail {

namespace {
    // Two-column layout: label in the dim/dark-green HUD accent color,
    // value in white (or an override, e.g. red for an emergency squawk).
    // Column widths are sized for this board's small font at 240px wide,
    // not for arbitrarily long values - fine here since every value drawn
    // through this is a short fixed-format number/code.
    constexpr int16_t LEFT_LABEL_X  = 4;
    constexpr int16_t LEFT_VALUE_X  = 46;
    constexpr int16_t RIGHT_LABEL_X = 128;
    constexpr int16_t RIGHT_VALUE_X = 170;
    constexpr int16_t LINE_H = 12;

    void row2(M5Canvas& s, int16_t y, const char* labelL, const char* valueL,
              const char* labelR, const char* valueR,
              uint16_t colorL = TFT_WHITE, uint16_t colorR = TFT_WHITE) {
        s.setTextDatum(top_left);
        s.setTextColor(TFT_DARKGREEN);
        s.drawString(labelL, LEFT_LABEL_X, y);
        s.setTextColor(colorL);
        s.drawString(valueL, LEFT_VALUE_X, y);
        if (labelR) {
            s.setTextColor(TFT_DARKGREEN);
            s.drawString(labelR, RIGHT_LABEL_X, y);
            s.setTextColor(colorR);
            s.drawString(valueR, RIGHT_VALUE_X, y);
        }
    }
}

void init() {
    // Nothing to do - deliberately shares DisplayRadar's own sprite (see
    // DisplayRadar::sprite()'s comment) rather than allocating a 2nd
    // full-screen buffer of its own. DisplayRadar::init() (which creates
    // that sprite) already runs earlier in setup(), before this is called.
}

void render(const Aircraft& a) {
    M5Canvas& s = DisplayRadar::sprite();
    int16_t screenW = M5Cardputer.Display.width();
    int16_t screenH = M5Cardputer.Display.height();
    bool emergency = a.isEmergencySquawk();

    s.fillScreen(TFT_BLACK);

    // Header: callsign (falls back to hex if no callsign yet resolved),
    // or a blinking "EMERGENCY" banner in place of it while squawking.
    s.setTextDatum(top_center);
    if (emergency && (millis() / 300) % 2 == 0) {
        s.setTextColor(TFT_RED);
        s.drawString("EMERGENCY", screenW / 2, 3);
    } else {
        s.setTextColor(TFT_WHITE);
        s.drawString(a.callsign[0] ? a.callsign : (a.hex[0] ? a.hex : "-------"),
                      screenW / 2, 3);
    }
    s.drawFastHLine(0, 16, screenW, TFT_DARKGREEN);

    int16_t y = 20;

    row2(s, y, "CS", a.callsign[0] ? a.callsign : "-------", "HEX", a.hex[0] ? a.hex : "------");
    y += LINE_H;

    row2(s, y, "REG", a.reg[0] ? a.reg : "-------", "TYPE", a.typeCode[0] ? a.typeCode : "----");
    y += LINE_H;

    // Airline gets the full row width - names run longer than any of the
    // paired fields below and there's nothing sensible to pair it with.
    s.setTextDatum(top_left);
    s.setTextColor(TFT_DARKGREEN);
    s.drawString("AIRLINE", LEFT_LABEL_X, y);
    s.setTextColor(TFT_WHITE);
    s.drawString(a.airlineName[0] ? a.airlineName : "Unknown", LEFT_VALUE_X, y);
    y += LINE_H;

    char seatsBuf[12];
    if (a.estSeats > 0) snprintf(seatsBuf, sizeof(seatsBuf), "%u (est.)", a.estSeats);
    else strcpy(seatsBuf, "n/a");
    row2(s, y, "SQUAWK", a.squawk[0] ? a.squawk : "----", "SEATS", seatsBuf,
         emergency ? TFT_RED : TFT_WHITE);
    y += LINE_H;

    char altBuf[16], vsBuf[16];
    if (Units::currentAltitude() == Units::Altitude::Meters) {
        snprintf(altBuf, sizeof(altBuf), "%.0fm", UnitMath::ftToMeters((float)a.altBaroFt));
        snprintf(vsBuf, sizeof(vsBuf), "%+.0fm/min", UnitMath::ftToMeters((float)a.vertRateFtMin));
    } else {
        snprintf(altBuf, sizeof(altBuf), "%ldft", (long)a.altBaroFt);
        snprintf(vsBuf, sizeof(vsBuf), "%+dfpm", a.vertRateFtMin);
    }
    row2(s, y, "ALT", altBuf, "V/S", vsBuf);
    y += LINE_H;

    char spdBuf[16], hdgBuf[8];
    Units::formatSpeed(a.groundSpeedKt, spdBuf, sizeof(spdBuf));
    snprintf(hdgBuf, sizeof(hdgBuf), "%03.0f", a.headingDeg);
    row2(s, y, "SPD", spdBuf, "HDG", hdgBuf);
    y += LINE_H;

    char distBuf[16], brgBuf[8];
    Units::formatDistance(a.distanceKm, distBuf, sizeof(distBuf));
    snprintf(brgBuf, sizeof(brgBuf), "%03.0f", a.bearingDeg);
    row2(s, y, "DIST", distBuf, "BRG", brgBuf);
    y += LINE_H;

    // Coordinates - not shown anywhere on the HUD/radar, only here.
    char latBuf[16], lonBuf[16];
    snprintf(latBuf, sizeof(latBuf), "%.4f", (double)a.lat);
    snprintf(lonBuf, sizeof(lonBuf), "%.4f", (double)a.lon);
    row2(s, y, "LAT", latBuf, "LON", lonBuf);

    s.setTextDatum(bottom_center);
    s.setTextColor(TFT_DARKGREEN);
    s.drawString("ESC: back to radar", screenW / 2, screenH - 2);

    s.pushSprite(0, 0);
}

}
