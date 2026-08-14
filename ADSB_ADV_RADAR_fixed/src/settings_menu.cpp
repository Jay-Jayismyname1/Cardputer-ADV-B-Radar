#include "settings_menu.h"
#include "wifi_setup_screen.h"
#include "wifi_manager.h"
#include "volume_control.h"
#include "neopixel_status.h"
#include "proximity_alert.h"
#include "location_manager.h"
#include "units.h"
#include "flight_logbook.h"
#include "display_radar.h"
#include "adsb_client.h"
#include "config.h"
#include <M5Cardputer.h>
#include <Preferences.h>

namespace SettingsMenu {

namespace {
    constexpr uint8_t HID_ENTER = 0x28;
    // NOTE: M5Cardputer's keyboard driver (lib version pinned in
    // platformio.ini) never reports the physical Esc key through
    // status.hid_keys with code 0x29 on this board — it only ever shows up
    // as the '`' character in status.word. HID_ESC is kept here as a
    // defensive fallback in case a future library version does report it,
    // but '`' is the actual, reliably-working "close/cancel" key and must
    // stay the primary check everywhere in this file.
    constexpr uint8_t HID_ESC   = 0x29;
    constexpr uint8_t HID_BACKSPACE = 0x2A; // physical "Del" key
    constexpr uint16_t TONE_NAV_HZ     = 1400;
    constexpr uint16_t TONE_NAV_MS     = 25;
    constexpr uint16_t TONE_ADJUST_HZ  = 1000;
    constexpr uint16_t TONE_ADJUST_MS  = 25;
    constexpr uint16_t TONE_CONFIRM_HZ = 2000;
    constexpr uint16_t TONE_CONFIRM_MS = 45;
    constexpr uint16_t TONE_CLOSE_HZ   = 700;
    constexpr uint16_t TONE_CLOSE_MS   = 45;

    void tone(uint16_t hz, uint16_t ms) { M5Cardputer.Speaker.tone(hz, ms); }

    // Every sub-screen/entry-mode in this file re-checks the same handful
    // of control keys out of the raw hidKeys/chars arrays handleWord() gets
    // each call. Scanning all four unconditionally is a handful of byte
    // comparisons over at most ~16 elements either way, so one shared scan
    // is simpler than parameterizing which flags each caller actually needs.
    struct Keys {
        bool enter = false;
        bool esc = false;
        bool backspace = false;
        bool backtick = false; // the actual "close/cancel" key on this board - see HID_ESC's note above
    };

