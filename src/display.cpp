// ----------------------------------------------------------------------------
//  Presentation layer implementation: palette, text helpers and the per-style
//  row renderers. To add a new row look, add a renderer and an entry to
//  kRenderers[] (see RowStyle in departures.h).
// ----------------------------------------------------------------------------
#include "display.h"
#include <TFT_eSPI.h>
#include <qrcode.h>
#include <algorithm>
#include "settings.h"

// ---------------------------------------------------------------------------
//  Palette
// ---------------------------------------------------------------------------
static TFT_eSPI tft = TFT_eSPI();

#define COL_BG      tft.color565(0x00, 0x05, 0x00)  // near-black board background
#define COL_AMBER   tft.color565(0xE0, 0xC4, 0x30)  // Wiener Linien amber/gold
#define COL_DIM     tft.color565(0x60, 0x60, 0x40)  // separators / muted text
#define COL_OEBB_BG tft.color565(0x0B, 0x2E, 0x6B)  // ÖBB board blue
#define COL_DELAY   tft.color565(0xF0, 0xC0, 0x30)  // delayed clock time (amber)
#define COL_PLAN    tft.color565(0xB0, 0xB8, 0xC8)  // planned clock time (grey)

// ---------------------------------------------------------------------------
//  Text helpers
// ---------------------------------------------------------------------------
// Greedily wrap `s` into up to `maxLines` lines that each fit `maxW` px, using
// whatever font is currently selected. Last line is ellipsised if truncated.
static std::vector<String> wrapText(const String& s, int maxW, int maxLines) {
  std::vector<String> lines;
  String rest = s;
  rest.trim();
  while (rest.length() && (int)lines.size() < maxLines) {
    if (tft.textWidth(rest) <= maxW) { lines.push_back(rest); break; }

    int cut = -1;  // longest word-boundary prefix that fits
    for (int i = 0; i < (int)rest.length(); i++) {
      if (rest[i] == ' ') {
        if (tft.textWidth(rest.substring(0, i)) <= maxW) cut = i;
        else break;
      }
    }
    if (cut < 0) {  // single word too long: hard-cut by characters
      int i = rest.length();
      while (i > 1 && tft.textWidth(rest.substring(0, i)) > maxW) i--;
      cut = i;
      lines.push_back(rest.substring(0, cut));
      rest = rest.substring(cut);
    } else {
      lines.push_back(rest.substring(0, cut));
      rest = rest.substring(cut + 1);
    }
    rest.trim();
  }
  if (rest.length() && (int)lines.size() == maxLines && lines.size()) {
    String last = lines.back();
    while (last.length() > 1 && tft.textWidth(last + "...") > maxW)
      last.remove(last.length() - 1);
    lines.back() = last + "...";
  }
  return lines;
}

// Draw `towards` wrapped to <=2 lines, vertically centred at `cy`, left at `dirX`.
static void drawDirection(const String& towards, int dirX, int dirW, int cy,
                          uint16_t fg, uint16_t bg, int lineGap) {
  tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextColor(fg, bg);
  tft.setTextDatum(ML_DATUM);
  std::vector<String> dl = wrapText(towards, dirW, 2);
  int startY = cy - ((int)dl.size() - 1) * lineGap / 2;
  for (int k = 0; k < (int)dl.size(); k++)
    tft.drawString(dl[k], dirX, startY + k * lineGap);
}

// ---------------------------------------------------------------------------
//  Row renderers — one per RowStyle. Add a renderer + table entry below to
//  introduce a new provider look; existing providers are untouched.
// ---------------------------------------------------------------------------
typedef void (*RowRenderer)(const Departure&, int y, int rowH);

// RowStyle::Countdown — amber line | white direction | amber minutes (Wiener Linien).
static void renderCountdown(const Departure& d, int y, int rowH) {
  const int W = tft.width();
  const int cy = y + rowH / 2;
  const int padX = 8, lineColW = 52, cdColW = 60;
  const int dirX = padX + lineColW;

  tft.setTextColor(COL_AMBER, COL_BG);
  tft.setFreeFont(&FreeSansBold18pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(d.line, padX, cy);

  String cd = (d.countdown <= 0) ? String("0") : String(d.countdown);
  tft.setTextDatum(MR_DATUM);
  tft.drawString(cd, W - padX, cy);

  drawDirection(d.towards, dirX, W - dirX - cdColW - padX, cy, TFT_WHITE, COL_BG, 20);
}

// RowStyle::Clock — blue row, white line + destination, planned/delayed times (ÖBB).
static void renderClock(const Departure& d, int y, int rowH) {
  const int W = tft.width();
  const int cy = y + rowH / 2;
  const int padX = 6, lineColW = 96, timeColW = 56;

  tft.fillRect(0, y, W, rowH, COL_OEBB_BG);

  // Line label (left); shrink font if long (e.g. "RJX 765").
  tft.setTextColor(TFT_WHITE, COL_OEBB_BG);
  tft.setFreeFont(&FreeSansBold12pt7b);
  if (tft.textWidth(d.line) > lineColW - 6) tft.setFreeFont(&FreeSansBold9pt7b);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(d.line, padX, cy);

  // Times (right): delayed -> planned (grey) over actual (amber); else one time.
  const int tx = W - padX;
  if (d.delayed) {
    tft.setTextFont(2);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(COL_PLAN, COL_OEBB_BG);
    tft.drawString(d.planned, tx, cy - 12);
    tft.setTextColor(COL_DELAY, COL_OEBB_BG);
    tft.drawString(d.actual, tx, cy + 9);
  } else {
    tft.setFreeFont(&FreeSansBold9pt7b);
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_WHITE, COL_OEBB_BG);
    tft.drawString(d.planned, tx, cy);
  }

  const int dirX = padX + lineColW;
  drawDirection(d.towards, dirX, (W - timeColW - padX) - dirX, cy,
                TFT_WHITE, COL_OEBB_BG, 18);
}

