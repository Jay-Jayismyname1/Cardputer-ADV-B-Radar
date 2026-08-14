#include <M5Cardputer.h>
#include <cstring>
#include "config.h"
#include "screen_mode.h"
#include "wifi_manager.h"
#include "adsb_client.h"
#include "aircraft_table.h"
#include "display_radar.h"
#include "proximity_alert.h"
#include "sd_storage.h"
#include "airline_lookup.h"
#include "neopixel_status.h"
#include "volume_control.h"
#include "wifi_setup_screen.h"
#include "settings_menu.h"
#include "location_manager.h"
#include "units.h"
#include "flight_logbook.h"

namespace {
    uint32_t lastFetchMs = 0;
    uint32_t lastFrameMs = 0;
    uint8_t selectedIndex = 0;
    int lastHttpCode = 0;

    // Coordinates the background fetch task was last kicked off with -
    // remembered here because postFetchUpdate() (bearing/distance calc)
    // needs to use the *same* home position the fetch was made for, not
    // whatever LocationManager reports at the moment the result happens
    // to arrive.
    double lastFetchHomeLat = 0.0, lastFetchHomeLon = 0.0;

    bool sdReady = false;
    bool timePrimed = false;

    // True from boot until the user has either connected to WiFi or
    // cancelled out of the forced setup screen (see setup()/loop() below).
    // While true, loop() shows the WiFi setup screen directly instead of
    // the Radar screen - no need to go through Settings -> WiFi first.
    bool forceWifiSetup = false;

    // --- Keyboard debounce ---
    // The Cardputer's key matrix chatters on contact, especially on combo
    // presses (Aa+letter for capitals), so isChange() can fire more than
    // once for a single physical keystroke and double-type characters.
    // We debounce by requiring the reported key state to actually differ
    // from the last *accepted* state, and by ignoring repeats that land
    // inside a short window after the last accepted event.
    constexpr uint32_t KEY_DEBOUNCE_MS = 40;
    uint32_t lastKeyAcceptMs = 0;
    uint8_t lastWordSig[16] = {0};
    uint8_t lastWordSigLen = 0;
    uint8_t lastHidSig[16] = {0};
    uint8_t lastHidSigLen = 0;
    bool lastFn = false, lastShift = false;

    bool sameSig(const uint8_t* a, uint8_t aLen, const uint8_t* b, uint8_t bLen) {
        if (aLen != bLen) return false;
        for (uint8_t i = 0; i < aLen; i++) if (a[i] != b[i]) return false;
        return true;
    }

    // Returns true if this key state should be processed (i.e. it's a new,
    // debounced event rather than matrix-scan chatter from the same
    // physical keystroke).
    bool acceptKeyEvent(const Keyboard_Class::KeysState& status) {
        uint8_t wordSig[16]; uint8_t wordLen = 0;
        for (auto c : status.word) {
            if (wordLen < sizeof(wordSig)) wordSig[wordLen++] = (uint8_t)c;
        }
        uint8_t hidSig[16]; uint8_t hidLen = 0;
        for (auto k : status.hid_keys) {
            if (hidLen < sizeof(hidSig)) hidSig[hidLen++] = k;
        }

        bool sameAsLast = sameSig(wordSig, wordLen, lastWordSig, lastWordSigLen) &&
                           sameSig(hidSig, hidLen, lastHidSig, lastHidSigLen) &&
                           status.fn == lastFn && status.shift == lastShift;

        uint32_t now = millis();
        if (sameAsLast && (now - lastKeyAcceptMs) < KEY_DEBOUNCE_MS) {
            return false; // chatter — same state repeated too quickly
        }

        memcpy(lastWordSig, wordSig, wordLen); lastWordSigLen = wordLen;
        memcpy(lastHidSig, hidSig, hidLen); lastHidSigLen = hidLen;
        lastFn = status.fn; lastShift = status.shift;
        lastKeyAcceptMs = now;
        return true;
    }