    Keys scanKeys(const uint8_t* hidKeys, uint8_t hidKeyCount, const char* chars, uint8_t count) {
        Keys k;
        for (uint8_t i = 0; i < hidKeyCount; i++) {
            if (hidKeys[i] == HID_ENTER) k.enter = true;
            if (hidKeys[i] == HID_ESC) k.esc = true;
            if (hidKeys[i] == HID_BACKSPACE) k.backspace = true;
        }
        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == '`') k.backtick = true;
        }
        return k;
    }

    enum class Item : uint8_t { Wifi = 0, Location, DataSource, Units, ProxBeep, DisplayBrightness, RadarRotation, LedBrightness, Volume, Logbook, Count };
    Item selected = Item::Wifi;

    // How many item rows are scrolled past the top of the visible list -
    // the item list is taller than the small Cardputer screen, so render()
    // keeps the currently selected row scrolled into view using this offset.
    uint8_t scrollOffset = 0;

    bool inWifiSubscreen = false;
    bool inWifiManage = false;
    uint8_t wifiManageSelected = 0; // 0..savedCount-1 = saved networks, savedCount = "Add network"

    bool inManualLocationEntry = false;
    uint8_t manualEntryField = 0; // 0 = lat, 1 = lon
    char manualLatBuf[16] = "";
    char manualLonBuf[16] = "";
    uint8_t manualLatLen = 0;
    uint8_t manualLonLen = 0;

    // Radar rotation: an inline slider on the main list (like Display/LED
    // brightness) for quick coarse steps, plus an exact-value entry mode
    // (same "m" convention as Location's manual lat/lon) for precise input.
    constexpr int16_t ROTATION_STEP_DEG = 15;
    bool inRotationManualEntry = false;
    char rotationBuf[4] = ""; // "0".."359"
    uint8_t rotationLen = 0;

    // Location sub-screen: row 0 is always the GPS on/off toggle. Row 1 is
    // context-sensitive - the GPS pin-pair cycler while GPS is on, or the
    // IP/Manual source toggle while GPS is off - but it's always exactly
    // one row, so a plain 2-row list with ;/. navigation covers both.
    bool inLocationSubscreen = false;
    uint8_t locationSubSelected = 0;
    constexpr uint8_t LOCATION_SUB_ROWS = 2;

    // Data Source sub-screen: Source cycle (adsb.fi/adsb.lol/airplanes.live/
    // Custom), then a Test Connection action always present at the end.
    // Host/Port/Scheme only exist as rows at all while Custom is selected -
    // they're meaningless for a hosted API, so they're hidden rather than
    // just harmlessly editable. Row 0 is always Source; the last row
    // (dataSourceRowCount()-1) is always Test - see dataSourceRowCount()/
    // isCustomSelected() below for how the row count/meaning flexes.
    bool inDataSourceSubscreen = false;
    uint8_t dataSourceSubSelected = 0;
    constexpr uint8_t DATASOURCE_ROW_SOURCE = 0;
    constexpr uint8_t DATASOURCE_ROW_HOST   = 1; // Custom only
    constexpr uint8_t DATASOURCE_ROW_PORT   = 2; // Custom only
    constexpr uint8_t DATASOURCE_ROW_SCHEME = 3; // Custom only

    // Host is free-text (hostname or IP) - same printable-ASCII entry
    // pattern already used for the WiFi password field in
    // wifi_setup_screen.cpp, rather than the digits-only pattern used for
    // manual lat/lon/rotation entry.
    bool inHostTextEntry = false;
    char hostBuf[Config::TAR1090_HOST_MAX_LEN] = "";
    uint8_t hostLen = 0;

    // Port is digits-only, same convention as the other numeric-entry
    // screens.
    bool inPortEntry = false;
    char portBuf[6] = ""; // up to 65535
    uint8_t portLen = 0;

    // Test Connection result, shown below the row list until the next test
    // (or until the sub-screen is left). Idle means "never tested this
    // session".
    enum class ConnTestState : uint8_t { Idle, Testing, Ok, Failed };
    ConnTestState connTestState = ConnTestState::Idle;
    char connTestMsg[40] = "";

    const char* dataSourceLabel(AdsbClient::DataSource src) {
        switch (src) {
            case AdsbClient::DataSource::AdsbLol:       return "adsb.lol";
            case AdsbClient::DataSource::AirplanesLive: return "airplanes.live";
            case AdsbClient::DataSource::CustomTar1090: return "Custom";
            default:                                     return "adsb.fi";
        }
    }

    AdsbClient::DataSource nextDataSource(AdsbClient::DataSource src) {
        switch (src) {
            case AdsbClient::DataSource::AdsbFi:        return AdsbClient::DataSource::AdsbLol;
            case AdsbClient::DataSource::AdsbLol:       return AdsbClient::DataSource::AirplanesLive;
            case AdsbClient::DataSource::AirplanesLive: return AdsbClient::DataSource::CustomTar1090;
            default:                                     return AdsbClient::DataSource::AdsbFi;
        }
    }

    bool isCustomSelected() {
        return AdsbClient::currentDataSource() == AdsbClient::DataSource::CustomTar1090;
    }

    // 5 rows (Source, Host, Port, Scheme, Test) while Custom is selected;
    // just 2 (Source, Test) for any hosted API, since Host/Port/Scheme are
    // meaningless there. The Test row is always the last row, whichever
    // that is - see dataSourceTestRow() below.
    uint8_t dataSourceRowCount() {
        return isCustomSelected() ? 5 : 2;
    }

    uint8_t dataSourceTestRow() {
        return dataSourceRowCount() - 1;
    }

    // Units sub-screen: distance + altitude, each a simple cycle.
    bool inUnitsSubscreen = false;
    uint8_t unitsSubSelected = 0;
    constexpr uint8_t UNITS_SUB_ROWS = 2;

    // Proximity Beep sub-screen: max distance slider, max height slider,
    // beep on/off toggle.
    bool inProxBeepSubscreen = false;
    uint8_t proxBeepSubSelected = 0;
    constexpr uint8_t PROXBEEP_SUB_ROWS = 3;
    constexpr float PROX_DIST_MIN_KM  = 1.0f;
    constexpr float PROX_DIST_MAX_KM  = 100.0f;
    constexpr float PROX_DIST_STEP_KM = 1.0f;
    constexpr float PROX_ALT_MIN_FT   = 500.0f;
    constexpr float PROX_ALT_MAX_FT   = 50000.0f;
    constexpr float PROX_ALT_STEP_FT  = 500.0f;

    bool done = false;

    Preferences prefs;
    uint8_t displayBrightnessPercent = 80;
    M5Canvas settingsSprite(&M5Cardputer.Display);
    bool spriteReady = false;

    void applyDisplayBrightness() {
        M5Cardputer.Display.setBrightness((displayBrightnessPercent * 255) / 100);
    }

    void startManualEntry() {
        double lat = 0.0, lon = 0.0;
        LocationManager::getHomeLocation(lat, lon);
        if (lat != 0.0 || lon != 0.0) {
            manualLatLen = snprintf(manualLatBuf, sizeof(manualLatBuf), "%.4f", lat);
            manualLonLen = snprintf(manualLonBuf, sizeof(manualLonBuf), "%.4f", lon);
        } else {
            manualLatBuf[0] = '\0'; manualLatLen = 0;
            manualLonBuf[0] = '\0'; manualLonLen = 0;
        }
        manualEntryField = 0;
        inManualLocationEntry = true;
    }

    void startRotationManualEntry() {
        rotationLen = snprintf(rotationBuf, sizeof(rotationBuf), "%u",
                                 DisplayRadar::currentRotationDeg());
        inRotationManualEntry = true;
    }

    // Shared by both the main list's Location row and the Location
    // sub-screen's Source row - "manual" reflects the user's persisted
    // preference (LocationManager::SourcePref), not just whatever the
    // current fix happens to be, so it stays stable across boots.
    const char* locationSourceLabel() {
        return (LocationManager::sourcePreference() == LocationManager::SourcePref::Manual)
                   ? "manual" : "IP";
    }

    void startHostEntry() {
        strncpy(hostBuf, AdsbClient::customHost(), sizeof(hostBuf) - 1);
        hostBuf[sizeof(hostBuf) - 1] = '\0';
        hostLen = strlen(hostBuf);
        inHostTextEntry = true;
    }

    void startPortEntry() {
        portLen = snprintf(portBuf, sizeof(portBuf), "%u", AdsbClient::customPort());
        inPortEntry = true;
    }
}

void init() {
    prefs.begin("adsb_radar", false);
    displayBrightnessPercent = prefs.getUChar("dispBright", 80);

    // setup() sets a hardcoded 80% before this runs (display needs to be
    // usable before prefs/SD are ready), so the saved percentage was never
    // actually applied — brightness silently reset to 80 on every boot no
    // matter what had been saved. Apply it now that we've loaded it.
    applyDisplayBrightness();

    settingsSprite.createSprite(M5Cardputer.Display.width(), M5Cardputer.Display.height());
    // Classic 6x8 GLCD font (font 1) - blocky, monospaced, crisp at any
    // integer scale (no anti-aliasing blur like the proportional fonts).
    // This is the same style used by Bruce/Poseidon-type Cardputer
    // firmware. Scaled 2x for readability while staying pixel-sharp.
    settingsSprite.setTextFont(1);
    settingsSprite.setTextSize(1);
    spriteReady = true;
}

