#pragma once
// ----------------------------------------------------------------------------
//  Data layer: the transit providers.
//
//  `DepartureSource` is the interface every provider implements; each fetches
//  and parses its own API and appends normalised `Departure` records. This file
//  has no display dependencies — see display.h for rendering.
// ----------------------------------------------------------------------------
#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>

// How a row should be drawn. A provider tags each departure with one of these;
// the display maps it to a renderer (see display.cpp). Reuse an existing style
// for a new provider, or add a value here + a renderer to introduce a new look.
enum class RowStyle : uint8_t {
  Countdown,  // amber-on-black, minutes remaining (Wiener Linien)
  Clock,      // blue board, planned/realtime clock times (ÖBB)
};

// One upcoming departure, normalised across all providers.
struct Departure {
  String   line;       // "D", "U6", "S 80", "RJ 558"
  String   towards;    // direction text
  int      countdown;  // minutes until departure (>= 0); also the sort key
  RowStyle style;      // how to render this row
  String   planned;    // planned "HH:MM" (clock styles; "" otherwise)
  String   actual;     // realtime "HH:MM" (== planned when on time)
  bool     delayed;    // actual later than planned
};

// Abstract data source. Each provider appends its departures to `out`.
// Returns false on network/parse failure (partial results may still be added).
class DepartureSource {
public:
  virtual ~DepartureSource() {}
  virtual bool fetch(std::vector<Departure>& out) = 0;
  virtual const char* name() const = 0;
};

// Wiener Linien OGD realtime monitor (countdown supplied directly by the API).
class WienerLinienSource : public DepartureSource {
public:
  WienerLinienSource(const String& rblIds, const String& lineFilter)
    : rblIds_(rblIds), lineFilter_(lineFilter) {}
  bool fetch(std::vector<Departure>& out) override;
  const char* name() const override { return "WL"; }
private:
  bool lineAllowed(const String& lineName) const;
  String rblIds_;
  String lineFilter_;
};

// ÖBB via the Scotty "liveticker" station board (fahrplan.oebb.at) — no key,
// no proxy. Station names are resolved to evaIds at first fetch. The board
// gives local clock times, so countdown is computed from the synced clock.
class OebbSource : public DepartureSource {
public:
  OebbSource(const String& stopNames, bool trainsOnly, int maxPerStop,
             const String& destinationName = "")
    : stopNames_(stopNames), destinationName_(destinationName),
      trainsOnly_(trainsOnly), maxPerStop_(maxPerStop),
      resolved_(false), destEva_(0) {}
  bool fetch(std::vector<Departure>& out) override;
  const char* name() const override { return "OEBB"; }
private:
  void resolve();                       // names -> evaIds (once)
  long resolveEva(const String& name);  // ajax-getstop lookup
  String stopNames_;
  String destinationName_;
  bool   trainsOnly_;
  int    maxPerStop_;
  bool   resolved_;
  std::vector<long> stopEvas_;
  long   destEva_;
};

// Shared helpers (departures.cpp).
bool httpGetRaw(const String& url, String& out);       // HTTPS GET -> raw body
bool httpGetJson(const String& url, JsonDocument& doc,  // GET -> parsed JSON
                 const JsonDocument* filter = nullptr, int nestingLimit = 10);
