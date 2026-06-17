// ----------------------------------------------------------------------------
//  Data layer implementation: shared HTTP helpers + the transit providers.
// ----------------------------------------------------------------------------
#include "departures.h"
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <time.h>
#include <stdlib.h>
#include <string.h>
#include <initializer_list>

// A real epoch is well past this; used to tell "NTP not synced yet".
static const time_t kClockSyncedEpoch = 1700000000;  // ~2023-11

// ÖBB Scotty productsFilter bitmasks (one bit per product class).
static const char* kOebbProductsTrains = "1111110000000000";  // trains + S-Bahn
static const char* kOebbProductsAll    = "1111111111111111";  // everything

// ---------------------------------------------------------------------------
//  Shared HTTP(S) helpers
// ---------------------------------------------------------------------------
// GET a URL and return the raw body. Follows redirects (ÖBB http->https).
bool httpGetRaw(const String& url, String& out) {
  WiFiClientSecure client;
  client.setInsecure();  // public endpoints, no cert pinning
  HTTPClient https;
  https.setTimeout(10000);
  https.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  https.setUserAgent("Oeffi-ESP32/1.0 (+personal departure board)");
  if (!https.begin(client, url)) {
    Serial.printf("[http] begin failed: %s\n", url.c_str());
    return false;
  }
  int code = https.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[http] HTTP %d for %s\n", code, url.c_str());
    https.end();
    return false;
  }
  // Read the whole body first (see note below), then hand it back.
  out = https.getString();
  https.end();
  if (out.length() == 0) {
    Serial.printf("[http] empty body for %s\n", url.c_str());
    return false;
  }
  return true;
}

// GET -> parsed JSON. Buffers the body before parsing: streaming straight from
// WiFiClientSecure can hit a momentary empty chunk that ArduinoJson reads as
// EOF (-> IncompleteInput). RAM headroom is ample.
bool httpGetJson(const String& url, JsonDocument& doc,
                 const JsonDocument* filter, int nestingLimit) {
  String payload;
  if (!httpGetRaw(url, payload)) return false;
  DeserializationError err;
  if (filter) {
    err = deserializeJson(doc, payload,
                          DeserializationOption::Filter(*filter),
                          DeserializationOption::NestingLimit(nestingLimit));
  } else {
    err = deserializeJson(doc, payload,
                          DeserializationOption::NestingLimit(nestingLimit));
  }
  if (err) {
    Serial.printf("[http] json error: %s (%d bytes)\n", err.c_str(), payload.length());
    return false;
  }
  return true;
}

// Percent-encode a string for safe use in a URL path/query.
static String urlEncode(const String& s) {
  static const char* hex = "0123456789ABCDEF";
  String out;
  out.reserve(s.length() * 3);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (isalnum((unsigned char)c) || c == '-' || c == '_' || c == '.' || c == '~') {
      out += c;
    } else {
      out += '%';
      out += hex[(c >> 4) & 0xF];
      out += hex[c & 0xF];
    }
  }
  return out;
}

// Iterate comma-separated IDs, calling fn(id) for each non-empty trimmed entry.
template <typename Fn>
static void forEachId(const String& ids, Fn fn) {
  int start = 0;
  while (start < (int)ids.length()) {
    int comma = ids.indexOf(',', start);
    if (comma < 0) comma = ids.length();
    String id = ids.substring(start, comma);
    id.trim();
    if (id.length()) fn(id);
    start = comma + 1;
  }
}

// ---------------------------------------------------------------------------
//  Wiener Linien
// ---------------------------------------------------------------------------
bool WienerLinienSource::lineAllowed(const String& lineName) const {
  if (lineFilter_.length() == 0) return true;
  String f = "," + lineFilter_ + ",";
  return f.indexOf("," + lineName + ",") >= 0;
}

