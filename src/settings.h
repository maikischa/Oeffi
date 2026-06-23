#pragma once
// ----------------------------------------------------------------------------
//  Settings store: persistent user configuration in NVS (flash).
//
//  Wraps the ESP32 `Preferences` library (NVS namespace "oeffi"). This is the
//  single source of truth for every user setting — WiFi credentials, provider
//  config (stops, filters, …) and system settings (rows, refresh) are all
//  configured on-device via the web portal and live only here; there is no
//  config file. No WiFi, web or display code lives in this layer.
// ----------------------------------------------------------------------------
#include <Arduino.h>

// Open NVS once. Call in setup() before any other settings access.
void settingsBegin();

// --- WiFi credentials -------------------------------------------------------
String wifiSsid();
String wifiPass();
bool   wifiConfigured();                                   // non-empty SSID stored
void   setWifiCreds(const String& ssid, const String& pass);
void   clearWifi();                                        // wipe creds (-> setup AP)

// --- Provider config ---------------------------------------------------------
// Web-portal-editable only (disabled/empty until saved — no config.h
// fallback). Applied on the next boot (registerSources() reads these), so
// saving reboots the device.
bool   wlEnabled();
String rblIds();
String lineFilter();
void   setWlConfig(bool enabled, const String& rblIds, const String& lineFilter);

bool   oebbEnabled();
String oebbStops();
bool   oebbTrainsOnly();
int    oebbMaxPerStop();
String oebbDestination();
void   setOebbConfig(bool enabled, const String& stops, bool trainsOnly,
                      int maxPerStop, const String& destination);

// --- System settings ---------------------------------------------------------
// Board layout / refresh cadence; web-portal editable at /system. Defaults
// apply until the user saves a value.
int    maxRows();                 // departures shown on screen at once
int    refreshIntervalMs();       // how often to re-fetch (milliseconds)
void   setSystemConfig(int maxRows, int refreshIntervalMs);

// --- Generic typed accessors (foundation for later settings) ----------------
// Read a stored value, falling back to `def` (the hardcoded default the typed
// accessors above pass in) when the key is absent.
String settingStr(const char* key, const String& def);
int    settingInt(const char* key, int def);
bool   settingBool(const char* key, bool def);
void   settingSetStr(const char* key, const String& value);
void   settingSetInt(const char* key, int value);
void   settingSetBool(const char* key, bool value);