void onEnter() {
    selected = Item::Wifi;
    scrollOffset = 0;
    inWifiSubscreen = false;
    inWifiManage = false;
    inManualLocationEntry = false;
    inLocationSubscreen = false;
    inDataSourceSubscreen = false;
    inHostTextEntry = false;
    inPortEntry = false;
    inUnitsSubscreen = false;
    inProxBeepSubscreen = false;
    inRotationManualEntry = false;
    connTestState = ConnTestState::Idle;
    done = false;
}

bool isDone() { return done; }

void handleWord(const char* chars, uint8_t count, bool fnHeld, bool shiftHeld,
                 const uint8_t* hidKeys, uint8_t hidKeyCount) {

    if (inManualLocationEntry) {
        char* buf = (manualEntryField == 0) ? manualLatBuf : manualLonBuf;
        uint8_t& len = (manualEntryField == 0) ? manualLatLen : manualLonLen;
        constexpr uint8_t bufCap = 16;

        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBackspace = keys.backspace, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) { // cancel, discard edits
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inManualLocationEntry = false;
            return;
        }

        if (hasBackspace) {
            if (len > 0) { len--; buf[len] = '\0'; tone(TONE_ADJUST_HZ, TONE_ADJUST_MS); }
            return;
        }

        if (hasEnter) {
            if (manualEntryField == 0) {
                manualEntryField = 1;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else {
                double lat = atof(manualLatBuf);
                double lon = atof(manualLonBuf);
                if ((lat != 0.0 || lon != 0.0) && lat >= -90.0 && lat <= 90.0 &&
                    lon >= -180.0 && lon <= 180.0) {
                    LocationManager::setManualLocation(lat, lon);
                    tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                    inManualLocationEntry = false;
                } else {
                    // Invalid/empty — low buzz, stay in the field to fix it.
                    tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
                }
            }
            return;
        }

        for (uint8_t i = 0; i < count; i++) {
            char c = chars[i];
            bool allowed = (c >= '0' && c <= '9') || c == '-' || c == '.';
            if (allowed && len < bufCap - 1) {
                buf[len++] = c;
                buf[len] = '\0';
            }
        }
        return;
    }

    if (inRotationManualEntry) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBackspace = keys.backspace, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) { // cancel, discard edits
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inRotationManualEntry = false;
            return;
        }

        if (hasBackspace) {
            if (rotationLen > 0) { rotationLen--; rotationBuf[rotationLen] = '\0'; tone(TONE_ADJUST_HZ, TONE_ADJUST_MS); }
            return;
        }

        if (hasEnter) {
            long deg = atol(rotationBuf);
            if (rotationLen > 0 && deg >= 0 && deg <= 359) {
                DisplayRadar::setRotationDeg(static_cast<uint16_t>(deg));
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inRotationManualEntry = false;
            } else {
                // Invalid/empty — low buzz, stay in the field to fix it.
                tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            }
            return;
        }

        for (uint8_t i = 0; i < count; i++) {
            char c = chars[i];
            if (c >= '0' && c <= '9' && rotationLen < sizeof(rotationBuf) - 1) {
                rotationBuf[rotationLen++] = c;
                rotationBuf[rotationLen] = '\0';
            }
        }
        return;
    }

    if (inHostTextEntry) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBackspace = keys.backspace, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) { // cancel, discard edits
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inHostTextEntry = false;
            return;
        }

        if (hasBackspace) {
            if (hostLen > 0) { hostLen--; hostBuf[hostLen] = '\0'; tone(TONE_ADJUST_HZ, TONE_ADJUST_MS); }
            return;
        }

        if (hasEnter) {
            AdsbClient::setCustomHost(hostBuf); // empty is valid - just means "not configured yet"
            tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
            inHostTextEntry = false;
            return;
        }

        // Printable ASCII only - same range the WiFi password field in
        // wifi_setup_screen.cpp already accepts, since a hostname/IP needs
        // more than just digits (unlike lat/lon/rotation entry).
        for (uint8_t i = 0; i < count; i++) {
            char c = chars[i];
            if (c >= 32 && c < 127 && hostLen < sizeof(hostBuf) - 1) {
                hostBuf[hostLen++] = c;
                hostBuf[hostLen] = '\0';
            }
        }
        return;
    }

    if (inPortEntry) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBackspace = keys.backspace, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) {
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inPortEntry = false;
            return;
        }

        if (hasBackspace) {
            if (portLen > 0) { portLen--; portBuf[portLen] = '\0'; tone(TONE_ADJUST_HZ, TONE_ADJUST_MS); }
            return;
        }

        if (hasEnter) {
            long port = atol(portBuf);
            if (portLen > 0 && port >= 1 && port <= 65535) {
                AdsbClient::setCustomPort(static_cast<uint16_t>(port));
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inPortEntry = false;
            } else {
                // Invalid/empty — low buzz, stay in the field to fix it.
                tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            }
            return;
        }

        for (uint8_t i = 0; i < count; i++) {
            char c = chars[i];
            if (c >= '0' && c <= '9' && portLen < sizeof(portBuf) - 1) {
                portBuf[portLen++] = c;
                portBuf[portLen] = '\0';
            }
        }
        return;
    }

    if (inDataSourceSubscreen) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) {
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inDataSourceSubscreen = false;
            return;
        }

        uint8_t rowCount = dataSourceRowCount();
        uint8_t testRow = dataSourceTestRow();

        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == ';') {
                dataSourceSubSelected = (dataSourceSubSelected + rowCount - 1) % rowCount;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '.') {
                dataSourceSubSelected = (dataSourceSubSelected + 1) % rowCount;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '/' || chars[i] == ',') {
                if (dataSourceSubSelected == DATASOURCE_ROW_SOURCE) {
                    AdsbClient::setDataSource(nextDataSource(AdsbClient::currentDataSource()));
                    // A test result only speaks to the source it was run
                    // against - once the selection moves on, it's stale.
                    connTestState = ConnTestState::Idle;
                    // Row count/meaning may have just changed (e.g. leaving
                    // Custom hides Host/Port/Scheme) - keep the selection
                    // in range rather than pointing at a row that no
                    // longer exists.
                    uint8_t newCount = dataSourceRowCount();
                    if (dataSourceSubSelected >= newCount) dataSourceSubSelected = newCount - 1;
                    tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
                } else if (isCustomSelected() && dataSourceSubSelected == DATASOURCE_ROW_SCHEME) {
                    AdsbClient::setCustomUseHttps(!AdsbClient::customUseHttps());
                    tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
                }
            }
        }

        if (hasEnter) {
            if (dataSourceSubSelected == DATASOURCE_ROW_SOURCE) {
                AdsbClient::setDataSource(nextDataSource(AdsbClient::currentDataSource()));
                connTestState = ConnTestState::Idle;
                uint8_t newCount = dataSourceRowCount();
                if (dataSourceSubSelected >= newCount) dataSourceSubSelected = newCount - 1;
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
            } else if (isCustomSelected() && dataSourceSubSelected == DATASOURCE_ROW_HOST) {
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                startHostEntry();
            } else if (isCustomSelected() && dataSourceSubSelected == DATASOURCE_ROW_PORT) {
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                startPortEntry();
            } else if (isCustomSelected() && dataSourceSubSelected == DATASOURCE_ROW_SCHEME) {
                AdsbClient::setCustomUseHttps(!AdsbClient::customUseHttps());
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
            } else if (dataSourceSubSelected == testRow) {
                // Always present, whatever source is currently selected -
                // tests the hosted API in play, or the Custom config.
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                connTestState = ConnTestState::Testing;
                // Force an immediate redraw so "Testing..." is visible
                // before the blocking call below - handleWord() and
                // render() are otherwise only called from separate points
                // in main.cpp's loop(), so without this the screen would
                // just look frozen for the whole test duration instead of
                // showing what's happening.
                render();
                AdsbClient::ConnectionTestResult testResult = AdsbClient::testCurrentDataSource();
                connTestState = testResult.ok ? ConnTestState::Ok : ConnTestState::Failed;
                strncpy(connTestMsg, testResult.message, sizeof(connTestMsg) - 1);
                connTestMsg[sizeof(connTestMsg) - 1] = '\0';
                tone(testResult.ok ? TONE_CONFIRM_HZ : TONE_CLOSE_HZ, TONE_CONFIRM_MS);
            }
        }
        return;
    }

    if (inWifiManage) {
        uint8_t savedCount = WifiMgr::savedNetworkCount();
        uint8_t totalRows = savedCount + 1; // last row = "+ Add network"

        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBackspace = keys.backspace, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) {
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inWifiManage = false;
            return;
        }

        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == ';') { // up
                wifiManageSelected = (wifiManageSelected + totalRows - 1) % totalRows;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '.') { // down
                wifiManageSelected = (wifiManageSelected + 1) % totalRows;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            }
        }

        if (hasBackspace && wifiManageSelected < savedCount) {
            // Forget the selected saved network - no confirmation prompt,
            // easy to just re-add it if you didn't mean to.
            WifiMgr::forgetNetwork(wifiManageSelected);
            WifiMgr::saveCredentialsToSdIfMounted(); // keep wifi.txt in sync, if an SD card is present
            tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
            uint8_t newTotalRows = WifiMgr::savedNetworkCount() + 1;
            if (wifiManageSelected >= newTotalRows) wifiManageSelected = newTotalRows - 1;
            return;
        }

        if (hasEnter && wifiManageSelected == savedCount) {
            // "+ Add network" row - launch the familiar scan/connect flow.
            tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
            inWifiManage = false;
            inWifiSubscreen = true;
            WifiSetupScreen::onEnter();
        }
        // Enter on an existing saved network row currently does nothing -
        // only Del (forget) acts on those rows.
        return;
    }

    if (inWifiSubscreen) {
        WifiSetupScreen::handleWord(chars, count, fnHeld, shiftHeld, hidKeys, hidKeyCount);
        if (WifiSetupScreen::isDone()) {
            if (WifiSetupScreen::didConnectSucceed()) {
                WifiMgr::saveCredentialsToSdIfMounted();
            }
            inWifiSubscreen = false;
        }
        return;
    }

    if (inLocationSubscreen) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEnter = keys.enter, hasEsc = keys.esc, hasBacktick = keys.backtick;
        bool hasM = false;
        for (uint8_t i = 0; i < count; i++) if (chars[i] == 'm') hasM = true;

        if (hasEsc || hasBacktick) {
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inLocationSubscreen = false;
            return;
        }

        if (hasM) {
            // Manual coordinate entry is available regardless of which row
            // is selected, same as it was on the main list before.
            tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
            startManualEntry();
            return;
        }

        bool gpsOn = LocationManager::isGpsEnabled();

        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == ';') { // up
                locationSubSelected = (locationSubSelected + LOCATION_SUB_ROWS - 1) % LOCATION_SUB_ROWS;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '.') { // down
                locationSubSelected = (locationSubSelected + 1) % LOCATION_SUB_ROWS;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '/' || chars[i] == ',') { // adjust selected row
                if (locationSubSelected == 1) {
                    if (gpsOn) {
                        LocationManager::cycleGpsPinPair();
                    } else {
                        bool nowManual = LocationManager::sourcePreference() == LocationManager::SourcePref::Manual;
                        LocationManager::setSourceOverride(nowManual ? LocationManager::SourcePref::Auto
                                                                       : LocationManager::SourcePref::Manual);
                    }
                    tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
                }
            }
        }

        if (hasEnter) {
            if (locationSubSelected == 0) {
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                LocationManager::setGpsEnabled(!gpsOn);
            } else if (!gpsOn) {
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                bool nowManual = LocationManager::sourcePreference() == LocationManager::SourcePref::Manual;
                LocationManager::setSourceOverride(nowManual ? LocationManager::SourcePref::Auto
                                                               : LocationManager::SourcePref::Manual);
            }
        }
        return;
    }

    if (inUnitsSubscreen) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEsc = keys.esc, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) {
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inUnitsSubscreen = false;
            return;
        }

        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == ';') {
                unitsSubSelected = (unitsSubSelected + UNITS_SUB_ROWS - 1) % UNITS_SUB_ROWS;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '.') {
                unitsSubSelected = (unitsSubSelected + 1) % UNITS_SUB_ROWS;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '/' || chars[i] == ',') {
                if (unitsSubSelected == 0) {
                    Units::toggleDistance();
                } else {
                    Units::toggleAltitude();
                }
                tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
            }
        }
        return;
    }

    if (inProxBeepSubscreen) {
        Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
        bool hasEsc = keys.esc, hasBacktick = keys.backtick;

        if (hasEsc || hasBacktick) {
            tone(TONE_CLOSE_HZ, TONE_CLOSE_MS);
            inProxBeepSubscreen = false;
            return;
        }

        for (uint8_t i = 0; i < count; i++) {
            if (chars[i] == ';') {
                proxBeepSubSelected = (proxBeepSubSelected + PROXBEEP_SUB_ROWS - 1) % PROXBEEP_SUB_ROWS;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '.') {
                proxBeepSubSelected = (proxBeepSubSelected + 1) % PROXBEEP_SUB_ROWS;
                tone(TONE_NAV_HZ, TONE_NAV_MS);
            } else if (chars[i] == '/') { // increase / next
                switch (proxBeepSubSelected) {
                    case 0: {
                        float km = min(PROX_DIST_MAX_KM, ProximityAlert::getThresholdKm() + PROX_DIST_STEP_KM);
                        ProximityAlert::setThresholdKm(km);
                        break;
                    }
                    case 1: {
                        float ft = min(PROX_ALT_MAX_FT, ProximityAlert::getThresholdAltFt() + PROX_ALT_STEP_FT);
                        ProximityAlert::setThresholdAltFt(ft);
                        break;
                    }
                    default:
                        ProximityAlert::setBeepEnabled(!ProximityAlert::isBeepEnabled());
                        break;
                }
                tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
            } else if (chars[i] == ',') { // decrease / prev
                switch (proxBeepSubSelected) {
                    case 0: {
                        float km = max(PROX_DIST_MIN_KM, ProximityAlert::getThresholdKm() - PROX_DIST_STEP_KM);
                        ProximityAlert::setThresholdKm(km);
                        break;
                    }
                    case 1: {
                        float ft = max(PROX_ALT_MIN_FT, ProximityAlert::getThresholdAltFt() - PROX_ALT_STEP_FT);
                        ProximityAlert::setThresholdAltFt(ft);
                        break;
                    }
                    default:
                        ProximityAlert::setBeepEnabled(!ProximityAlert::isBeepEnabled());
                        break;
                }
                tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
            }
        }
        return;
    }

    Keys keys = scanKeys(hidKeys, hidKeyCount, chars, count);
    bool hasEnter = keys.enter, hasEsc = keys.esc, hasBacktick = keys.backtick;

    if (hasEsc || hasBacktick) { tone(TONE_CLOSE_HZ, TONE_CLOSE_MS); done = true; return; }

    for (uint8_t i = 0; i < count; i++) {
        if (chars[i] == ';') { // up
            selected = static_cast<Item>((static_cast<uint8_t>(selected) +
                       static_cast<uint8_t>(Item::Count) - 1) % static_cast<uint8_t>(Item::Count));
            tone(TONE_NAV_HZ, TONE_NAV_MS);
        } else if (chars[i] == '.') { // down
            selected = static_cast<Item>((static_cast<uint8_t>(selected) + 1) %
                       static_cast<uint8_t>(Item::Count));
            tone(TONE_NAV_HZ, TONE_NAV_MS);
        } else if (chars[i] == '/') { // right — increase slider
            switch (selected) {
                case Item::DisplayBrightness:
                    displayBrightnessPercent = min(100, displayBrightnessPercent + 10);
                    prefs.putUChar("dispBright", displayBrightnessPercent);
                    applyDisplayBrightness();
                    break;
                case Item::LedBrightness:
                    NeopixelStatus::setBrightnessPercent(
                        min(100, NeopixelStatus::getBrightnessPercent() + 10));
                    break;
                case Item::RadarRotation:
                    DisplayRadar::cycleRotation(ROTATION_STEP_DEG);
                    break;
                case Item::Volume:
                    VolumeControl::increase();
                    break;
                case Item::Logbook:
                    FlightLogbook::setEnabled(!FlightLogbook::isEnabled());
                    break;
                default: break;
            }
            tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
        } else if (chars[i] == ',') { // left — decrease slider
            switch (selected) {
                case Item::DisplayBrightness:
                    displayBrightnessPercent = max(0, displayBrightnessPercent - 10);
                    prefs.putUChar("dispBright", displayBrightnessPercent);
                    applyDisplayBrightness();
                    break;
                case Item::RadarRotation:
                    DisplayRadar::cycleRotation(-ROTATION_STEP_DEG);
                    break;
                case Item::LedBrightness:
                    NeopixelStatus::setBrightnessPercent(
                        max(0, NeopixelStatus::getBrightnessPercent() - 10));
                    break;
                case Item::Volume:
                    VolumeControl::decrease();
                    break;
                case Item::Logbook:
                    FlightLogbook::setEnabled(!FlightLogbook::isEnabled());
                    break;
                default: break;
            }
            tone(TONE_ADJUST_HZ, TONE_ADJUST_MS);
        } else if (chars[i] == 'm' && selected == Item::RadarRotation) {
            // Same "m" convention as Location's manual lat/lon entry - an
            // exact-value alternative to the coarse +/-15° slider above.
            tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
            startRotationManualEntry();
        }
    }

    if (hasEnter) {
        switch (selected) {
            case Item::Wifi:
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inWifiManage = true;
                wifiManageSelected = 0;
                break;
            case Item::Location:
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inLocationSubscreen = true;
                locationSubSelected = 0;
                break;
            case Item::DataSource:
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inDataSourceSubscreen = true;
                dataSourceSubSelected = 0;
                break;
            case Item::Units:
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inUnitsSubscreen = true;
                unitsSubSelected = 0;
                break;
            case Item::ProxBeep:
                tone(TONE_CONFIRM_HZ, TONE_CONFIRM_MS);
                inProxBeepSubscreen = true;
                proxBeepSubSelected = 0;
                break;
            default: break;
        }
    }
}