bool WienerLinienSource::fetch(std::vector<Departure>& out) {
  String url =
    "https://www.wienerlinien.at/ogd_realtime/monitor?activateTrafficInfo=stoerunglang";
  forEachId(rblIds_, [&](const String& id) { url += "&rbl=" + id; });
  Serial.printf("[WL] GET %s\n", url.c_str());

  // Filter: keep only the fields we render -> small RAM footprint.
  JsonDocument filter;
  JsonObject fMon = filter["data"]["monitors"].add<JsonObject>();
  JsonObject fLine = fMon["lines"].add<JsonObject>();
  fLine["name"] = true;
  fLine["towards"] = true;
  fLine["departures"]["departure"][0]["departureTime"]["countdown"] = true;

  JsonDocument doc;
  if (!httpGetJson(url, doc, &filter, 20)) return false;

  int added = 0;
  for (JsonObject mon : doc["data"]["monitors"].as<JsonArray>()) {
    for (JsonObject line : mon["lines"].as<JsonArray>()) {
      String name = line["name"].as<String>();
      if (!lineAllowed(name)) continue;
      String towards = line["towards"].as<String>();
      towards.trim();
      for (JsonObject dep : line["departures"]["departure"].as<JsonArray>()) {
        int cd = dep["departureTime"]["countdown"] | -1;
        if (cd < 0) continue;
        out.push_back({name, towards, cd, RowStyle::Countdown, "", "", false});
        added++;
      }
    }
  }
  Serial.printf("[WL] %d departures\n", added);
  return true;
}

// ---------------------------------------------------------------------------
//  ÖBB Scotty liveticker (fahrplan.oebb.at)
// ---------------------------------------------------------------------------
// Map a Unicode code point to an ASCII-ish equivalent the GFX fonts can draw.
static String cpToAscii(long cp) {
  switch (cp) {
    case 0xC4: return "A"; case 0xD6: return "O"; case 0xDC: return "U";  // ÄÖÜ
    case 0xE4: return "a"; case 0xF6: return "o"; case 0xFC: return "u";  // äöü
    case 0xDF: return "ss";                                               // ß
    case 0xE9: case 0xE8: case 0xEA: return "e";                          // éèê
    case 0xC9: case 0xC8: return "E";
    case 0xE1: case 0xE0: case 0xE2: return "a";
    case 0xED: case 0xEC: return "i";
    case 0xF3: case 0xF2: case 0xF4: return "o";
    case 0xFA: case 0xF9: return "u";
    case 0xF1: return "n"; case 0xE7: return "c";
  }
  if (cp >= 32 && cp < 127) return String((char)cp);
  return "";  // drop anything else
}

// Decode HTML entities (Scotty returns e.g. "H&#252;tteldorf") to plain ASCII.
static String decodeHtml(String s) {
  int i;
  while ((i = s.indexOf("&#")) >= 0) {
    int semi = s.indexOf(';', i);
    if (semi < 0 || semi - i > 8) break;
    String code = s.substring(i + 2, semi);
    long cp = (code.length() && (code[0] == 'x' || code[0] == 'X'))
                ? strtol(code.c_str() + 1, nullptr, 16)
                : code.toInt();
    s = s.substring(0, i) + cpToAscii(cp) + s.substring(semi + 1);
  }
  s.replace("&amp;", "&");
  s.replace("&lt;", "<");
  s.replace("&gt;", ">");
  s.replace("&quot;", "\"");
  return s;
}

// Parse a JSON object embedded in a JS assignment (e.g. "obj = {...};").
static bool parseEmbeddedJson(const String& raw, JsonDocument& doc, int nesting) {
  int b = raw.indexOf('{');
  if (b < 0) return false;
  return !deserializeJson(doc, raw.c_str() + b,
                          DeserializationOption::NestingLimit(nesting));
}

// Resolve a station name to its numeric evaId via the Scotty station finder.
long OebbSource::resolveEva(const String& nm) {
  String url = "https://fahrplan.oebb.at/bin/ajax-getstop.exe/dn"
               "?REQ0JourneyStopsS0A=1&REQ0JourneyStopsB=1&js=true&S=" +
               urlEncode(nm);
  String raw;
  if (!httpGetRaw(url, raw)) return 0;
  JsonDocument doc;
  if (!parseEmbeddedJson(raw, doc, 8)) return 0;
  const char* ext = doc["suggestions"][0]["extId"];  // e.g. "001290401"
  return ext ? String(ext).toInt() : 0;              // -> 1290401
}

