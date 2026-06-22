// ----------------------------------------------------------------------------
//  Settings store implementation — NVS via Preferences (namespace "oeffi").
// ----------------------------------------------------------------------------
#include "settings.h"
#include <Preferences.h>
#include "config.h"

static Preferences g_prefs;

// NVS keys are limited to 15 chars.
static const char* kKeyWifiSsid = "wifi_ssid";
static const char* kKeyWifiPass = "wifi_pass";

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
// Seeded from config.h (WIFI_SSID/WIFI_PASS) so an existing flashed config keeps
// working; once saved via the portal, the stored value wins.
String wifiSsid() { return settingStr(kKeyWifiSsid, WIFI_SSID); }
String wifiPass() { return settingStr(kKeyWifiPass, WIFI_PASS); }

bool wifiConfigured() { return wifiSsid().length() > 0; }

void setWifiCreds(const String& ssid, const String& pass) {
  settingSetStr(kKeyWifiSsid, ssid);
  settingSetStr(kKeyWifiPass, pass);
}

void clearWifi() {
  g_prefs.remove(kKeyWifiSsid);
  g_prefs.remove(kKeyWifiPass);
}
