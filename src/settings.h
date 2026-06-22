#pragma once
// ----------------------------------------------------------------------------
//  Settings store: persistent user configuration in NVS (flash).
//
//  Wraps the ESP32 `Preferences` library (NVS namespace "oeffi"). This is the
//  runtime counterpart to the compile-time `#define`s in config.h: those now
//  serve as *seed defaults*, while values saved here (via the web portal)
//  override them. No WiFi, web or display code lives in this layer.
//
//  Step 1 stores only the WiFi credentials; the generic getters below let
//  later steps add stops/providers/refresh as one-line accessors.
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

// --- Generic typed accessors (foundation for later settings) ----------------
// Read a stored value, falling back to `def` (typically a config.h default)
// when the key is absent.
String settingStr(const char* key, const String& def);
int    settingInt(const char* key, int def);
bool   settingBool(const char* key, bool def);
void   settingSetStr(const char* key, const String& value);
void   settingSetInt(const char* key, int value);
void   settingSetBool(const char* key, bool value);
