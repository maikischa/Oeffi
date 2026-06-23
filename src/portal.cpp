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
  "form{margin-bottom:22px}"
  "fieldset{border:1px solid #334;border-radius:8px;padding:4px 14px 14px;"
  "margin:0 0 18px}legend{padding:0 6px;color:#E0C430;font-weight:600}"
  "input[type=checkbox]{width:auto;padding:0;margin:0 8px 0 0;vertical-align:middle;"
  "border:0;background:none}"
  "::placeholder{color:#4a5168;font-style:italic;opacity:1}"
  "a{color:#E0C430}"
  ".status{display:flex;align-items:center;gap:8px;margin:18px 0 22px;color:#c8ccd8}"
  ".dot{width:9px;height:9px;border-radius:50%;background:#3ddc84;flex:0 0 auto}"
  "code{font-family:ui-monospace,monospace;background:#161c30;padding:2px 7px;"
  "border-radius:5px;font-size:13px}"
  "nav a{display:block;padding:14px 16px;background:#161c30;border:1px solid #334;"
  "border-radius:8px;margin-bottom:10px;font-weight:600;text-decoration:none}"
  "nav a:hover{border-color:#E0C430}"
  "footer{margin-top:28px;padding-top:16px;border-top:1px solid #223;"
  "font-size:13px;color:#8890a8}footer a{color:#8890a8}"
  "</style></head><body>";
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
  page += "<h1>Öffi WiFi Setup</h1>"
          "<p class=muted>Choose your 2.4 GHz network and enter the "
          "password.</p>"
          "<form method=POST action=/save>"
          "<label>Network</label><select name=ssid>" + opts + "</select>"
          "<label>Password</label>"
          "<input name=pass type=password placeholder='WiFi password'>"
          "<button type=submit>Save &amp; connect</button></form>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

// A "device is restarting" page that keeps retrying `target` in the
// background and jumps there the moment it answers — instead of leaving the
// visitor stuck on a static "please wait" message. Works automatically when
// the browser stays on the same WiFi network as the device; otherwise the
// `hint` text tells the visitor what to do manually.
static String restartingPage(const String& title, const String& hint,
                              const String& target) {
  String page = FPSTR(kHead);
  page += "<h1>" + title + "</h1><p>" + hint + "</p>"
          "<p class=muted id=st>Waiting for restart…</p>"
          "<script>"
          "var t='" + target + "';"
          "function poll(){"
            "fetch(t,{mode:'no-cors',cache:'no-store'}).then(function(){"
              "location.href=t;"
            "}).catch(function(){"
              "document.getElementById('st').textContent='Waiting for restart… (not reachable yet)';"
              "setTimeout(poll,3000);"
            "});"
          "}"
          "setTimeout(poll,6000);"
          "</script>";
  page += FPSTR(kFoot);
  return page;
}

static void handleSave() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length() == 0) { server.sendHeader("Location", "/"); server.send(302); return; }

  setWifiCreds(ssid, pass);

  server.send(200, "text/html", restartingPage(
      "Saved",
      "Connecting to <b>" + esc(ssid) + "</b>… The device is restarting and "
      "will then be reachable at <b>http://oeffi.local/</b> once your device "
      "is back on the same network.",
      "http://oeffi.local/"));

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
          "<p class=status><span class=dot></span> Connected to <b>" +
          esc(wifiSsid()) + "</b> &middot; <code>" +
          WiFi.localIP().toString() + "</code></p>"
          "<nav>"
          "<a href=/providers>Configure providers &rarr;</a>"
          "<a href=/wifi>Configure WiFi &rarr;</a>"
          "<a href=/system>System settings &rarr;</a>"
          "</nav>"
          "<footer>Öffi public-transport board &middot; "
          "<a href=https://github.com/maikischa/Oeffi target=_blank rel=noopener>"
          "GitHub &#8599;</a></footer>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

