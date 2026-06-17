// ----------------------------------------------------------------------------
//  Presentation layer implementation: palette, text helpers and the per-style
//  row renderers. To add a new row look, add a renderer and an entry to
//  kRenderers[] (see RowStyle in departures.h).
// ----------------------------------------------------------------------------
#include "display.h"
#include <TFT_eSPI.h>
#include <algorithm>
#include "config.h"

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

void displayBoard(const std::vector<Departure>& deps) {
  const int W = tft.width();
  const int H = tft.height();
  const int rowH = H / MAX_ROWS;

  tft.fillScreen(COL_BG);

  if (deps.empty()) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(COL_DIM, COL_BG);
    tft.drawString("Keine Abfahrten", W / 2, H / 2);
    return;
  }

  int rows = std::min((int)deps.size(), (int)MAX_ROWS);
  for (int i = 0; i < rows; i++) {
    kRenderers[(int)deps[i].style](deps[i], i * rowH, rowH);
    if (i > 0) tft.drawFastHLine(0, i * rowH, W, COL_DIM);
  }
  tft.setTextFont(2);  // restore for status screens
}
