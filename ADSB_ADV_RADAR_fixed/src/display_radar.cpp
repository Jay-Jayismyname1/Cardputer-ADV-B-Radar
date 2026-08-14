#include "display_radar.h"
#include "radar_math.h"
#include "config.h"
#include "units.h"
#include "adsb_client.h"
#include "proximity_alert.h"
#include "alert_filter.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace DisplayRadar {

namespace {
    M5Canvas radarSprite(&M5Cardputer.Display);
    int16_t centerX, centerY, outerRadiusPx;
    float sweepAngleDeg = 0.0f;
    constexpr float SWEEP_DEGREES_PER_SEC = 90.0f;

    Preferences prefs;
    uint8_t rangeIndex = Config::DEFAULT_RANGE_INDEX;
    uint16_t rotationDeg = 0; // 0 = North-up (default/unrotated)

    uint16_t altitudeColor(int32_t altFt) {
        if (altFt < Config::COLOR_LOW_ALT_THRESHOLD_FT) return TFT_YELLOW;
        if (altFt < Config::COLOR_MID_ALT_THRESHOLD_FT) return TFT_GREEN;
        return TFT_CYAN;
    }

    void drawRadarBase() {
        radarSprite.fillScreen(TFT_BLACK);


        for (int i = 1; i <= 3; i++) {
            radarSprite.drawCircle(centerX, centerY, (outerRadiusPx * i) / 3, TFT_DARKGREEN);
        }

        radarSprite.drawFastHLine(centerX - outerRadiusPx, centerY, outerRadiusPx * 2, TFT_DARKGREEN);
        radarSprite.drawFastVLine(centerX, centerY - outerRadiusPx, outerRadiusPx * 2, TFT_DARKGREEN);

        // Heading ring. N/S (top/bottom) were dropped so the circle can use
        // the vertical space they used to reserve (see the smaller margin
        // in init()) - screen is wider than it is tall, so the horizontal
        // E/W labels were never the binding constraint on radius anyway.
        radarSprite.setTextColor(TFT_DARKGREEN);
        radarSprite.setTextDatum(middle_center);

        if (rotationDeg == 0) {
            // Default, unrotated view - fixed E/W at the sides.
            constexpr struct { int deg; const char* label; } COMPASS_POINTS[] = {
                { 90, "E" }, { 270, "W" }
            };
            for (const auto& cp : COMPASS_POINTS) {
                double rad = cp.deg * DEG_TO_RAD;
                int16_t lx = centerX + (outerRadiusPx + 10) * sin(rad);
                int16_t ly = centerY - (outerRadiusPx + 10) * cos(rad);
                radarSprite.drawString(cp.label, lx, ly);
            }
        } else {
            // Rotated view - fixed E/W screen slots would generally no
            // longer land on an actual cardinal direction (e.g. a 135°
            // rotation puts True North at a diagonal, not aligned with any
            // fixed slot), so instead draw a single "N" marker at whichever
            // screen angle True North currently maps to - same convention
            // "heading up" GPS navigation UIs use, and the same
            // bearing-to-screen transform every aircraft blip already uses.
            float northScreenAngle = RadarMath::applyRotation(0.0f, (float)rotationDeg);
            double rad = northScreenAngle * DEG_TO_RAD;
            int16_t lx = centerX + (outerRadiusPx + 10) * sin(rad);
            int16_t ly = centerY - (outerRadiusPx + 10) * cos(rad);
            radarSprite.drawString("N", lx, ly);
        }
    }

    void drawSweep() {
        double rad = sweepAngleDeg * DEG_TO_RAD;
        int16_t x2 = centerX + outerRadiusPx * sin(rad);
        int16_t y2 = centerY - outerRadiusPx * cos(rad);
        radarSprite.drawLine(centerX, centerY, x2, y2, TFT_GREEN);
        for (int i = 1; i <= 3; i++) {
            double trailRad = (sweepAngleDeg - i * 6.0) * DEG_TO_RAD;
            int16_t tx = centerX + outerRadiusPx * sin(trailRad);
            int16_t ty = centerY - outerRadiusPx * cos(trailRad);
            radarSprite.drawLine(centerX, centerY, tx, ty, 0x0320 /* dim green */);
        }
    }

