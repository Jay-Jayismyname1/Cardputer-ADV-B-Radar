#include "neopixel_status.h"
#include "config.h"
#include <Adafruit_NeoPixel.h>
#include <Preferences.h>
#include <M5Cardputer.h>

namespace NeopixelStatus {

namespace {
    Adafruit_NeoPixel pixel(Config::NEOPIXEL_COUNT, Config::NEOPIXEL_PIN,
                             NEO_GRB + NEO_KHZ800);
    Preferences prefs;

    constexpr uint32_t IDLE_OFF    = 0x000000;
    constexpr uint32_t WHITE_COLOR = 0xFFFFFF;
    constexpr uint32_t BLUE_COLOR  = 0x0000FF;
    constexpr uint32_t YELLOW_COLOR= 0xFFFF00;
    constexpr uint32_t AMBER_COLOR = 0xFFB000;
    constexpr uint32_t GREEN_COLOR = 0x00FF00;
    constexpr uint32_t RED_COLOR   = 0xFF0000;

    // Emergency squawk (7500/7600/7700) flashes noticeably faster than the
    // normal proximity flash, so it reads as "urgent" rather than just
    // another zone color.
    constexpr uint32_t EMERGENCY_FLASH_INTERVAL_MS = 200;

    // Default raised from 60 -> 90: at low brightness percentages this
    // NeoPixel's actual output was low enough to look "not lighting up"
    // on some panels/lighting conditions. Still adjustable in Settings.
    uint8_t brightnessPercent = 90;

    uint32_t baseColor = IDLE_OFF;
    bool flashing = false;       // whether the current zone wants a flash
    bool flashOn = false;        // current flash phase
    uint32_t lastFlashToggleMs = 0;

    // True when at least one currently-tracked aircraft is squawking an
    // emergency code. Takes priority over the proximity zone colors and
    // the idle heading beacon below - a nearby-but-normal aircraft is far
    // less important than a distant one in genuine distress.
    bool emergencyActive = false;

    // Idle-state heading beacon: when no aircraft are within alert range,
    // the LED instead flashes green every NORTH_FLASH_INTERVAL_MS at the
    // moment the radar sweep crosses 0 degrees (North), so the LED still
    // has a purpose instead of sitting dark. Proximity alerts always take
    // priority over this - it only runs when the zone logic below found
    // nothing to report.
    bool headingBeaconArmed = false;
    constexpr uint32_t NORTH_FLASH_ON_MS = 120;
    uint32_t northFlashOffAtMs = 0;
    bool northFlashActive = false;