// Indexed by RowStyle. Keep this in the same order as the enum.
static const RowRenderer kRenderers[] = {
  renderCountdown,  // RowStyle::Countdown
  renderClock,      // RowStyle::Clock
};

// ---------------------------------------------------------------------------
//  Public API
// ---------------------------------------------------------------------------
void displayInit() {
  tft.init();
  tft.setRotation(1);  // Landscape 320x240, ST7789 (use 3 if upside-down)
  tft.fillScreen(COL_BG);
}

void displayStatus(const String& msg, StatusKind kind) {
  tft.fillScreen(COL_BG);
  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(2);
  tft.setTextColor(kind == StatusKind::Warn ? TFT_RED : COL_AMBER, COL_BG);
  tft.drawString(msg, tft.width() / 2, tft.height() / 2);
}

// Backslash-escape the WiFi-QR meta chars (\ ; , : ") per the MECARD-style
// `WIFI:` payload so SSIDs/passwords with them still scan correctly.
static String qrEscape(const String& s) {
  String o;
  o.reserve(s.length() + 4);
  for (char c : s) {
    if (c == '\\' || c == ';' || c == ',' || c == ':' || c == '"') o += '\\';
    o += c;
  }
  return o;
}

// Draw a QR of `text` with the top-left of its white quiet-zone box at (x,y);
// `scale` = px per module. Returns the box side length in px.
static int drawQr(const String& text, int x, int y, int scale) {
  const uint8_t version = 3;  // 29x29 modules; holds ~53 byte chars at ECC_LOW
  QRCode qr;
  uint8_t data[qrcode_getBufferSize(version)];
  qrcode_initText(&qr, data, version, ECC_LOW, text.c_str());

  const int quiet = 2;  // white border (modules) — required for reliable scans
  const int dim = (qr.size + 2 * quiet) * scale;
  tft.fillRect(x, y, dim, dim, TFT_WHITE);
  for (uint8_t my = 0; my < qr.size; my++)
    for (uint8_t mx = 0; mx < qr.size; mx++)
      if (qrcode_getModule(&qr, mx, my))
        tft.fillRect(x + (mx + quiet) * scale, y + (my + quiet) * scale,
                     scale, scale, TFT_BLACK);
  return dim;
}

void displaySetupScreen(const String& apName, const String& ip,
                        const String& reason) {
  const int W = tft.width();

  tft.fillScreen(COL_BG);

  // QR (right): "WIFI:" join code for the open setup AP. Scanning it joins the
  // network; the captive portal then auto-opens the config page.
  const int scale = 4;
  const int qDim = (29 + 4) * scale;        // version-3 box side
  const int qx = W - qDim - 6;
  const int qy = 8;
  drawQr("WIFI:T:nopass;S:" + qrEscape(apName) + ";;", qx, qy, scale);

  // Instructions (left column).
  tft.setTextDatum(TL_DATUM);
  const int tx = 8;

  tft.setTextFont(4);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.drawString("WiFi Setup", tx, 12);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, COL_BG);
  tft.drawString("Scan QR to join,", tx, 54);
  tft.drawString("or join WiFi:", tx, 76);
  tft.setTextFont(4);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.drawString(apName, tx, 98);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, COL_BG);
  tft.drawString("Then open:", tx, 138);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.drawString(ip, tx, 160);

  // Reason (bottom strip, full width) — why we dropped back into setup.
  if (reason.length()) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(TFT_RED, COL_BG);
    tft.drawString(reason, W / 2, 222);
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);  // restore default
}

// ---------------------------------------------------------------------------
//  On-board controls: the "tool" (settings) button + the settings/QR screen.
//  Geometry and hit-testing live here so the display stays the single owner of
//  the screen layout; main.cpp only asks "did this tap hit a button?".
// ---------------------------------------------------------------------------
static const int kBtnSize = 46;   // button square (px)
static const int kBtnPad  = 6;    // margin from the screen edge
static const int kHitGrow = 10;   // enlarge the touch target beyond the visible button