    // Draws a small triangle pointing along `headingDeg` (0 = up/North),
    // centered at (cx, cy). Replaces the plain dot blip so heading is
    // visible on the radar at a glance, not just in the HUD panel text.
    void drawHeadingArrow(int16_t cx, int16_t cy, float headingDeg, uint16_t color, bool selected) {
        int16_t len = selected ? 7 : 5;
        int16_t wing = selected ? 4 : 3;
        double rad = headingDeg * DEG_TO_RAD;
        double sinA = sin(rad), cosA = cos(rad);

        // Local-space triangle: nose forward, two back corners.
        float noseX = 0, noseY = -len;
        float leftX = -wing, leftY = len * 0.6f;
        float rightX = wing, rightY = len * 0.6f;

        auto rotate = [&](float lx, float ly, int16_t& outX, int16_t& outY) {
            outX = cx + static_cast<int16_t>(lx * cosA + ly * sinA);
            outY = cy + static_cast<int16_t>(-lx * sinA + ly * cosA);
        };

        int16_t nx, ny, lx2, ly2, rx2, ry2;
        rotate(noseX, noseY, nx, ny);
        rotate(leftX, leftY, lx2, ly2);
        rotate(rightX, rightY, rx2, ry2);

        radarSprite.fillTriangle(nx, ny, lx2, ly2, rx2, ry2, color);
        if (selected) {
            radarSprite.drawCircle(cx, cy, 9, TFT_WHITE);
        }
    }

    void drawAircraftBlip(const Aircraft& a, bool selected) {
        // Both the blip's position and its heading arrow are real-world
        // compass bearings, so both need the same display-rotation offset
        // applied - otherwise a plane actually flying north would still
        // point its arrow "up" even on a rotated display, which would look
        // wrong relative to everything else on screen.
        float screenBearing = RadarMath::applyRotation(a.bearingDeg, (float)rotationDeg);
        float screenHeading = RadarMath::applyRotation(a.headingDeg, (float)rotationDeg);

        RadarMath::PolarCoord polar{a.distanceKm, screenBearing};
        auto pt = RadarMath::toScreen(polar, centerX, centerY, outerRadiusPx,
                                       Config::RANGE_STEPS_KM[rangeIndex]);

        bool emergency = a.isEmergencySquawk();
        uint16_t color = emergency ? TFT_RED : altitudeColor(a.altBaroFt);
        drawHeadingArrow(pt.x, pt.y, screenHeading, color, selected);

        if (emergency) {
            // Pulsing red ring so an emergency aircraft stands out at a
            // glance, distinct from the plain white "selected" ring.
            bool pulseOn = (millis() / 300) % 2 == 0;
            if (pulseOn) {
                radarSprite.drawCircle(pt.x, pt.y, 11, TFT_RED);
            }
        }

        // Callsign label — small, offset above the blip
        radarSprite.setTextColor(color);
        radarSprite.setTextDatum(bottom_center);
        radarSprite.setTextSize(1);
        const char* label = a.callsign[0] ? a.callsign : a.hex;
        radarSprite.drawString(label, pt.x, pt.y - 9);

        if (emergency) {
            // Squawk code shown right under the blip, so it reads as
            // "why is this red" (7500/7600/7700) rather than just "red".
            radarSprite.setTextColor(TFT_RED);
            radarSprite.setTextDatum(top_center);
            radarSprite.drawString(a.squawk, pt.x, pt.y + 9);
        }
    }

    // Returns white/orange/red based on battery percentage thresholds.
    uint16_t batteryColor(int pct) {
        if (pct > 50) return TFT_WHITE;
        if (pct > 20) return 0xFD20 /* orange */;
        return TFT_RED;
    }