    void applyPixel(uint32_t color) {
        pixel.setPixelColor(0, color);
        pixel.show();
    }
}

void init() {
    prefs.begin("adsb_radar", false);
    brightnessPercent = prefs.getUChar("ledBright", 90);

    // Cardputer-ADV's RGB LED rail power-enable (GPIO38) is NOT actually
    // independent of the LCD backlight on this hardware - M5GFX drives
    // the backlight PWM on that exact same pin. A plain pinMode()+
    // digitalWrite() here (as this used to do) detaches the pin from the
    // display's LEDC channel for the rest of the session: M5Cardputer.
    // Display.setBrightness() keeps "succeeding" afterward (it only
    // writes a duty register) but the physical pin never moves again -
    // which is why the brightness slider stopped doing anything the
    // moment this ran. Driving the same rail through the display's own
    // brightness call instead keeps GPIO38 under LEDC control, so
    // SettingsMenu::init() (which runs right after this and restores the
    // user's saved percentage) can still take over the pin correctly.
    M5Cardputer.Display.setBrightness(255);

    pixel.begin();
    pixel.setBrightness((brightnessPercent * 255) / 100);
    pixel.show(); // off by default until first update()
}

void flashFetching() {
    applyPixel(WHITE_COLOR);
}

void setOff() {
    baseColor = IDLE_OFF;
    flashing = false;
    flashOn = false;
    emergencyActive = false;
    headingBeaconArmed = false;
    northFlashActive = false;
    applyPixel(IDLE_OFF);
}

void update(const Aircraft* table, uint8_t count, int selectedIndex) {
    // Emergency squawk check takes priority over everything below - scans
    // every valid aircraft, not just the closest/selected one, since an
    // emergency anywhere in range matters more than proximity ranking.
    bool emergency = false;
    for (uint8_t i = 0; i < count; i++) {
        if (table[i].valid && table[i].isEmergencySquawk()) { emergency = true; break; }
    }

    headingBeaconArmed = false;
    emergencyActive = emergency;

    if (emergency) {
        baseColor = RED_COLOR;
        flashing = true;
    } else {
        bool any = false;
        float closestKm = 1e9f;

        if (selectedIndex >= 0 && selectedIndex < count && table[selectedIndex].valid) {
            any = true;
            closestKm = table[selectedIndex].distanceKm;
        } else {
            for (uint8_t i = 0; i < count; i++) {
                if (!table[i].valid) continue;
                any = true;
                if (table[i].distanceKm < closestKm) closestKm = table[i].distanceKm;
            }
        }

        if (!any) {
            baseColor = IDLE_OFF;
            flashing = false;
            headingBeaconArmed = true; // nothing to report -> LED free for the heading beacon
        } else if (closestKm <= Config::ZONE_VISUAL_KM) {
            baseColor = GREEN_COLOR;
            flashing = true;
        } else if (closestKm <= Config::ZONE_AMBER_KM) {
            baseColor = AMBER_COLOR;
            flashing = false;
        } else if (closestKm <= Config::ZONE_YELLOW_KM) {
            baseColor = YELLOW_COLOR;
            flashing = false;
        } else if (closestKm <= Config::ZONE_BLUE_KM) {
            baseColor = BLUE_COLOR;
            flashing = false;
        } else {
            baseColor = IDLE_OFF;
            flashing = false;
            headingBeaconArmed = true; // out of every zone -> also idle, beacon can run
        }
    }

    if (!flashing) {
        flashOn = false;
        if (!headingBeaconArmed || !northFlashActive) {
            applyPixel(headingBeaconArmed ? IDLE_OFF : baseColor);
        }
    }
}

// Called once per frame with the current sweep angle so the beacon can
// fire exactly as the sweep line crosses North (0 degrees).
void notifySweepAngle(float sweepAngleDeg) {
    static float lastAngle = 0.0f;
    bool crossedNorth = (lastAngle > 300.0f && sweepAngleDeg < 60.0f); // wrapped past 360/0
    lastAngle = sweepAngleDeg;

    if (!headingBeaconArmed || flashing) return; // proximity/emergency flash owns the LED
    if (!crossedNorth) return;

    northFlashActive = true;
    northFlashOffAtMs = millis() + NORTH_FLASH_ON_MS;
    applyPixel(GREEN_COLOR);
    // Deliberately silent - this fires roughly every sweep revolution
    // (4s at 90 deg/sec), so chirping here would get noisy fast. The
    // audible beep is owned entirely by ProximityAlert::checkAndAlert().
}

void tick(uint32_t now) {
    if (northFlashActive) {
        if (now >= northFlashOffAtMs) {
            northFlashActive = false;
            if (headingBeaconArmed && !flashing) applyPixel(IDLE_OFF);
        }
    }

    if (!flashing) return;

    uint32_t interval = emergencyActive ? EMERGENCY_FLASH_INTERVAL_MS : Config::FLASH_INTERVAL_MS;
    if (now - lastFlashToggleMs >= interval) {
        lastFlashToggleMs = now;
        flashOn = !flashOn;
        applyPixel(flashOn ? baseColor : IDLE_OFF);
    }
}

void setBrightnessPercent(uint8_t percent) {
    if (percent > 100) percent = 100;
    brightnessPercent = percent;
    prefs.putUChar("ledBright", brightnessPercent);
    pixel.setBrightness((brightnessPercent * 255) / 100);
    applyPixel(flashing ? (flashOn ? baseColor : IDLE_OFF) : baseColor);
}

uint8_t getBrightnessPercent() {
    return brightnessPercent;
}

}