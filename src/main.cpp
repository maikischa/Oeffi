// ----------------------------------------------------------------------------
//  Nahverkehrsanzeige — ESP32 public-transport departure board.
//
//  Orchestration only: brings up WiFi + NTP, registers the enabled providers,
//  then periodically fetches, merges and hands departures to the display.
//  Provider logic lives in departures.*, all drawing in display.*.
// ----------------------------------------------------------------------------
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <vector>
#include <algorithm>
#include "config.h"
#include "departures.h"
#include "display.h"

static std::vector<Departure> g_departures;
static std::vector<DepartureSource*> g_sources;
static uint32_t g_lastFetch = 0;

// ---------------------------------------------------------------------------
//  Source registry — enable/disable or add a provider here. Each source is a
//  static instance (constructed once, no heap churn); `#if` keeps disabled
//  providers out of the build entirely.
// ---------------------------------------------------------------------------
void registerSources() {
#if WL_ENABLED
  static WienerLinienSource wl(RBL_IDS, LINE_FILTER);
  g_sources.push_back(&wl);
#endif
#if OEBB_ENABLED
  static OebbSource oebb(OEBB_STOPS, OEBB_TRAINS_ONLY, OEBB_MAX_PER_STOP,
                         OEBB_DESTINATION);
  g_sources.push_back(&oebb);
#endif
}

// ---------------------------------------------------------------------------
//  WiFi
// ---------------------------------------------------------------------------
void connectWiFi() {
  displayStatus("WiFi: " WIFI_SSID, StatusKind::Info);
  Serial.printf("[wifi] connecting to %s\n", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[wifi] FAILED");
    displayStatus("WiFi failed - check config.h", StatusKind::Warn);
    delay(3000);
  }
}

// ---------------------------------------------------------------------------
//  Time (NTP) — providers that report clock times convert them via localtime.
// ---------------------------------------------------------------------------
void syncClock() {
  // Europe/Vienna TZ so mktime()/localtime() match the ÖBB board's local times.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3",
               "pool.ntp.org", "time.cloudflare.com");
  Serial.print("[ntp] syncing");
  uint32_t start = millis();
  while (time(nullptr) < 1700000000 && millis() - start < 10000) {
    delay(250);
    Serial.print(".");
  }
  Serial.printf(" epoch=%ld\n", (long)time(nullptr));
}

// ---------------------------------------------------------------------------
//  Fetch from all sources, merge, sort soonest-first
// ---------------------------------------------------------------------------
void fetchAll() {
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
    if (WiFi.status() != WL_CONNECTED) return;
  }

  std::vector<Departure> merged;
  for (DepartureSource* s : g_sources) {
    if (!s->fetch(merged))
      Serial.printf("[%s] fetch failed\n", s->name());
  }

  std::sort(merged.begin(), merged.end(),
            [](const Departure& a, const Departure& b) {
              return a.countdown < b.countdown;
            });

  g_departures = merged;
  Serial.printf("[board] %d departures total\n", (int)merged.size());
}

// ---------------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n[boot] Nahverkehrsanzeige");

  displayInit();
  registerSources();

  connectWiFi();
  syncClock();
  fetchAll();
  g_lastFetch = millis();
  displayBoard(g_departures);
}

void loop() {
  if (millis() - g_lastFetch >= REFRESH_INTERVAL_MS) {
    fetchAll();
    g_lastFetch = millis();
    displayBoard(g_departures);
  }
  delay(50);
}