    void drawHudPanel(const Aircraft* list, uint8_t count, uint8_t selectedIndex,
                       bool wifiConnected, int batteryPct,
                       const char* locationLabel, int lastHttpCode) {
        // Bottom strip: detail on the closest / selected aircraft
        int16_t panelY = M5Cardputer.Display.height() - 42;
        radarSprite.fillRect(0, panelY, M5Cardputer.Display.width(), 42, TFT_BLACK);
        radarSprite.drawFastHLine(0, panelY, M5Cardputer.Display.width(), TFT_DARKGREEN);

        radarSprite.setTextDatum(top_left);
        radarSprite.setTextColor(TFT_WHITE);

        if (count == 0 || selectedIndex >= count) {
            radarSprite.drawString("No traffic in range", 4, panelY + 4);
        } else {
            const Aircraft& a = list[selectedIndex];
            bool emergency = a.isEmergencySquawk();

            char line1[64];
            if (emergency) {
                snprintf(line1, sizeof(line1), "EMERGENCY %s  SQUAWK %s",
                         a.callsign[0] ? a.callsign : a.hex, a.squawk);
            } else {
                snprintf(line1, sizeof(line1), "%s  %s  %s",
                         a.callsign[0] ? a.callsign : "-------",
                         a.reg[0] ? a.reg : "REG?",
                         a.airlineName[0] ? a.airlineName : "");
            }
            radarSprite.setTextColor(emergency ? TFT_RED : TFT_WHITE);
            radarSprite.drawString(line1, 4, panelY + 4);
            radarSprite.setTextColor(TFT_WHITE); // reset for the lines below

            char distBuf[12];
            Units::formatDistance(a.distanceKm, distBuf, sizeof(distBuf));
            char altBuf[16];
            // Vertical speed follows the same ft/m choice as altitude,
            // rather than staying hardcoded to ft/min regardless of it.
            char vsBuf[16];
            if (Units::currentAltitude() == Units::Altitude::Meters) {
                snprintf(altBuf, sizeof(altBuf), "%.0fm", UnitMath::ftToMeters((float)a.altBaroFt));
                snprintf(vsBuf, sizeof(vsBuf), "%+.0fm/min", UnitMath::ftToMeters((float)a.vertRateFtMin));
            } else {
                snprintf(altBuf, sizeof(altBuf), "%ldft", (long)a.altBaroFt);
                snprintf(vsBuf, sizeof(vsBuf), "%+dfpm", a.vertRateFtMin);
            }
            char line2[64];
            snprintf(line2, sizeof(line2), "%s  ALT %s  VS %s  HDG %03.0f",
                     distBuf, altBuf, vsBuf, a.headingDeg);
            radarSprite.drawString(line2, 4, panelY + 16);

            // Ground speed follows the same Distance unit as everything
            // else (km/h, mph, or kt) rather than being its own separate
            // setting - see Units::formatSpeed().
            char spdBuf[16];
            Units::formatSpeed(a.groundSpeedKt, spdBuf, sizeof(spdBuf));
            char line3[64];
            if (a.estSeats > 0) {
                snprintf(line3, sizeof(line3), "%s  SPD %s  ~%u seats (est.)",
                         a.typeCode[0] ? a.typeCode : "TYPE?", spdBuf, a.estSeats);
            } else {
                snprintf(line3, sizeof(line3), "%s  SPD %s  seats: n/a",
                         a.typeCode[0] ? a.typeCode : "TYPE?", spdBuf);
            }
            radarSprite.drawString(line3, 4, panelY + 28);
        }

        int16_t screenW = M5Cardputer.Display.width();

        // Top-left: green WiFi status dot, GPS/location method label to its
        // right, and the current ADS-B data source abbreviated on the line
        // below (FI/LOL/LIVE/CSTM) - at a glance which feed is in use
        // without having to open Settings.
        constexpr int16_t dotX = 8, dotY = 8, dotR = 4;
        radarSprite.fillCircle(dotX, dotY, dotR, wifiConnected ? TFT_GREEN : TFT_DARKGREEN);
        radarSprite.setTextDatum(middle_left);
        radarSprite.setTextColor(TFT_WHITE);
        radarSprite.drawString(locationLabel, dotX + dotR + 6, dotY);
        radarSprite.setTextColor(TFT_DARKGREEN);
        radarSprite.drawString(AdsbClient::currentDataSourceShortLabel(), dotX + dotR + 6, dotY + 12);

        // Top-center: blinking "EMERGENCY" banner whenever any tracked
        // aircraft is squawking 7500/7600/7700 - shown regardless of
        // which aircraft is currently selected, so it isn't missed just
        // because you had Tab'd over to look at something else.
        bool anyEmergency = false;
        for (uint8_t i = 0; i < count; i++) {
            if (list[i].valid && list[i].isEmergencySquawk()) { anyEmergency = true; break; }
        }
        if (anyEmergency && (millis() / 300) % 2 == 0) {
            radarSprite.setTextDatum(top_center);
            radarSprite.setTextColor(TFT_RED);
            radarSprite.drawString("EMERGENCY", screenW / 2, 2);
        }

        // Top-right: battery pill, aircraft count to its left.
        char battLabel[8];
        snprintf(battLabel, sizeof(battLabel), "%d%%", batteryPct);
        int16_t pillW = radarSprite.textWidth(battLabel) + 12;
        int16_t pillH = 14;
        int16_t pillX = screenW - 6 - pillW;
        int16_t pillY = 2;
        uint16_t battCol = batteryColor(batteryPct);
        radarSprite.drawRoundRect(pillX, pillY, pillW, pillH, pillH / 2, battCol);
        radarSprite.setTextDatum(middle_center);
        radarSprite.setTextColor(battCol);
        radarSprite.drawString(battLabel, pillX + pillW / 2, pillY + pillH / 2);

        char countLabel[8];
        snprintf(countLabel, sizeof(countLabel), "[%u]", count);
        radarSprite.setTextDatum(middle_right);
        radarSprite.setTextColor(TFT_WHITE);
        radarSprite.drawString(countLabel, pillX - 6, pillY + pillH / 2);

        // Below the total count: how many of those aircraft currently
        // satisfy the proximity beep predicate (same distance/height
        // thresholds ProximityAlert::checkAndAlert() uses, or emergency) -
        // a live "how many would beep right now" count, not just the ones
        // that were newly in range on the last scan.
        uint8_t inBeepRangeCount = 0;
        for (uint8_t i = 0; i < count; i++) {
            if (!list[i].valid) continue;
            if (AlertFilter::shouldAlert(list[i].distanceKm, static_cast<float>(list[i].altBaroFt),
                                          list[i].isEmergencySquawk(),
                                          ProximityAlert::getThresholdKm(),
                                          ProximityAlert::getThresholdAltFt())) {
                inBeepRangeCount++;
            }
        }
        char inRangeLabel[8];
        snprintf(inRangeLabel, sizeof(inRangeLabel), "(%u)", inBeepRangeCount);
        radarSprite.setTextColor(TFT_DARKGREEN);
        radarSprite.drawString(inRangeLabel, pillX - 6, pillY + pillH / 2 + 12);

        // Range scale — bottom-left corner, just above the HUD panel divider.
        char rangeLabel[16];
        Units::formatDistance(Config::RANGE_STEPS_KM[rangeIndex], rangeLabel, sizeof(rangeLabel));
        radarSprite.setTextDatum(bottom_left);
        radarSprite.setTextColor(TFT_DARKGREEN);
        radarSprite.drawString(rangeLabel, 4, panelY - 4);
    }
}

