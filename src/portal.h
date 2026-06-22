#pragma once
// ----------------------------------------------------------------------------
//  Web portal: WiFi provisioning + always-on config server.
//
//  Owns the synchronous `WebServer` (port 80), a captive-portal `DNSServer`
//  (port 53, AP mode only) and mDNS. Two entry points:
//
//    * portalStartSetupAP()      — no/failed WiFi: brings up the "Oeffi-Setup"
//                                  access point + captive portal so the user
//                                  can pick a network and save credentials.
//    * portalStartConfigServer() — WiFi up: serves a status/reconfigure page on
//                                  the home network (http://oeffi.local/).
//
//  portalHandle() must be pumped from loop() (and the provisioning spin-loop).
//  All persistence goes through settings.*; no display code lives here.
// ----------------------------------------------------------------------------
#include <Arduino.h>

void portalStartSetupAP();       // AP "Oeffi-Setup" + captive portal
void portalStartConfigServer();  // STA-mode config/status server + mDNS
void portalHandle();             // pump DNS + HTTP; call every loop iteration
