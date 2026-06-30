#pragma once
// ----------------------------------------------------------------------------
//  Touch input layer (CYD resistive touchscreen, XPT2046).
//
//  The touch controller sits on its own SPI bus, separate from the display.
//  No display or network code lives here — main.cpp turns taps into UI actions.
// ----------------------------------------------------------------------------

// Bring up the XPT2046 on its dedicated SPI bus. Call once in setup().
void touchInit();

// Poll for a new touch. On the press edge, fills `sx`/`sy` with the touch
// position mapped to screen pixels (landscape 0..319 x 0..239), logs raw +
// mapped coordinates to serial, and returns true. Returns false otherwise.
// Edge-detected internally, so a held touch yields a single tap. Cheap to call
// every loop.
bool touchGetTap(int& sx, int& sy);
