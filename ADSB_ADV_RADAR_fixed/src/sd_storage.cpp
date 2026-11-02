#include "sd_storage.h"
#include "config.h"
#include <SD.h>
#include <SPI.h>
#include <time.h>

namespace SdStorage {

namespace {
    bool mounted = false;
    SPIClass sdSpi(FSPI); 
    const char* kDefaultAirlinesCsv =
        "icao,name\n"
        "BAW,British Airways\n"
        "SAA,South African Airways\n"
        "CAW,Comair\n"
        "FLY,Safair (FlySafair)\n"
        "KLM,KLM Royal Dutch Airlines\n"
        "DLH,Lufthansa\n"
        "UAE,Emirates\n"
        "QTR,Qatar Airways\n"
        "ETH,Ethiopian Airlines\n"
        "AFR,Air France\n"
        "SWR,Swiss International\n"
        "MSR,EgyptAir\n"
        "KQA,Kenya Airways\n"
        "UAL,United Airlines\n"
        "DAL,Delta Air Lines\n"
        "AAL,American Airlines\n"
        "EWG,Eurowings\n"
        "CFG,Condor\n"
        "TUI,TUIfly\n"
        "CLH,Lufthansa CityLine\n"
        "DLA,Air Dolomiti\n"
        "SDR,Sundair\n"
        "RYR,Ryanair\n"
        "EZY,easyJet\n"
        "EJU,easyJet Europe\n"
        "WZZ,Wizz Air\n"
        "WMT,Wizz Air Malta\n"
        "VLG,Vueling\n"
        "IBE,Iberia\n"
        "TAP,TAP Air Portugal\n"
        "AUA,Austrian Airlines\n"
        "SAS,SAS Scandinavian Airlines\n"
        "NAX,Norwegian Air Shuttle\n"
        "LOT,LOT Polish Airlines\n"
        "FIN,Finnair\n"
        "EIN,Aer Lingus\n"
        "BEL,Brussels Airlines\n"
        "CTN,Croatia Airlines\n"
        "THY,Turkish Airlines\n"
        "PGT,Pegasus Airlines\n"
        "BTI,Air Baltic\n"
        "ICE,Icelandair\n";

    const char* kDefaultAircraftTypesCsv =
        "type,seats\n"
        "A320,180\n"
        "A321,220\n"
        "A319,140\n"
        "A332,278\n"
        "A333,277\n"
        "A359,314\n"
        "A388,469\n"
        "B738,189\n"
        "B737,148\n"
        "B739,180\n"
        "B77W,365\n"
        "B788,242\n"
        "B789,296\n"
        "E190,100\n"
        "CRJ2,50\n"
        "CRJ9,90\n"
        "DH8D,78\n"
        "A20N,180\n"
        "A21N,220\n"
        "A19N,145\n"
        "A318,107\n"
        "BCS1,133\n"
        "BCS3,149\n"
        "A339,287\n"
        "A343,295\n"
        "A346,380\n"
        "A35K,350\n"
        "B37M,153\n"
        "B38M,178\n"
        "B39M,193\n"
        "B752,200\n"
        "B753,243\n"
        "B762,180\n"
        "B763,218\n"
        "B744,416\n"
        "B748,410\n"
        "E170,76\n"
        "E175,88\n"
        "E195,124\n"
        "CRJ7,70\n"
        "CRJ1,50\n"
        "AT72,70\n"
        "AT43,48\n"
        "F100,100\n"
        "RJ85,85\n"
        "RJ1H,100\n"
        "SF34,34\n"
        "SB20,50\n"
        "PC12,9\n"
        "C172,4\n"
        "C182,4\n"
        "PA28,4\n"
        "C56X,9\n"
        "C25A,8\n"
        "GLF5,14\n"
        "CL60,9\n"
        "H160,8\n"
        "EC35,6\n"
        "EC45,8\n"
        // --- Ergänzt: gängige Kleinflugzeuge, Business Jets, ältere
        // Airliner und Hubschrauber, die vorher fehlten (z.B. C152). ---
        "C152,2\n"
        "C150,2\n"
        "DA40,4\n"
        "DA42,4\n"
        "DA62,7\n"
        "SR22,4\n"
        "SR20,4\n"
        "PA34,6\n"
        "PA44,4\n"
        "PA31,8\n"
        "BE20,9\n"
        "BE9L,8\n"
        "C208,9\n"
        "C210,4\n"
        "C525,7\n"
        "C560,8\n"
        "C680,9\n"
        "C750,8\n"
        "GLF4,14\n"
        "GLF6,14\n"
        "FA7X,12\n"
        "FA8X,14\n"
        "E135,37\n"
        "E145,50\n"
        "E120,30\n"
        "B733,149\n"
        "B734,168\n"
        "B735,132\n"
        "MD82,155\n"
        "L410,19\n"
        "R44,4\n"
        "R66,5\n"
        "H125,5\n"
        "AS50,5\n"
        "TB20,4\n"
        "TB10,4\n";

    bool ensureDir(const char* path) {
        if (SD.exists(path)) return true;
        return SD.mkdir(path);
    }

    void writeIfAbsent(const char* path, const char* contents) {
        if (SD.exists(path)) return;
        File f = SD.open(path, FILE_WRITE);
        if (!f) return;
        f.print(contents);
        f.close();
    }
}

bool init() {
    // Confirmed pinout (side panel sticker, Stamp-S3A module):
    // SCK=G40, MISO=G39, MOSI=G14, CS=G12.
    sdSpi.begin(Config::SD_SPI_CLK_PIN, Config::SD_SPI_MISO_PIN,
                Config::SD_SPI_MOSI_PIN, Config::SD_SPI_CS_PIN);
    mounted = SD.begin(Config::SD_SPI_CS_PIN, sdSpi);
    if (!mounted) return false;

    ensureDir(Config::SD_ROOT_DIR);
    ensureDir(Config::SD_LOG_DIR);
    return true;
}

bool isMounted() { return mounted; }

void seedDefaultDataFiles() {
    if (!mounted) return;
    writeIfAbsent(Config::SD_AIRLINES_CSV, kDefaultAirlinesCsv);
    writeIfAbsent(Config::SD_AIRCRAFT_TYPES_CSV, kDefaultAircraftTypesCsv);
}

void logEvent(const char* csvLine) {
    if (!mounted) {
        Serial.println("[SD] logEvent: card not mounted");
        return;
    }

    time_t now = time(nullptr);
    struct tm tmNow;
    localtime_r(&now, &tmNow);
    char filename[64];
    snprintf(filename, sizeof(filename), "%s/%04d-%02d-%02d.csv",
             Config::SD_LOG_DIR, tmNow.tm_year + 1900, tmNow.tm_mon + 1, tmNow.tm_mday);

    File f = SD.open(filename, FILE_APPEND);
    if (!f) {
        Serial.printf("[SD] logEvent: could not open file: %s\n", filename);
        return;
    }
    f.println(csvLine);
    f.close();
    Serial.printf("[SD] logEvent: geschrieben nach %s -> %s\n", filename, csvLine);
}

}