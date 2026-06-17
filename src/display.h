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

// Render the departure board. Rows are drawn per `Departure::style`.
void displayBoard(const std::vector<Departure>& deps);
