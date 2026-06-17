#pragma once
// ============================================================================
//  Öffi — user configuration
//  ----------------------------------------------------------------------------
//  Copy this file to "config.h" (same folder) and fill in your values:
//
//      cp src/config.example.h src/config.h
//
//  config.h is git-ignored so your WiFi password is never committed.
// ============================================================================

// --- WiFi -------------------------------------------------------------------
// Note: the ESP32 only supports 2.4 GHz networks (not 5 GHz).
#define WIFI_SSID   "YOUR_WIFI_NAME"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

// --- Board ------------------------------------------------------------------
// Departures shown on screen at once. The landscape layout looks best with 3-4.
#define MAX_ROWS             4
// How often to refresh data from the providers (milliseconds).
#define REFRESH_INTERVAL_MS  30000

// ============================================================================
//  Providers — enable the ones you want; each is independent.
// ============================================================================

// --- Wiener Linien (Vienna trams / U-Bahn / bus) ----------------------------
#define WL_ENABLED  1
// Stop ID(s) ("RBL"), comma-separated. Find yours at https://till.mabe.at/rbl/
// Example: "4616,4613"
#define RBL_IDS     "4616,4613"
// Optional: only show these line names (comma-separated, e.g. "D,2,U2").
// Leave "" to show every line at the stop(s).
#define LINE_FILTER ""

// --- ÖBB (Austrian railways: trains / S-Bahn) -------------------------------
// Data comes straight from ÖBB's Scotty backend — no API key, no proxy.
#define OEBB_ENABLED      1
// Station name(s), comma-separated. Type them as you would in Scotty; they are
// resolved to IDs automatically at boot. Example: "Wien Mitte" or "Wien Hbf"
#define OEBB_STOPS        "Wien Hbf"
// 1 = trains/S-Bahn only (hide bus, tram, subway). 0 = everything at the stop.
#define OEBB_TRAINS_ONLY  1
// Max departures to pull per ÖBB stop.
#define OEBB_MAX_PER_STOP 6
// Optional: only show trains heading via this station (native direction filter,
// includes pass-through trains). Leave "" to disable. Example: "Flughafen Wien"
#define OEBB_DESTINATION  ""