namespace {
    // Row text is drawn larger than the title/footer for readability - 1.5x
    // is the largest size that still keeps the longest real row (" Distance:
    // Nautical Miles", 25 chars) under the 240px sprite width.
    constexpr float kSubRowTextSize = 1.5f;

    // Shared row-list renderer for the sub-screens below - draws a title, a
    // divider, up to `rowCount` selectable text rows, and a footer hint.
    // Returns the Y coordinate just below the last row, so callers that need
    // an extra info line beneath the list (current coordinates, AMSL note,
    // connection test result) can position it correctly without duplicating
    // the row-height math.
    //
    // Deliberately does NOT push the sprite - callers must call
    // d.pushSprite(0, 0) themselves exactly once, after drawing any extra
    // content. An earlier version pushed here and callers pushed again after
    // adding their extra line, which sent two separate frames per render()
    // call: the first without the extra line, the second with it - visible
    // as that line flickering on/off every call.
    int16_t renderSubscreenRows(M5Canvas& d, const char* title, const char* const* rows,
                                 uint8_t rowCount, uint8_t selectedRow, const char* footer) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextDatum(top_left);
        d.setTextColor(TFT_GREEN);
        d.setCursor(4, 4);
        d.println(title);
        d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

        d.setTextSize(kSubRowTextSize);
        int16_t y = 28;
        int16_t rowH = d.fontHeight() + 3;
        for (uint8_t i = 0; i < rowCount; i++) {
            bool isSelected = (i == selectedRow);
            if (isSelected) d.fillRect(0, y, d.width(), rowH, TFT_GREEN);
            d.setTextColor(isSelected ? TFT_BLACK : TFT_WHITE, isSelected ? TFT_GREEN : TFT_BLACK);
            d.setCursor(4, y + 2);
            d.print(rows[i]);
            y += rowH;
        }

