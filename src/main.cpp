// ----------------------------------------------------------------------------
//  Öffi — ESP32 public-transport departure board.
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
#include "settings.h"
#include "portal.h"

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
//  WiFi — credentials come from the settings store (set via the web portal).
//  Returns true once connected.
// ---------------------------------------------------------------------------
bool connectWiFi() {
  String ssid = wifiSsid();
  displayStatus("WiFi: " + ssid, StatusKind::Info);
  Serial.printf("[wifi] connecting to %s\n", ssid.c_str());
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), wifiPass().c_str());

  uint32_t start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < 20000) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[wifi] connected, IP %s\n", WiFi.localIP().toString().c_str());
    return true;
  }
  Serial.println("[wifi] FAILED");
  return false;
}

// ---------------------------------------------------------------------------
//  Provisioning — bring up the "Oeffi-Setup" AP + captive portal and spin here
//  serving it. The portal's save handler reboots the device, so this never
//  returns; the board comes back up with stored credentials.
// ---------------------------------------------------------------------------
void enterProvisioning(const String& reason = "") {
  Serial.printf("[wifi] starting setup portal%s%s\n",
                reason.length() ? " -> " : "", reason.c_str());
  displaySetupScreen("Oeffi-Setup", "192.168.4.1", reason);
  portalStartSetupAP();
  for (;;) {
    portalHandle();
    delay(5);
  }
}

// ---------------------------------------------------------------------------
//  Time (NTP) — providers that report clock times convert them via localtime.
// ---------------------------------------------------------------------------
// Returns false if the clock never synced within the timeout — a reliable proxy
// for "no real internet" (e.g. an open/captive-portal WiFi that associates but
// blocks NTP and HTTPS until you accept terms in a browser).
bool syncClock() {
  // Europe/Vienna TZ so mktime()/localtime() match the ÖBB board's local times.
  configTzTime("CET-1CEST,M3.5.0,M10.5.0/3",
               "pool.ntp.org", "time.cloudflare.com");
  Serial.print("[ntp] syncing");
  uint32_t start = millis();
  while (time(nullptr) < 1700000000 && millis() - start < 15000) {
    delay(250);
    Serial.print(".");
  }
  bool synced = time(nullptr) >= 1700000000;
  Serial.printf(" epoch=%ld%s\n", (long)time(nullptr),
                synced ? "" : " (NOT synced)");
  return synced;
}

// ---------------------------------------------------------------------------
//  Fetch from all sources, merge, sort soonest-first
// ---------------------------------------------------------------------------
void fetchAll() {
  if (WiFi.status() != WL_CONNECTED) {
    if (!connectWiFi()) return;  // transient drop; retry next refresh
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
  Serial.println("\n[boot] Oeffi");

  displayInit();
  settingsBegin();
  registerSources();

  // No stored WiFi, or it won't connect -> drop into the setup portal (never
  // returns; reboots once the user saves credentials).
  if (!wifiConfigured() || !connectWiFi())
    enterProvisioning();

  // Associated, but is the internet actually reachable? An open/captive-portal
  // network lets us join yet blocks NTP/HTTPS. A failed clock sync means no real
  // internet -> fall back to the setup portal so the user can pick another net.
  if (!syncClock())
    enterProvisioning("Kein Internet - anderes Netz waehlen");

  // Connected with working internet: bring up the always-on config server.
  portalStartConfigServer();
  fetchAll();
  g_lastFetch = millis();
  displayBoard(g_departures);
}

void loop() {
  portalHandle();  // keep the config server responsive between fetches
  if (millis() - g_lastFetch >= REFRESH_INTERVAL_MS) {
    fetchAll();
    g_lastFetch = millis();
    displayBoard(g_departures);
  }
  delay(50);
}