void OebbSource::resolve() {
  forEachId(stopNames_, [&](const String& nm) {
    long eva = resolveEva(nm);
    if (eva > 0) {
      stopEvas_.push_back(eva);
      Serial.printf("[OEBB] '%s' -> eva %ld\n", nm.c_str(), eva);
    } else {
      Serial.printf("[OEBB] could not resolve station '%s'\n", nm.c_str());
    }
  });
  if (destinationName_.length()) {
    destEva_ = resolveEva(destinationName_);
    Serial.printf("[OEBB] destination '%s' -> eva %ld\n",
                  destinationName_.c_str(), destEva_);
  }
  resolved_ = true;
}

bool OebbSource::fetch(std::vector<Departure>& out) {
  time_t now = time(nullptr);
  if (now < kClockSyncedEpoch) {  // clock not synced yet -> can't compute times
    Serial.println("[OEBB] clock not synced, skipping");
    return false;
  }
  if (!resolved_) resolve();
  if (stopEvas_.empty()) return false;

  // Current Vienna wall-clock for the board query (TZ set in syncClock()).
  struct tm lt;
  localtime_r(&now, &lt);
  char tbuf[6], dbuf[11];
  strftime(tbuf, sizeof(tbuf), "%H:%M", &lt);
  strftime(dbuf, sizeof(dbuf), "%d.%m.%Y", &lt);
  const char* pf = trainsOnly_ ? kOebbProductsTrains : kOebbProductsAll;

  bool ok = true;
  for (long eva : stopEvas_) {
    String url = "https://fahrplan.oebb.at/bin/stboard.exe/dn"
                 "?L=vs_scotty.vs_liveticker&boardType=dep&outputMode=tickerDataOnly"
                 "&start=yes&selectDate=period&additionalTime=0"
                 "&evaId=" + String(eva) +
                 "&maxJourneys=" + String(maxPerStop_ + 3) +
                 "&productsFilter=" + pf +
                 "&time=" + tbuf + "&dateBegin=" + dbuf + "&dateEnd=" + dbuf;
    if (destEva_ > 0) url += "&dirInput=" + String(destEva_);

    String raw;
    if (!httpGetRaw(url, raw)) { ok = false; continue; }
    JsonDocument doc;
    if (!parseEmbeddedJson(raw, doc, 12)) {
      Serial.printf("[OEBB] parse failed for eva %ld\n", eva);
      ok = false; continue;
    }

    int added = 0;
    for (JsonObject j : doc["journey"].as<JsonArray>()) {
      if (added >= maxPerStop_) break;

      // Skip cancelled departures (rt.status set to a non-empty string).
      const char* status = j["rt"]["status"];
      if (status && status[0]) continue;

      const char* da = j["da"];
      const char* dlt = j["rt"]["dlt"];   // realtime (actual) departure time
      const char* ti  = j["ti"];          // planned time
      const char* tim = (dlt && dlt[0]) ? dlt : ti;
      if (!da || !tim || strlen(tim) < 4) continue;

      struct tm dt = {};
      dt.tm_year = String(da).substring(6, 10).toInt() - 1900;
      dt.tm_mon  = String(da).substring(3, 5).toInt() - 1;
      dt.tm_mday = String(da).substring(0, 2).toInt();
      dt.tm_hour = String(tim).substring(0, 2).toInt();
      dt.tm_min  = String(tim).substring(3, 5).toInt();
      dt.tm_isdst = -1;
      time_t dep = mktime(&dt);           // Vienna local -> epoch
      int cd = (int)((dep - now) / 60);
      if (cd < 0) cd = 0;

      String line = String((const char*)(j["pr"] | "")); line.trim();
      String towards = decodeHtml(String((const char*)(j["st"] | "")));
      // Drop the redundant station-type suffix so names fit the row.
      for (const char* suf : {" Bahnhof", " Bahnhst", " Bahnhst.", " Bf"}) {
        if (towards.endsWith(suf)) { towards.remove(towards.length() - strlen(suf)); break; }
      }

      String planned = ti ? String(ti) : String("");
      String actual  = (dlt && dlt[0]) ? String(dlt) : planned;
      bool delayed   = planned.length() && actual.length() && actual != planned;

      out.push_back({line, towards, cd, RowStyle::Clock, planned, actual, delayed});
      added++;
    }
    Serial.printf("[OEBB] eva %ld: %d departures\n", eva, added);
  }
  return ok;
}