static void toolBtnRect(int& x, int& y) {   // bottom-right
  x = tft.width()  - kBtnSize - kBtnPad;
  y = tft.height() - kBtnSize - kBtnPad;
}
static void backBtnRect(int& x, int& y) {    // bottom-left
  x = kBtnPad;
  y = tft.height() - kBtnSize - kBtnPad;
}
static bool btnHit(int px, int py, int bx, int by) {
  return px >= bx - kHitGrow && px < bx + kBtnSize + kHitGrow &&
         py >= by - kHitGrow && py < by + kBtnSize + kHitGrow;
}

// Rounded button chrome shared by both controls: dark fill, amber border.
static void drawButtonBox(int x, int y) {
  tft.fillRoundRect(x, y, kBtnSize, kBtnSize, 8, COL_BG);
  tft.drawRoundRect(x, y, kBtnSize, kBtnSize, 8, COL_AMBER);
}

// A gear glyph centred at (cx,cy): 8 teeth around a body with a hole.
static void drawGear(int cx, int cy, uint16_t col) {
  const int rOuter = 15, rBody = 12, rHole = 5, tooth = 5;
  for (int i = 0; i < 8; i++) {
    float a = i * (PI / 4.0f);
    int tx = cx + (int)(cos(a) * rOuter);
    int ty = cy + (int)(sin(a) * rOuter);
    tft.fillRect(tx - tooth / 2, ty - tooth / 2, tooth, tooth, col);
  }
  tft.fillCircle(cx, cy, rBody, col);
  tft.fillCircle(cx, cy, rHole, COL_BG);
}

// A left-pointing back arrow centred at (cx,cy).
static void drawBackArrow(int cx, int cy, uint16_t col) {
  const int s = 11;
  tft.fillTriangle(cx - s, cy, cx, cy - s, cx, cy + s, col);   // arrowhead
  tft.fillRect(cx, cy - s / 3, s + 2, (2 * s) / 3 + 1, col);   // shaft
}

void displayShowToolIcon() {
  int x, y; toolBtnRect(x, y);
  drawButtonBox(x, y);
  drawGear(x + kBtnSize / 2, y + kBtnSize / 2, COL_AMBER);
}

bool displayToolIconHit(int px, int py) {
  int x, y; toolBtnRect(x, y); return btnHit(px, py, x, y);
}

void displaySettingsScreen(const String& url) {
  const int W = tft.width();
  tft.fillScreen(COL_BG);

  // Centred QR (same style as the empty-board screen): scan to open the config
  // page on the home network.
  const int scale = 3;
  const int qDim  = (29 + 4) * scale;       // version-3 box side

  tft.setTextDatum(MC_DATUM);
  tft.setTextFont(4);
  tft.setTextColor(COL_AMBER, COL_BG);
  tft.drawString("Settings", W / 2, 24);

  drawQr(url, (W - qDim) / 2, 46, scale);

  tft.setTextFont(2);
  tft.setTextColor(TFT_WHITE, COL_BG);
  tft.drawString("Scan to configure, or open:", W / 2, 46 + qDim + 14);
  tft.setTextColor(COL_AMBER, COL_BG);
  String shown = url; shown.replace("http://", "");
  tft.drawString(shown, W / 2, 46 + qDim + 34);

  // Back arrow (bottom-left) — a hint that tapping returns to the board.
  int bx, by; backBtnRect(bx, by);
  drawButtonBox(bx, by);
  drawBackArrow(bx + kBtnSize / 2, by + kBtnSize / 2, COL_AMBER);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(2);  // restore default
}

void displayBoard(const std::vector<Departure>& deps, const String& configUrl) {
  const int W = tft.width();
  const int H = tft.height();
  const int nMax = std::max(1, maxRows());
  const int rowH = H / nMax;

  tft.fillScreen(COL_BG);

  if (deps.empty()) {
    // Amber-on-black to match the board; a centred QR jumps to /providers.
    const String url = configUrl.length() ? configUrl
                                          : "http://oeffi.local/providers";
    const int scale = 3;
    const int qDim = (29 + 4) * scale;  // version-3 box side

    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(COL_AMBER, COL_BG);
    tft.drawString("No departures", W / 2, 24);

    drawQr(url, (W - qDim) / 2, 46, scale);

    tft.setTextFont(2);
    tft.setTextColor(TFT_WHITE, COL_BG);
    tft.drawString("Scan to add a stop, or open:", W / 2, 46 + qDim + 14);
    tft.setTextColor(COL_AMBER, COL_BG);
    // Show the bare host/path (no scheme) so it stays on one line.
    String shown = url;
    shown.replace("http://", "");
    tft.drawString(shown, W / 2, 46 + qDim + 34);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);  // restore default
    return;
  }

  int rows = std::min((int)deps.size(), nMax);
  for (int i = 0; i < rows; i++) {
    kRenderers[(int)deps[i].style](deps[i], i * rowH, rowH);
    if (i > 0) tft.drawFastHLine(0, i * rowH, W, COL_DIM);
  }
  tft.setTextFont(2);  // restore for status screens
}
