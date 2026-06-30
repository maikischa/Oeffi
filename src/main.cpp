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
#include "departures.h"
#include "display.h"
#include "settings.h"
#include "portal.h"
#include "touch.h"

static std::vector<Departure> g_departures;
static std::vector<DepartureSource*> g_sources;
static uint32_t g_lastFetch = 0;

// On-board touch UI: the board, plus a transient gear "tool" icon that opens a
// settings/QR screen. The icon auto-hides kToolTimeoutMs after it appears.
enum UiState { STATE_BOARD, STATE_SETTINGS };
static UiState  g_uiState     = STATE_BOARD;
static bool     g_toolVisible = false;
static uint32_t g_toolShownAt = 0;
static const uint32_t kToolTimeoutMs = 5000;

// ---------------------------------------------------------------------------
//  Source registry — enable/disable or add a provider here. Each source is a
//  static instance (constructed once, no heap churn). Enabled state and
//  per-provider config are read from the settings store (web-portal editable
//  via /providers) rather than compile-time `#if`, so a config change takes
//  effect on the next reboot without reflashing.
// ---------------------------------------------------------------------------
void registerSources() {
  if (wlEnabled()) {
    static WienerLinienSource wl(rblIds(), lineFilter());
    g_sources.push_back(&wl);
  }
  if (oebbEnabled()) {
    static OebbSource oebb(oebbStops(), oebbTrainsOnly(), oebbMaxPerStop(),
                           oebbDestination());
    g_sources.push_back(&oebb);
  }
}

// ---------------------------------------------------------------------------
//  WiFi — credentials come from the settings store (set via the web portal).
//  Returns true once connected.
// ---------------------------------------------------------------------------
bool connectWiFi() {
  String ssid = wifiSsid();
  displayBoot("Connecting to " + ssid);
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

// URL of the on-device provider-config page, shown (as text + QR) on the
// empty board so the user can jump straight there.
String providersUrl() {
  return "http://" + WiFi.localIP().toString() + "/providers";
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

  settingsBegin();      // open NVS first so displayInit() can read the saved rotation
  displayInit();
  touchInit();
  displayBoot("Starting...");
  registerSources();

  // No stored WiFi, or it won't connect -> drop into the setup portal (never
  // returns; reboots once the user saves credentials).
  if (!wifiConfigured() || !connectWiFi())
    enterProvisioning();

  // Associated, but is the internet actually reachable? An open/captive-portal
  // network lets us join yet blocks NTP/HTTPS. A failed clock sync means no real
  // internet -> fall back to the setup portal so the user can pick another net.
  if (!syncClock())
    enterProvisioning("No internet - choose another network");

  // Connected with working internet: bring up the always-on config server.
  portalStartConfigServer();
  // Briefly show the reachable address — falls back to this IP if oeffi.local
  // doesn't resolve on the visitor's device/browser.
  displayBoot("oeffi.local  /  " + WiFi.localIP().toString());
  delay(3000);
  fetchAll();
  g_lastFetch = millis();
  displayBoard(g_departures, providersUrl());
}

// ---------------------------------------------------------------------------
//  On-board touch UI — a tap surfaces the gear tool icon (bottom-right) for
//  ~5 s; tapping it opens a settings/QR screen (to the config home page),
//  where any tap returns to the board.
// ---------------------------------------------------------------------------
void handleUi() {
  int tx, ty;
  const bool tapped = touchGetTap(tx, ty);

  if (g_uiState == STATE_SETTINGS) {
    if (tapped) {                     // any tap dismisses -> back to the board
      g_uiState = STATE_BOARD;
      displayBoard(g_departures, providersUrl());
    }
    return;
  }

  // STATE_BOARD: expire the tool icon after the timeout (redraw to erase it).
  if (g_toolVisible && millis() - g_toolShownAt >= kToolTimeoutMs) {
    g_toolVisible = false;
    displayBoard(g_departures, providersUrl());
  }

  if (!tapped) return;

  if (g_toolVisible && displayToolIconHit(tx, ty)) {
    g_uiState = STATE_SETTINGS;       // tapped the icon -> open settings QR
    g_toolVisible = false;
    displaySettingsScreen("http://" + WiFi.localIP().toString() + "/");
  } else {                            // any other tap (re)surfaces the icon
    g_toolVisible = true;
    g_toolShownAt = millis();
    displayShowToolIcon();
  }
}

void loop() {
  portalHandle();  // keep the config server responsive between fetches
  handleUi();      // surface/handle the on-board tool icon + settings screen
  if (g_uiState == STATE_BOARD &&
      millis() - g_lastFetch >= (uint32_t)refreshIntervalMs()) {
    fetchAll();
    g_lastFetch = millis();
    displayBoard(g_departures, providersUrl());
    g_toolVisible = false;  // board redrawn -> any visible tool icon is gone
  }
  delay(50);
}
