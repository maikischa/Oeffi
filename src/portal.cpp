// ----------------------------------------------------------------------------
//  Web portal implementation — captive-portal provisioning + config server.
//
//  Uses only arduino-esp32 built-ins (WebServer, DNSServer, ESPmDNS); no extra
//  lib_deps. Page chrome lives in PROGMEM (flash) to keep it off the heap; only
//  the small dynamic bits (scanned SSID list, current values) are built in RAM.
// ----------------------------------------------------------------------------
#include "portal.h"
#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include "settings.h"

static WebServer server(80);
static DNSServer dns;
static bool g_dnsActive = false;  // true only while the setup AP is up

static const IPAddress kApIp(192, 168, 4, 1);
static const char* kApSsid = "Oeffi-Setup";
static const char* kHostname = "oeffi";

// --- Shared page chrome (flash) ---------------------------------------------
static const char kHead[] PROGMEM =
  "<!doctype html><html><head><meta charset=utf-8>"
  "<meta name=viewport content='width=device-width,initial-scale=1'>"
  "<title>Oeffi</title><style>"
  "body{font-family:system-ui,sans-serif;background:#0b1020;color:#e0e0e0;"
  "margin:0;padding:24px;max-width:480px;margin:auto}"
  "h1{color:#E0C430;font-size:22px}label{display:block;margin:14px 0 4px}"
  "input,select,button{width:100%;box-sizing:border-box;padding:11px;"
  "font-size:16px;border-radius:8px;border:1px solid #334;background:#161c30;"
  "color:#e0e0e0}button{background:#E0C430;color:#111;border:0;font-weight:600;"
  "margin-top:18px}.muted{color:#8890a8;font-size:14px}"
  "form{margin-bottom:22px}</style></head><body>";
static const char kFoot[] PROGMEM = "</body></html>";

// HTML-escape for echoing user/scan values into attributes & text.
static String esc(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (char c : s) {
    switch (c) {
      case '&': o += "&amp;"; break;
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '"': o += "&quot;"; break;
      case '\'': o += "&#39;"; break;
      default: o += c;
    }
  }
  return o;
}

// --- Setup AP (provisioning) ------------------------------------------------
static void handleSetupRoot() {
  int n = WiFi.scanNetworks();  // synchronous; fine, nothing else runs in AP mode
  String opts;
  for (int i = 0; i < n; i++) {
    String ssid = WiFi.SSID(i);
    if (ssid.length() == 0) continue;
    opts += "<option value='" + esc(ssid) + "'>" + esc(ssid) +
            " (" + String(WiFi.RSSI(i)) + " dBm)</option>";
  }
  WiFi.scanDelete();

  String page = FPSTR(kHead);
  page += "<h1>Öffi WiFi-Setup</h1>"
          "<p class=muted>Wählen Sie Ihr 2,4-GHz-Netz und geben Sie das "
          "Passwort ein.</p>"
          "<form method=POST action=/save>"
          "<label>Netzwerk</label><select name=ssid>" + opts + "</select>"
          "<label>Passwort</label>"
          "<input name=pass type=password placeholder='WLAN-Passwort'>"
          "<button type=submit>Speichern &amp; verbinden</button></form>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) { server.sendHeader("Location", "/"); server.send(302); return; }

  setWifiCreds(ssid, pass);

  String page = FPSTR(kHead);
  page += "<h1>Gespeichert</h1><p>Verbinde mit <b>" + esc(ssid) +
          "</b>… Das Gerät startet neu.</p>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);

  delay(800);       // let the response flush before we reboot
  ESP.restart();
}

// Send any captive-portal probe to the setup page so phones auto-open it.
static void handleCaptiveRedirect() {
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}

void portalStartSetupAP() {
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(kApIp, kApIp, IPAddress(255, 255, 255, 0));
  WiFi.softAP(kApSsid);           // open network
  delay(100);

  dns.start(53, "*", kApIp);      // catch-all DNS -> captive portal
  g_dnsActive = true;

  server.on("/", HTTP_GET, handleSetupRoot);
  server.on("/save", HTTP_POST, handleSave);
  // Common OS connectivity-check endpoints -> redirect into the portal.
  server.on("/generate_204", handleCaptiveRedirect);
  server.on("/gen_204", handleCaptiveRedirect);
  server.on("/hotspot-detect.html", handleCaptiveRedirect);
  server.on("/ncsi.txt", handleCaptiveRedirect);
  server.onNotFound(handleCaptiveRedirect);
  server.begin();

  Serial.printf("[portal] setup AP '%s' up, http://%s/\n",
                kApSsid, kApIp.toString().c_str());
}

// --- Config server (STA mode) -----------------------------------------------
static void handleStatusRoot() {
  String page = FPSTR(kHead);
  page += "<h1>Öffi</h1>"
          "<p class=muted>Verbunden mit <b>" + esc(wifiSsid()) + "</b><br>"
          "IP: " + WiFi.localIP().toString() + "</p>"
          "<form method=POST action=/wifi>"
          "<label>WLAN ändern — neues Netzwerk (SSID)</label>"
          "<input name=ssid placeholder='SSID' value='" + esc(wifiSsid()) + "'>"
          "<label>Passwort</label>"
          "<input name=pass type=password placeholder='unverändert lassen = neu eingeben'>"
          "<button type=submit>Speichern &amp; neu starten</button></form>"
          "<form method=POST action=/forget>"
          "<button type=submit style='background:#b04030;color:#fff'>"
          "WLAN vergessen</button></form>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

static void handleWifiChange() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length()) {
    setWifiCreds(ssid, pass);
    server.send(200, "text/html",
                String(FPSTR(kHead)) + "<h1>Gespeichert</h1>"
                "<p>Neustart…</p>" + String(FPSTR(kFoot)));
    delay(800);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/");
    server.send(302);
  }
}

static void handleForget() {
  clearWifi();
  server.send(200, "text/html",
              String(FPSTR(kHead)) + "<h1>WLAN vergessen</h1>"
              "<p>Neustart in den Setup-Modus…</p>" + String(FPSTR(kFoot)));
  delay(800);
  ESP.restart();
}

void portalStartConfigServer() {
  if (MDNS.begin(kHostname))
    MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleStatusRoot);
  server.on("/wifi", HTTP_POST, handleWifiChange);
  server.on("/forget", HTTP_POST, handleForget);
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();

  Serial.printf("[portal] config server up, http://%s.local/ (%s)\n",
                kHostname, WiFi.localIP().toString().c_str());
}

// --- Pump -------------------------------------------------------------------
void portalHandle() {
  if (g_dnsActive) dns.processNextRequest();
  server.handleClient();
}
