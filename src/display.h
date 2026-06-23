#pragma once
// ----------------------------------------------------------------------------
//  Presentation layer.
//
//  Owns the TFT and draws the board. The only input is a list of `Departure`s;
//  each row is rendered according to its `RowStyle`. No network code lives here.
// ----------------------------------------------------------------------------
#include <Arduino.h>
#include <vector>
#include "departures.h"

// Full-screen status message kind (maps to a colour inside the display).
enum class StatusKind { Info, Warn };

// Bring up the TFT (rotation, clear). Call once in setup().
void displayInit();

// Show a centred status message (boot / WiFi / errors).
void displayStatus(const String& msg, StatusKind kind = StatusKind::Info);

// WiFi-provisioning instructions: how to join the setup AP and open the portal.
// `reason` (optional) is shown as a warning line above the title — used when we
// drop back into setup because the joined network has no working internet.
void displaySetupScreen(const String& apName, const String& ip,
                        const String& reason = "");

// Render the departure board. Rows are drawn per `Departure::style`. When
// `deps` is empty, an "add a provider" screen is shown instead; `configUrl`
// (e.g. "http://192.168.0.41/providers") is rendered there as scannable text
// + QR so the user can jump straight to the config page.
void displayBoard(const std::vector<Departure>& deps,
                  const String& configUrl = "");