static void handleWifiRoot() {
  String page = FPSTR(kHead);
  page += "<h1>WiFi</h1>"
          "<p class=muted>Connected to <b>" + esc(wifiSsid()) + "</b><br>"
          "IP: " + WiFi.localIP().toString() + "</p>"
          "<form method=POST action=/wifi>"
          "<label>Change WiFi — new network (SSID)</label>"
          "<input name=ssid placeholder='SSID' value='" + esc(wifiSsid()) + "'>"
          "<label>Password</label>"
          "<input name=pass type=password placeholder='leave blank to keep current'>"
          "<button type=submit>Save &amp; restart</button></form>"
          "<form method=POST action=/forget>"
          "<button type=submit style='background:#b04030;color:#fff'>"
          "Forget WiFi</button></form>"
          "<p class=muted><a href=/>&larr; Back</a></p>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

static void handleWifiChange() {
  String ssid = server.arg("ssid");
  String pass = server.arg("pass");
  if (ssid.length()) {
    if (pass.length() == 0) pass = wifiPass();  // blank = keep current password
    setWifiCreds(ssid, pass);
    server.send(200, "text/html", restartingPage(
        "Saved",
        "Connecting to <b>" + esc(ssid) + "</b>… This page reloads "
        "automatically once the device is reachable again (if you changed "
        "networks, reconnect yourself first).",
        "http://oeffi.local/"));
    delay(800);
    ESP.restart();
  } else {
    server.sendHeader("Location", "/");
    server.send(302);
  }
}

static void handleForget() {
  clearWifi();
  server.send(200, "text/html", restartingPage(
      "WiFi forgotten",
      "The device is restarting into setup mode. Connect your device to the "
      "<b>Oeffi-Setup</b> WiFi network (open, no password) — this page will "
      "then jump to setup automatically.",
      "http://192.168.4.1/"));
  delay(800);
  ESP.restart();
}

static void handleProvidersRoot() {
  String page = FPSTR(kHead);
  page += "<h1>Providers</h1>"
          "<form method=POST action=/providers>"
          "<fieldset><legend><input type=checkbox name=wl_enabled value=1" +
            String(wlEnabled() ? " checked" : "") + "> Wiener Linien</legend>"
          "<label>Stop IDs (RBL, comma-separated — see "
          "<a href=https://till.mabe.at/rbl/ target=_blank rel=noopener>"
          "till.mabe.at/rbl</a>)</label>"
          "<input name=rbl_ids value='" + esc(rblIds()) + "' placeholder='4616,4613'>"
          "<label>Line filter (optional, comma-separated)</label>"
          "<input name=line_filter value='" + esc(lineFilter()) + "' placeholder='D,2,U2'>"
          "</fieldset>"
          "<fieldset><legend><input type=checkbox name=oebb_enabled value=1" +
            String(oebbEnabled() ? " checked" : "") + "> ÖBB</legend>"
          "<label>Stations (comma-separated)</label>"
          "<input name=oebb_stops value='" + esc(oebbStops()) + "' placeholder='Wien Hbf'>"
          "<label><input type=checkbox name=oebb_trains value=1" +
            String(oebbTrainsOnly() ? " checked" : "") +
            "> Trains/S-Bahn only (no bus/tram/subway)</label>"
          "<label>Max departures per station</label>"
          "<input name=oebb_max type=number min=1 max=20 value='" +
            String(oebbMaxPerStop()) + "'>"
          "<label>Direction filter (optional)</label>"
          "<input name=oebb_dest value='" + esc(oebbDestination()) +
            "' placeholder='Flughafen Wien'>"
          "</fieldset>"
          "<button type=submit>Save &amp; restart</button></form>"
          "<p class=muted><a href=/>&larr; Back</a></p>"
          // Auto-tick a provider's enable box the moment any of its fields is
          // edited, so the user can't fill a stop in and forget to enable it.
          "<script>"
          "document.querySelectorAll('fieldset').forEach(function(fs){"
            "var en=fs.querySelector('legend input[type=checkbox]');"
            "if(!en)return;"
            "fs.querySelectorAll('input,select').forEach(function(el){"
              "if(el===en)return;"
              "el.addEventListener('input',function(){en.checked=true;});"
            "});"
          "});"
          "</script>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

static void handleProvidersSave() {
  setWlConfig(server.hasArg("wl_enabled"), server.arg("rbl_ids"),
              server.arg("line_filter"));

  int maxPerStop = server.arg("oebb_max").toInt();
  if (maxPerStop < 1) maxPerStop = 1;
  if (maxPerStop > 20) maxPerStop = 20;
  setOebbConfig(server.hasArg("oebb_enabled"), server.arg("oebb_stops"),
                server.hasArg("oebb_trains"), maxPerStop,
                server.arg("oebb_dest"));

  server.send(200, "text/html", restartingPage(
      "Saved",
      "Provider configuration saved. The device is restarting…",
      "http://oeffi.local/"));
  delay(800);
  ESP.restart();
}

static void handleSystemRoot() {
  String page = FPSTR(kHead);
  page += "<h1>System</h1>"
          "<form method=POST action=/system>"
          "<label>Departures shown on screen at once (1–8; 3–4 looks best)</label>"
          "<input name=max_rows type=number min=1 max=8 value='" +
            String(maxRows()) + "'>"
          "<label>Refresh interval (seconds)</label>"
          "<input name=refresh_s type=number min=10 max=3600 value='" +
            String(refreshIntervalMs() / 1000) + "'>"
          "<button type=submit>Save &amp; restart</button></form>"
          "<p class=muted><a href=/>&larr; Back</a></p>";
  page += FPSTR(kFoot);
  server.send(200, "text/html", page);
}

static void handleSystemSave() {
  int rows = server.arg("max_rows").toInt();
  if (rows < 1) rows = 1;
  if (rows > 8) rows = 8;

  int secs = server.arg("refresh_s").toInt();
  if (secs < 10) secs = 10;
  if (secs > 3600) secs = 3600;

  setSystemConfig(rows, secs * 1000);

  server.send(200, "text/html", restartingPage(
      "Saved",
      "System settings saved. The device is restarting…",
      "http://oeffi.local/"));
  delay(800);
  ESP.restart();
}

void portalStartConfigServer() {
  if (MDNS.begin(kHostname))
    MDNS.addService("http", "tcp", 80);

  server.on("/", HTTP_GET, handleStatusRoot);
  server.on("/wifi", HTTP_GET, handleWifiRoot);
  server.on("/wifi", HTTP_POST, handleWifiChange);
  server.on("/forget", HTTP_POST, handleForget);
  server.on("/providers", HTTP_GET, handleProvidersRoot);
  server.on("/providers", HTTP_POST, handleProvidersSave);
  server.on("/system", HTTP_GET, handleSystemRoot);
  server.on("/system", HTTP_POST, handleSystemSave);
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
