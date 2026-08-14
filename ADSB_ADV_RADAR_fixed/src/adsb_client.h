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

    // Where fetch() gets its data from - the public opendata.adsb.fi API
    // (default, unchanged behavior), or a user-supplied tar1090/readsb
    // instance of their own (plain HTTP, host:port they configure in
    // Settings). Both sources are parsed by the same shared field-extraction
    // logic - see adsb_client.cpp - since the per-aircraft JSON fields are
    // identical between the two; only the transport, URL, and top-level
    // array key ("ac" vs "aircraft") differ.
    enum class DataSource : uint8_t { AdsbFi = 0, CustomTar1090 = 1 };

    // Loads the persisted data-source selection/host/port. Call once from
    // setup(), same as every other module's init().
    void init();

    void setDataSource(DataSource src);
    DataSource currentDataSource();

    // Host (hostname or IP, no scheme/port) and port for CustomTar1090.
    // Ignored while AdsbFi is selected. Both persist immediately on set.
    void setCustomHost(const char* host);
    const char* customHost();
    void setCustomPort(uint16_t port);
    uint16_t customPort();

    // Original blocking fetch - now only called internally by the
    // background task below. Calling this directly from loop() is what
    // caused the ~1-3s UI stutter every FETCH_INTERVAL_MS (TLS handshake +
    // HTTP GET + JSON parse all block whichever core calls it). Dispatches
    // to the adsb.fi or tar1090 implementation based on currentDataSource().
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