void init() {
    centerX = M5Cardputer.Display.width() / 2;
    centerY = (M5Cardputer.Display.height() - 42) / 2; // leave room for HUD strip at bottom
    // centerY (not centerX) is the binding constraint here on this wide,
    // short display - it used to need a bigger margin to leave room for
    // the N/S labels drawn directly above/below the circle. Those are
    // gone now (see drawRadarBase() - only E/W remain, on the horizontal
    // axis where centerX leaves far more headroom), so the margin only
    // needs to keep the ring off the very top/bottom edge.
    constexpr int16_t RADAR_MARGIN_PX = 4;
    outerRadiusPx = min(centerX, centerY) - RADAR_MARGIN_PX;
    radarSprite.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    radarSprite.setTextFont(1);

    // Same "adsb_radar" NVS namespace as every other setting - this one
    // was previously missed, so the zoom level silently reset to
    // DEFAULT_RANGE_INDEX (25km) on every reboot instead of persisting.
    prefs.begin("adsb_radar", false);
    rangeIndex = prefs.getUChar("rangeIdx", Config::DEFAULT_RANGE_INDEX);
    if (rangeIndex >= Config::RANGE_STEP_COUNT) rangeIndex = Config::DEFAULT_RANGE_INDEX;

    rotationDeg = prefs.getUShort("radarRotDeg", 0);
    rotationDeg %= 360;
}

void tickSweep(uint32_t deltaMs) {
    sweepAngleDeg += SWEEP_DEGREES_PER_SEC * (deltaMs / 1000.0f);
    if (sweepAngleDeg >= 360.0f) sweepAngleDeg -= 360.0f;
}

void cycleRange() {
    rangeIndex = (rangeIndex + 1) % Config::RANGE_STEP_COUNT;
    prefs.putUChar("rangeIdx", rangeIndex);
}

float currentRangeKm() {
    return Config::RANGE_STEPS_KM[rangeIndex];
}

void setRotationDeg(uint16_t deg) {
    rotationDeg = deg % 360;
    prefs.putUShort("radarRotDeg", rotationDeg);
}

void cycleRotation(int16_t stepDeg) {
    // Wrap into [0, 360) - stepDeg may be negative (rotating the other way).
    int32_t next = (static_cast<int32_t>(rotationDeg) + stepDeg) % 360;
    if (next < 0) next += 360;
    setRotationDeg(static_cast<uint16_t>(next));
}

uint16_t currentRotationDeg() {
    return rotationDeg;
}

float currentSweepAngle() {
    return sweepAngleDeg;
}

void render(const Aircraft* aircraftList, uint8_t count,
            float rangeKm, uint8_t selectedIndex,
            bool wifiConnected, int batteryPct,
            const char* locationLabel, int lastHttpCode) {
    drawRadarBase();
    drawSweep();

    for (uint8_t i = 0; i < count; i++) {
        if (!aircraftList[i].valid) continue;
        drawAircraftBlip(aircraftList[i], i == selectedIndex);
    }

    drawHudPanel(aircraftList, count, selectedIndex, wifiConnected, batteryPct,
                 locationLabel, lastHttpCode);

    radarSprite.pushSprite(0, 0);
}

}