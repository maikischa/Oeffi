// ----------------------------------------------------------------------------
//  Settings store implementation — NVS via Preferences (namespace "oeffi").
// ----------------------------------------------------------------------------
#include "settings.h"
#include <Preferences.h>

static Preferences g_prefs;

// NVS keys are limited to 15 chars.
static const char* kKeyWifiSsid = "wifi_ssid";
static const char* kKeyWifiPass = "wifi_pass";
static const char* kKeyWlEnabled = "wl_enabled";
static const char* kKeyRblIds = "rbl_ids";
static const char* kKeyLineFilter = "line_filter";
static const char* kKeyOebbEnabled = "oebb_enabled";
static const char* kKeyOebbStops = "oebb_stops";
static const char* kKeyOebbTrains = "oebb_trains";
static const char* kKeyOebbMax = "oebb_max";
static const char* kKeyOebbDest = "oebb_dest";
static const char* kKeyMaxRows = "max_rows";
static const char* kKeyRefreshMs = "refresh_ms";
static const char* kKeyRotation = "rotation";

void settingsBegin() {
  // false = read/write. The namespace is created on first write.
  g_prefs.begin("oeffi", false);
}

// --- Generic typed accessors ------------------------------------------------
String settingStr(const char* key, const String& def) {
  return g_prefs.getString(key, def);
}
int settingInt(const char* key, int def) {
  return g_prefs.getInt(key, def);
}
bool settingBool(const char* key, bool def) {
  return g_prefs.getBool(key, def);
}
void settingSetStr(const char* key, const String& value) {
  g_prefs.putString(key, value);
}
void settingSetInt(const char* key, int value) {
  g_prefs.putInt(key, value);
}
void settingSetBool(const char* key, bool value) {
  g_prefs.putBool(key, value);
}

// --- WiFi credentials -------------------------------------------------------
// Configured entirely on-device via the portal; "" until the user saves one.
String wifiSsid() { return settingStr(kKeyWifiSsid, ""); }
String wifiPass() { return settingStr(kKeyWifiPass, ""); }

bool wifiConfigured() { return wifiSsid().length() > 0; }

void setWifiCreds(const String& ssid, const String& pass) {
  settingSetStr(kKeyWifiSsid, ssid);
  settingSetStr(kKeyWifiPass, pass);
}

void clearWifi() {
  g_prefs.remove(kKeyWifiSsid);
  g_prefs.remove(kKeyWifiPass);
}

// --- Provider config ----------------------------------------------------------
// Configured entirely on-device via the portal; disabled/empty until the
// user saves a config at /providers.
bool wlEnabled()    { return settingBool(kKeyWlEnabled, false); }
String rblIds()     { return settingStr(kKeyRblIds, ""); }
String lineFilter() { return settingStr(kKeyLineFilter, ""); }

void setWlConfig(bool enabled, const String& rblIds, const String& lineFilter) {
  settingSetBool(kKeyWlEnabled, enabled);
  settingSetStr(kKeyRblIds, rblIds);
  settingSetStr(kKeyLineFilter, lineFilter);
}

bool oebbEnabled()        { return settingBool(kKeyOebbEnabled, false); }
String oebbStops()        { return settingStr(kKeyOebbStops, ""); }
bool oebbTrainsOnly()     { return settingBool(kKeyOebbTrains, true); }
int oebbMaxPerStop()      { return settingInt(kKeyOebbMax, 6); }
String oebbDestination()  { return settingStr(kKeyOebbDest, ""); }

void setOebbConfig(bool enabled, const String& stops, bool trainsOnly,
                    int maxPerStop, const String& destination) {
  settingSetBool(kKeyOebbEnabled, enabled);
  settingSetStr(kKeyOebbStops, stops);
  settingSetBool(kKeyOebbTrains, trainsOnly);
  settingSetInt(kKeyOebbMax, maxPerStop);
  settingSetStr(kKeyOebbDest, destination);
}

// --- System settings ---------------------------------------------------------
int maxRows()           { return settingInt(kKeyMaxRows, 4); }
int refreshIntervalMs() { return settingInt(kKeyRefreshMs, 30000); }
int displayRotation()   { return settingInt(kKeyRotation, 3); }

void setSystemConfig(int maxRows, int refreshIntervalMs, int displayRotation) {
  settingSetInt(kKeyMaxRows, maxRows);
  settingSetInt(kKeyRefreshMs, refreshIntervalMs);
  settingSetInt(kKeyRotation, displayRotation);
}