        d.setTextSize(1);
        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(2, d.height() - d.fontHeight() - 1);
        d.print(footer);

        return y;
    }
}

void render() {
    if (inWifiSubscreen) {
        WifiSetupScreen::render();
        // Poll here too, not just in handleWord() - otherwise the screen
        // only leaves Stage::Done (and returns to this menu) the next time
        // a key happens to be pressed, which looked like it was stuck on
        // "Returning..." forever.
        if (WifiSetupScreen::isDone()) {
            if (WifiSetupScreen::didConnectSucceed()) {
                WifiMgr::saveCredentialsToSdIfMounted();
            }
            inWifiSubscreen = false;
        }
        return;
    }

    if (!spriteReady) return;

    auto& d = settingsSprite;

    if (inWifiManage) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextDatum(top_left);
        d.setTextColor(TFT_GREEN);
        d.setCursor(4, 4);
        d.println("Manage WiFi networks");
        d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

        uint8_t savedCount = WifiMgr::savedNetworkCount();
        uint8_t totalRows = savedCount + 1;
        int16_t y = 28;
        int16_t rowH = d.fontHeight() + 2;

        if (savedCount == 0) {
            d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
            d.setCursor(4, y);
            d.println("No networks saved yet.");
            y += rowH;
        }

        for (uint8_t i = 0; i < totalRows; i++) {
            bool isSelected = (i == wifiManageSelected);
            if (isSelected) d.fillRect(0, y, d.width(), rowH, TFT_GREEN);
            d.setTextColor(isSelected ? TFT_BLACK : TFT_WHITE, isSelected ? TFT_GREEN : TFT_BLACK);
            d.setCursor(4, y + 1);
            if (i < savedCount) {
                d.printf(" %s", WifiMgr::savedNetworkSsid(i).c_str());
            } else {
                d.print(" + Add network");
            }
            y += rowH;
        }

        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(2, d.height() - d.fontHeight() - 1);
        d.print(";/.=move Ent=add Del=forget `=back");

        d.pushSprite(0, 0);
        return;
    }

    if (inManualLocationEntry) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextDatum(top_left);
        d.setTextColor(TFT_GREEN);
        d.setCursor(4, 4);
        d.println("Manual location");
        d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

        d.setCursor(4, 28);
        d.setTextColor(manualEntryField == 0 ? TFT_BLACK : TFT_WHITE,
                        manualEntryField == 0 ? TFT_GREEN : TFT_BLACK);
        d.printf(" Lat: %s%s \n", manualLatBuf, manualEntryField == 0 ? "_" : "");

        d.setCursor(4, 46);
        d.setTextColor(manualEntryField == 1 ? TFT_BLACK : TFT_WHITE,
                        manualEntryField == 1 ? TFT_GREEN : TFT_BLACK);
        d.printf(" Lon: %s%s \n", manualLonBuf, manualEntryField == 1 ? "_" : "");

        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, 66);
        d.println("Digits, - and . only");
        d.println("Enter: next  Del: back  `: cancel");

        d.pushSprite(0, 0);
        return;
    }

    if (inRotationManualEntry) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextDatum(top_left);
        d.setTextColor(TFT_GREEN);
        d.setCursor(4, 4);
        d.println("Radar rotation");
        d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

        d.setTextColor(TFT_BLACK, TFT_GREEN);
        d.setCursor(4, 28);
        d.printf(" %s_ \n", rotationBuf);

        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, 48);
        d.println("Degrees, 0-359");
        d.println("Enter: set  Del: back  `: cancel");

        d.pushSprite(0, 0);
        return;
    }

    if (inHostTextEntry) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextDatum(top_left);
        d.setTextColor(TFT_GREEN);
        d.setCursor(4, 4);
        d.println("tar1090 Host");
        d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

        d.setTextColor(TFT_BLACK, TFT_GREEN);
        d.setCursor(4, 28);
        d.printf(" %s_ \n", hostBuf);

        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, 48);
        d.println("Hostname or IP, e.g. 192.168.0.120");
        d.println("Enter: set  Del: back  `: cancel");

        d.pushSprite(0, 0);
        return;
    }

    if (inPortEntry) {
        d.fillScreen(TFT_BLACK);
        d.setTextSize(1);
        d.setTextDatum(top_left);
        d.setTextColor(TFT_GREEN);
        d.setCursor(4, 4);
        d.println("tar1090 Port");
        d.drawFastHLine(0, 20, d.width(), TFT_DARKGREEN);

        d.setTextColor(TFT_BLACK, TFT_GREEN);
        d.setCursor(4, 28);
        d.printf(" %s_ \n", portBuf);

        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, 48);
        d.println("Digits only, 1-65535");
        d.println("Enter: set  Del: back  `: cancel");

        d.pushSprite(0, 0);
        return;
    }

    if (inDataSourceSubscreen) {
        bool isCustom = isCustomSelected();
        uint8_t rowCount = dataSourceRowCount();

        char row0[32];
        snprintf(row0, sizeof(row0), " Source: %s",
                 dataSourceLabel(AdsbClient::currentDataSource()));

        char row1[48], row2[32], row3[32];
        const char* rows[5]; // max possible rows; only the first rowCount are used
        rows[0] = row0;

        if (isCustom) {
            const char* host = AdsbClient::customHost();
            snprintf(row1, sizeof(row1), " Host: %s", host[0] ? host : "(not set)");
            snprintf(row2, sizeof(row2), " Port: %u", AdsbClient::customPort());
            snprintf(row3, sizeof(row3), " Scheme: %s", AdsbClient::customUseHttps() ? "HTTPS" : "HTTP");
            rows[1] = row1;
            rows[2] = row2;
            rows[3] = row3;
            rows[4] = " Test Connection";
        } else {
            rows[1] = " Test Connection";
        }

        int16_t noteY = renderSubscreenRows(d, "Data Source", rows, rowCount, dataSourceSubSelected,
                                             ";/.=move Ent/,//=set `=back");

        // Test result shown below the row list, same treatment as
        // Location's current-coordinates line and Prox Beep's AMSL note.
        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, noteY + 4);
        switch (connTestState) {
            case ConnTestState::Testing:
                d.print(" Testing...");
                break;
            case ConnTestState::Ok:
                d.setTextColor(TFT_GREEN, TFT_BLACK);
                d.printf(" %s", connTestMsg);
                break;
            case ConnTestState::Failed:
                d.setTextColor(TFT_RED, TFT_BLACK);
                d.printf(" %s", connTestMsg);
                break;
            default:
                d.print(" (untested)");
                break;
        }

        d.pushSprite(0, 0);
        return;
    }

    if (inLocationSubscreen) {
        bool gpsOn = LocationManager::isGpsEnabled();

        char row0[32];
        snprintf(row0, sizeof(row0), " GPS: %s", gpsOn ? "ON" : "OFF");

        char row1[32];
        if (gpsOn) {
            snprintf(row1, sizeof(row1), " Pin: %s", LocationManager::currentGpsPinLabel());
        } else {
            snprintf(row1, sizeof(row1), " Source: %s", locationSourceLabel());
        }

        const char* rows[LOCATION_SUB_ROWS] = { row0, row1 };
        int16_t noteY = renderSubscreenRows(d, "Location", rows, LOCATION_SUB_ROWS, locationSubSelected,
                                             ";/.=move Ent/,//=set m=manual `=back");

        // Current location, shown below the row list rather than as a
        // selectable row - it's informational, not something to edit here
        // (that's what 'm' / Manual entry is for).
        double lat = 0.0, lon = 0.0;
        bool haveFix = LocationManager::currentSource() != LocationManager::Source::None;
        if (haveFix) LocationManager::getHomeLocation(lat, lon);

        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, noteY + 4);
        if (haveFix) {
            d.printf(" %.4f, %.4f", lat, lon);
        } else {
            d.print(" No fix yet");
        }

        d.pushSprite(0, 0);
        return;
    }

    if (inUnitsSubscreen) {
        // Spelled out in full here (this screen has the room for it) -
        // everywhere else that shows units (HUD, range scale, Prox Beep)
        // keeps the short suffix instead.
        char row0[32];
        snprintf(row0, sizeof(row0), " Distance: %s", Units::distFullName());
        char row1[32];
        snprintf(row1, sizeof(row1), " Altitude: %s", Units::altFullName());

        const char* rows[UNITS_SUB_ROWS] = { row0, row1 };
        renderSubscreenRows(d, "Units", rows, UNITS_SUB_ROWS, unitsSubSelected,
                             ";/.=move ,//=cycle `=back");
        d.pushSprite(0, 0);
        return;
    }

    if (inProxBeepSubscreen) {
        char distBuf[16];
        Units::formatDistance(ProximityAlert::getThresholdKm(), distBuf, sizeof(distBuf));
        char row0[32];
        snprintf(row0, sizeof(row0), " Max Distance: %s", distBuf);

        char row1[32];
        float threshAltFt = ProximityAlert::getThresholdAltFt();
        if (Units::currentAltitude() == Units::Altitude::Meters) {
            snprintf(row1, sizeof(row1), " Max Height: %.0fm", UnitMath::ftToMeters(threshAltFt));
        } else {
            snprintf(row1, sizeof(row1), " Max Height: %.0fft", threshAltFt);
        }

        char row2[32];
        snprintf(row2, sizeof(row2), " Beep: %s", ProximityAlert::isBeepEnabled() ? "On" : "Off");

        const char* rows[PROXBEEP_SUB_ROWS] = { row0, row1, row2 };
        int16_t noteY = renderSubscreenRows(d, "Proximity Beep", rows, PROXBEEP_SUB_ROWS, proxBeepSubSelected,
                                             ";/.=move ,//=adjust `=back");

        // Altitude filter is barometric (AMSL), not height above ground -
        // worth a permanent reminder since it's easy to assume otherwise.
        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.setCursor(4, noteY + 4);
        d.print(" Height filter is AMSL (baro alt)");

        d.pushSprite(0, 0);
        return;
    }

    d.fillScreen(TFT_BLACK);
    d.setTextSize(1.7f);
    int16_t lineH = d.fontHeight();

    d.setTextDatum(top_left);
    d.setTextColor(TFT_GREEN);
    int16_t y = 2;
    d.setCursor(4, y);
    d.print("Settings");
    y += lineH + 1;
    d.drawFastHLine(0, y, d.width(), TFT_DARKGREEN);
    y += 2;

    int16_t rowH = lineH + 1;
    int16_t screenW = d.width();
    int16_t listTop = y;
    int16_t footerH = lineH + 2; // reserved space for the footer hint line
    int16_t listBottom = d.height() - footerH;

    uint8_t totalItems = static_cast<uint8_t>(Item::Count);
    uint8_t selectedIdx = static_cast<uint8_t>(selected);

    uint8_t maxVisibleRows = (listBottom - listTop) / rowH;
    if (maxVisibleRows < 1) maxVisibleRows = 1;

    // Keep the selected row scrolled into view - the item list is taller
    // than the visible area now that Units/Flight Logbook were added, so
    // without this the last entries would be drawn off the bottom edge
    // and be unreadable even though they were still technically selectable.
    if (selectedIdx < scrollOffset) {
        scrollOffset = selectedIdx;
    } else if (selectedIdx >= scrollOffset + maxVisibleRows) {
        scrollOffset = selectedIdx - maxVisibleRows + 1;
    }
    if (totalItems > maxVisibleRows && scrollOffset + maxVisibleRows > totalItems) {
        scrollOffset = totalItems - maxVisibleRows;
    } else if (totalItems <= maxVisibleRows) {
        scrollOffset = 0;
    }

    for (uint8_t i = scrollOffset; i < totalItems && i < scrollOffset + maxVisibleRows; i++) {
        Item it = static_cast<Item>(i);
        bool isSelected = (it == selected);

        if (isSelected) d.fillRect(0, y, screenW, rowH, TFT_GREEN);
        d.setTextColor(isSelected ? TFT_BLACK : TFT_WHITE, isSelected ? TFT_GREEN : TFT_BLACK);
        d.setCursor(4, y + 1);

        switch (it) {
            case Item::Wifi:
                d.printf("WiFi: %s", WifiMgr::getState() == WifiMgr::State::Connected
                                        ? "Connected" : "Not connected");
                break;
            case Item::Location:
                if (LocationManager::isGpsEnabled()) {
                    d.printf("GPS: ON %s", LocationManager::hasGpsFix() ? "FIX" : "no fix");
                } else if (LocationManager::currentSource() == LocationManager::Source::None) {
                    d.print("GPS: OFF (no fix yet)");
                } else {
                    d.printf("GPS: OFF (%s)", locationSourceLabel());
                }
                break;
            case Item::DataSource:
                d.printf("Data Source: %s", dataSourceLabel(AdsbClient::currentDataSource()));
                break;
            case Item::Units:
                d.printf("Units: %s/%s", Units::distSuffix(), Units::altSuffix());
                break;
            case Item::ProxBeep:
                d.printf("Prox Beep: %s", ProximityAlert::isBeepEnabled() ? "On" : "Off");
                break;
            case Item::DisplayBrightness:
                d.printf("Display: %d%%", displayBrightnessPercent);
                break;
            case Item::RadarRotation:
                // Plain "deg" rather than a real degree glyph - this GLCD
                // font isn't guaranteed to have one, same reasoning as
                // every other unit label in this UI staying plain ASCII.
                d.printf("Rotation: %u deg", DisplayRadar::currentRotationDeg());
                break;
            case Item::LedBrightness:
                d.printf("LED: %d%%", NeopixelStatus::getBrightnessPercent());
                break;
            case Item::Volume:
                d.printf("Volume: %d/10", VolumeControl::currentStep());
                break;
            case Item::Logbook:
                d.printf("Logbook: %s", FlightLogbook::isEnabled() ? "On" : "Off");
                break;
            default: break;
        }
        y += rowH;
    }

    // Small scroll indicators so it's obvious there's more above/below,
    // rather than it just looking like the list quietly ends.
    if (scrollOffset > 0) {
        d.setTextDatum(top_right);
        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.drawString("^", screenW - 4, listTop);
    }
    if (scrollOffset + maxVisibleRows < totalItems) {
        d.setTextDatum(bottom_right);
        d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
        d.drawString("v", screenW - 4, listBottom - 1);
    }
    d.setTextDatum(top_left);

    // Footer pinned to the bottom edge rather than relative to the item
    // list, so it can't collide with rows above even as row count/height
    // changes.
    d.setTextColor(TFT_DARKGREEN, TFT_BLACK);
    d.setCursor(2, d.height() - lineH - 1);
    if (selected == Item::Wifi || selected == Item::Location ||
        selected == Item::DataSource || selected == Item::Units ||
        selected == Item::ProxBeep) {
        d.print(";/.=move Ent=open");
    } else if (selected == Item::RadarRotation) {
        d.print(";/.=move ,//=adjust m=exact");
    } else {
        d.print(";/.=move ,//=adjust");
    }

    d.pushSprite(0, 0);
}

}
