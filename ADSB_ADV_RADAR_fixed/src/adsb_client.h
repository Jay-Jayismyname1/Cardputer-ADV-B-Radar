#pragma once
#include <Arduino.h>
#include "aircraft.h"
#include "config.h"

namespace AdsbClient {

    struct FetchResult {
        bool     ok = false;
        uint16_t aircraftCount = 0;
        int      httpCode = 0;
    };

    // Where fetch() gets its data from. AdsbFi/AdsbLol/AirplanesLive are all
    // free, hosted APIs with an identical "ac"-array JSON shape and a
    // lat/lon/radius query (just different host + URL shape), sharing one
    // parsing path. CustomTar1090 is a user-supplied tar1090/readsb instance
    // of their own (host:port + http/https they configure in Settings),
    // whose /data/aircraft.json has a different array key ("aircraft") and
    // no server-side range filtering - see adsb_client.cpp for how that's
    // handled. Every source shares the same per-aircraft field-extraction
    // logic (AdsbClient::parseAircraftFeed()) since the field names
    // themselves are identical across all four.
    enum class DataSource : uint8_t { AdsbFi = 0, AdsbLol = 1, AirplanesLive = 2, CustomTar1090 = 3 };

    // Loads the persisted data-source selection/host/port/scheme. Call once
    // from setup(), same as every other module's init().
    void init();

    void setDataSource(DataSource src);
    DataSource currentDataSource();

    // Short (<=4 char) label for the HUD - "FI"/"LOL"/"LIVE"/"CSTM". Custom
    // deliberately shows a fixed abbreviation rather than the actual host/IP -
    // there isn't room on the HUD for that, and it isn't really "at a glance"
    // information the way the others are.
    const char* currentDataSourceShortLabel();

    // Host (hostname or IP, no scheme/port), port, and scheme for
    // CustomTar1090. Ignored while any hosted API is selected. All persist
    // immediately on set.
    void setCustomHost(const char* host);
    const char* customHost();
    void setCustomPort(uint16_t port);
    uint16_t customPort();
    void setCustomUseHttps(bool useHttps);
    bool customUseHttps();

    struct ConnectionTestResult {
        bool ok = false;
        int  httpCode = 0;
        char message[40] = "";
    };

    // Synchronous (blocking) connectivity check against the currently
    // configured CustomTar1090 host/port/scheme - GETs the small
    // /data/receiver.json and confirms the response actually looks like a
    // readsb/tar1090 instance (not just "some webserver that returned 200").
    // Deliberately blocking, same as this app's existing WiFi-scan settings
    // flow - it's a one-shot user-initiated action from the Settings menu,
    // not something in the render loop.
    ConnectionTestResult testCustomConnection();

    // Same idea as testCustomConnection(), but dispatches to whichever
    // source currentDataSource() currently selects - a hosted API (adsb.fi/
    // adsb.lol/airplanes.live) gets a minimal "null island" probe query
    // (lat=0, lon=0, a small radius) just to confirm the host is reachable
    // and returns the expected "ac"-shaped JSON; CustomTar1090 delegates to
    // testCustomConnection(). This is what the Settings "Test Connection"
    // action actually calls, so it always tests whatever's currently
    // selected, not just the custom config.
    ConnectionTestResult testCurrentDataSource();

    // Original blocking fetch - now only called internally by the
    // background task below. Calling this directly from loop() is what
    // caused the ~1-3s UI stutter every FETCH_INTERVAL_MS (TLS handshake +
    // HTTP GET + JSON parse all block whichever core calls it). Dispatches
    // to whichever source currentDataSource() currently selects.
    FetchResult fetch(double homeLat, double homeLon, float radiusKm,
                       Aircraft* table, uint8_t tableCapacity);

    void primeTime();

    // --- Background fetch API --------------------------------------------
    // Runs the network fetch on a FreeRTOS task pinned to core 0, so the
    // main loop (core 1: sweep animation, keypad polling, display) never
    // blocks on it. Call startBackgroundTask() once from setup().
    void startBackgroundTask();

    // Kicks off a new fetch if one isn't already running. Returns false
    // (and does nothing) if the previous fetch hasn't finished yet - the
    // caller should just try again on the next interval tick.
    bool requestFetch(double homeLat, double homeLon, float radiusKm);

    // True once a background fetch has finished and its data is ready to
    // be picked up with consumeResult().
    bool resultReady();

    // Copies the finished fetch's aircraft data into outTable (capacity
    // outCapacity entries) and returns its FetchResult. Only call this
    // after resultReady() returned true; clears the ready flag as a side
    // effect, so call it exactly once per completed fetch.
    FetchResult consumeResult(Aircraft* outTable, uint8_t outCapacity);

}