    ScreenMode screenMode = ScreenMode::Radar;

    // Currently unused by the radar HUD (replaced by the green/dark-green
    // status dot in DisplayRadar), kept for debug builds / future reuse.
    [[maybe_unused]] const char* wifiStatusLabel() {
        switch (WifiMgr::getState()) {
            case WifiMgr::State::Connected:      return "WIFI";
            case WifiMgr::State::Connecting:      return "...";
            case WifiMgr::State::Failed:          return "FAIL";
            case WifiMgr::State::NoCredentials:   return "NOCFG";
            default:                               return "IDLE";
        }
    }

    const char* locationStatusLabel() {
        switch (LocationManager::currentSource()) {
            case LocationManager::Source::GpsFix:        return "GPS";
            case LocationManager::Source::IpGeolocation: return "IP";
            case LocationManager::Source::Manual:        return "MANUAL";
            case LocationManager::Source::Persisted:     return "SAVED";
            default:                                      return "NOLOC";
        }
    }

    constexpr uint8_t HID_BACKSPACE = 0x2A;

    void handleKeyboardRadar(const Keyboard_Class::KeysState& status) {
        for (auto k : status.hid_keys) {
            if (k == HID_BACKSPACE) {
                screenMode = ScreenMode::Settings;
                SettingsMenu::onEnter();
                return;
            }
        }
        for (auto c : status.word) {
            if (status.fn) {
                if (c == 'w') {
                    screenMode = ScreenMode::Settings;
                    SettingsMenu::onEnter();
                    return;
                }
                if (c == '=') { VolumeControl::increase(); continue; }
                if (c == '-') { VolumeControl::decrease(); continue; }
            } else if (c == ' ') {
                DisplayRadar::cycleRange();
            }
        }

        if (status.tab) {
            uint8_t count = AircraftTable::validCount();
            if (count > 0) selectedIndex = (selectedIndex + 1) % count;
        }
    }
}

void setup() {
    // Needed for the Serial Monitor to show anything at all on this
    // ESP32-S3 board - without an explicit begin() here, Serial output
    // never reaches the USB port.
    Serial.begin(115200);

    auto cfg = M5.config();
    M5Cardputer.begin(cfg, true);

    M5Cardputer.Display.setRotation(1);
    M5Cardputer.Display.setBrightness(80);

    DisplayRadar::init();
    AircraftTable::init();
    ProximityAlert::init();
    WifiMgr::init();
    AirlineLookup::init();
    NeopixelStatus::init();
    VolumeControl::init();
    WifiSetupScreen::init();
    SettingsMenu::init();
    LocationManager::init();
    Units::init();
    FlightLogbook::init();

    // Starts the background FreeRTOS task (pinned to core 0) that
    // AdsbClient::requestFetch()/resultReady()/consumeResult() talk to
    // below. It just sits idle until the first requestFetch() call, so
    // it's safe to start this before WiFi is even connected.
    AdsbClient::startBackgroundTask();

    sdReady = SdStorage::init();
    if (sdReady) {
        SdStorage::seedDefaultDataFiles();
    }
    // If SdStorage::init() fails, the radar still runs fine — airline/seat
    // fields just stay blank, and the flight logbook simply stays off
    // (FlightLogbook::update() checks SdStorage::isMounted() itself).

    // "Kinectputer-style" SD credential loading
    if (WifiMgr::loadCredentialsFromSd()) {
        WifiMgr::beginAutoConnect(); // try every saved network in order, not just the first
    } else if (WifiMgr::hasStoredCredentials()) {
        WifiMgr::beginAutoConnect();
    } else {
        // No WiFi configured at all yet - no wifi.txt on the SD card, and
        // nothing saved from a previous manual setup either. Force the
        // WiFi setup screen straight away instead of silently sitting in
        // WifiMgr::State::NoCredentials in the background until the user
        // happens to open Settings themselves.
        forceWifiSetup = true;
        WifiSetupScreen::onEnter();
    }

    lastFrameMs = millis();
}

