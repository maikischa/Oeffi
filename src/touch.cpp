// ----------------------------------------------------------------------------
//  Touch input — CYD resistive touchscreen (XPT2046) on its own SPI bus.
// ----------------------------------------------------------------------------
#include "touch.h"
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include "settings.h"

// CYD touch pins (see the pin map in CLAUDE.md). These are NOT the display's
// SPI pins — the touch controller is wired to a second, independent bus.
static const int kTouchSclk = 25;
static const int kTouchMiso = 39;
static const int kTouchMosi = 32;
static const int kTouchCs   = 33;
static const int kTouchIrq  = 36;

// The display (TFT_eSPI) owns VSPI; give the touch controller its own HSPI bus
// so the two peripherals never contend for the wire.
static SPIClass            touchSPI(HSPI);
static XPT2046_Touchscreen touch(kTouchCs, kTouchIrq);

static bool g_wasTouched = false;  // previous poll's state, for press-edge detect

// Raw-to-pixel calibration. The XPT2046 returns ~0..4095 ADC counts; these four
// values are the raw readings at the screen edges in landscape. Tune them from
// the "[touch] raw=(x,y)" serial log if taps land off-target (swap a pair to
// flip an axis; rotation 1 vs 3 also inverts both). The display is 320x240.
static const int kScreenW = 320;
static const int kScreenH = 240;
static const int kRawX0 = 200,  kRawX1 = 3700;  // raw X at screen x = 0 .. W-1
static const int kRawY0 = 240,  kRawY1 = 3800;  // raw Y at screen y = 0 .. H-1

void touchInit() {
  touchSPI.begin(kTouchSclk, kTouchMiso, kTouchMosi, kTouchCs);
  touch.begin(touchSPI);
  touch.setRotation(displayRotation());  // match the display (set at /system)
}

bool touchGetTap(int& sx, int& sy) {
  const bool down = touch.touched();
  bool tap = false;
  if (down && !g_wasTouched) {            // fire once, on the press edge only
    TS_Point p = touch.getPoint();
    sx = constrain(map(p.x, kRawX0, kRawX1, 0, kScreenW - 1), 0, kScreenW - 1);
    sy = constrain(map(p.y, kRawY0, kRawY1, 0, kScreenH - 1), 0, kScreenH - 1);
    Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d)\n", p.x, p.y, sx, sy);
    tap = true;
  }
  g_wasTouched = down;
  return tap;
}