void loop() {
    M5Cardputer.update();

    if (forceWifiSetup) {
        // Route all input/rendering straight to the WiFi setup screen -
        // bypassing Radar and Settings entirely - until the user either
        // connects successfully or cancels out (Esc/`). See
        // wifi_setup_screen.cpp's handleWord()/isDone()/didConnectSucceed():
        // Esc/` already means "cancel, back to radar regardless of stage",
        // so cancelling here just means the radar screen starts in its
        // usual "not connected" state, same as if this feature didn't exist.
        bool keyChanged = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
        Keyboard_Class::KeysState status;
        if (keyChanged) {
            status = M5Cardputer.Keyboard.keysState();
            keyChanged = acceptKeyEvent(status); // same debounce as everywhere else
        }
        if (keyChanged) {
            char chars[16];
            uint8_t charCount = 0;
            for (auto c : status.word) {
                if (charCount < sizeof(chars)) chars[charCount++] = c;
            }
            uint8_t hidKeys[16];
            uint8_t hidCount = 0;
            for (auto k : status.hid_keys) {
                if (hidCount < sizeof(hidKeys)) hidKeys[hidCount++] = k;
            }
            WifiSetupScreen::handleWord(chars, charCount, status.fn, status.shift,
                                         hidKeys, hidCount);
        }

        WifiSetupScreen::render();

        if (WifiSetupScreen::isDone()) {
            forceWifiSetup = false; // connected or cancelled - either way, start the app now
        }

        uint32_t nowFws = millis();
        NeopixelStatus::tick(nowFws); // keep the idle LED animation alive here too
        lastFrameMs = nowFws;
        delay(16);
        return; // skip the rest of loop() this frame - the app hasn't started yet
    }

    bool keyChanged = M5Cardputer.Keyboard.isChange() && M5Cardputer.Keyboard.isPressed();
    Keyboard_Class::KeysState status;
    if (keyChanged) {
        status = M5Cardputer.Keyboard.keysState();
        keyChanged = acceptKeyEvent(status); // debounce matrix-scan chatter
    }

    if (screenMode == ScreenMode::Radar) {
        if (keyChanged) handleKeyboardRadar(status);
    } else { // Settings
        if (keyChanged) {
            char chars[16];
            uint8_t charCount = 0;
            for (auto c : status.word) {
                if (charCount < sizeof(chars)) chars[charCount++] = c;
            }
            uint8_t hidKeys[16];
            uint8_t hidCount = 0;
            for (auto k : status.hid_keys) {
                if (hidCount < sizeof(hidKeys)) hidKeys[hidCount++] = k;
            }
            SettingsMenu::handleWord(chars, charCount, status.fn, status.shift,
                                       hidKeys, hidCount);
        }
        if (SettingsMenu::isDone()) {
            screenMode = ScreenMode::Radar;
        }
    }

    WifiMgr::update();
    LocationManager::update();

    if (WifiMgr::getState() == WifiMgr::State::Connected && !timePrimed) {
        AdsbClient::primeTime();
        timePrimed = true;
    }
    if (WifiMgr::getState() == WifiMgr::State::Connected) {
        LocationManager::requestIpLookupIfNeeded(); // one-shot, no-ops after first success/attempt
    }

    uint32_t now = millis();

    // Kick off a new fetch on the interval tick. This used to call
    // AdsbClient::fetch() directly and block right here for the whole
    // TLS+HTTP+JSON-parse duration (a couple of seconds) - and because
    // DisplayRadar::tickSweep()/render() further down only run *after*
    // this point in the same loop() iteration, that blocking call is
    // exactly what froze the sweep/UI every FETCH_INTERVAL_MS. Now
    // requestFetch() just hands the request to a background task on core
    // 0 and returns immediately; loop() keeps ticking the sweep/rendering
    // every frame while the fetch runs in parallel. The result is picked
    // up below, whenever it's actually ready.
    if (screenMode == ScreenMode::Radar &&
        WifiMgr::getState() == WifiMgr::State::Connected &&
        now - lastFetchMs >= Config::FETCH_INTERVAL_MS) {

        if (LocationManager::currentSource() != LocationManager::Source::None) {
            double homeLat = 0.0, homeLon = 0.0;
            LocationManager::getHomeLocation(homeLat, homeLon);

            if (AdsbClient::requestFetch(homeLat, homeLon, DisplayRadar::currentRangeKm())) {
                lastFetchMs = now;
                lastFetchHomeLat = homeLat;
                lastFetchHomeLon = homeLon;
                NeopixelStatus::flashFetching();
            }
            // If requestFetch() returns false, the previous fetch is still
            // in flight (shouldn't normally happen at this interval) - we
            // simply leave lastFetchMs alone and try again next frame
            // instead of silently skipping a whole cycle.
        } else {
            lastFetchMs = now; // no fix yet - nothing to fetch, wait for the next tick

            // Un-gated from having a fix at all: even with no fix yet, this
            // still needs to run on the same cadence so headingBeaconArmed
            // gets set and the idle green heading-beacon flash has a chance
            // to fire instead of staying permanently unset/dark.
            NeopixelStatus::update(AircraftTable::raw(), AircraftTable::validCount(),
                                    AircraftTable::validCount() > 0 ? (int)selectedIndex : -1);
        }
    }

    // Pick up a finished background fetch's result, whenever it completes.
    // Checked every loop() iteration (a couple of atomic-bool reads - cheap)
    // rather than only on the interval tick, since the fetch is now
    // asynchronous and can finish at any point between ticks.
    if (AdsbClient::resultReady()) {
        auto result = AdsbClient::consumeResult(AircraftTable::raw(), AircraftTable::capacity());
        lastHttpCode = result.httpCode;

        if (result.ok) {
            AircraftTable::postFetchUpdate(lastFetchHomeLat, lastFetchHomeLon);
            if (selectedIndex >= AircraftTable::validCount()) selectedIndex = 0;
            FlightLogbook::update(AircraftTable::raw(), AircraftTable::validCount());
            ProximityAlert::checkAndAlert(AircraftTable::raw(), AircraftTable::validCount());
        }

        // Un-gated from `result.ok`, same as before: needs to run even on a
        // failed/empty fetch so headingBeaconArmed gets set and the idle
        // green heading-beacon flash isn't left permanently dark.
        NeopixelStatus::update(AircraftTable::raw(), AircraftTable::validCount(),
                                AircraftTable::validCount() > 0 ? (int)selectedIndex : -1);
    }

    // The audible proximity beep is owned by ProximityAlert - checkAndAlert()
    // (called above, once per fetch) queues up to one beep per newly
    // in-range aircraft (capped), and tick() here drains that queue one
    // beep at a time so several qualifying aircraft don't stall the render
    // loop with back-to-back blocking tones. NeoPixel flashing below is
    // purely visual - it must not also trigger a beep here, or an aircraft
    // inside the LED's fixed 2 km "visual" zone would beep even when it's
    // outside the user's configured thresholds.
    ProximityAlert::tick(now);
    NeopixelStatus::tick(now);

    if (screenMode == ScreenMode::Radar) {
        uint32_t deltaMs = now - lastFrameMs;
        DisplayRadar::tickSweep(deltaMs);
        NeopixelStatus::notifySweepAngle(DisplayRadar::currentSweepAngle());

        bool wifiConnected = (WifiMgr::getState() == WifiMgr::State::Connected);
        DisplayRadar::render(AircraftTable::raw(), AircraftTable::validCount(),
                              DisplayRadar::currentRangeKm(), selectedIndex,
                              wifiConnected, M5Cardputer.Power.getBatteryLevel(),
                              locationStatusLabel(), lastHttpCode);
    } else {
        SettingsMenu::render();
    }
    lastFrameMs = now;

    delay(16);
}