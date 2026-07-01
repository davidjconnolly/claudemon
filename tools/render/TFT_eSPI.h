// Host mock of TFT_eSPI: draws into a 320x240 RGB565 framebuffer.
// Text rendering mirrors TFT_eSPI's GLCD (font 1) and Font16 (font 2) paths,
// using the library's actual font data so metrics/positions are faithful.
#pragma once
#include <stdint.h>
#include <string.h>
#include "Arduino.h"
#include "pgmspace.h"

// Real TFT_eSPI colour constants (RGB565).
#define TFT_BLACK    0x0000
#define TFT_DARKGREY 0x7BEF
#define TFT_BLUE     0x001F
#define TFT_GREEN    0x07E0
#define TFT_CYAN     0x07FF
#define TFT_RED      0xF800
#define TFT_WHITE    0xFFFF
#define TFT_ORANGE   0xFDA0

// Datum constants (match TFT_eSPI numbering).
#define TL_DATUM 0
#define TC_DATUM 1
#define TR_DATUM 2
#define ML_DATUM 3
#define MC_DATUM 4
#define MR_DATUM 5
#define BL_DATUM 6
#define BC_DATUM 7
#define BR_DATUM 8

// Pull in the library's real font data (PROGMEM is a no-op here).
#include "fonts/glcdfont.c"  // static const unsigned char font[] (GLCD / font 1)
#include "fonts/Font16.c"    // widtbl_f16[96], chrtbl_f16[96]   (font 2)

class TFT_eSPI {
public:
  static const int W = 320, H = 240;
  uint16_t fb[W * H];
  uint8_t  datum = TL_DATUM, tsize = 1;
  uint16_t fg = TFT_WHITE, bg = TFT_BLACK;
  bool     fillbg = false;

  TFT_eSPI() { fillScreen(TFT_BLACK); }

  void init() {}
  void setRotation(uint8_t) {}
  void invertDisplay(bool) {}   // hardware inverts; logical colours unchanged here
  int  width()  { return W; }
  int  height() { return H; }

  void setTextDatum(uint8_t d) { datum = d; }
  void setTextSize(uint8_t s)  { tsize = s ? s : 1; }
  void setTextColor(uint16_t c) { fg = bg = c; fillbg = false; }
  void setTextColor(uint16_t c, uint16_t b) { fg = c; bg = b; fillbg = (c != b); }
  void setTextFont(uint8_t) {}

  inline void px(int x, int y, uint16_t c) {
    if (x >= 0 && x < W && y >= 0 && y < H) fb[y * W + x] = c;
  }
  void drawPixel(int x, int y, uint16_t c) { px(x, y, c); }
  void fillRect(int x, int y, int w, int h, uint16_t c) {
    for (int j = 0; j < h; j++)
      for (int i = 0; i < w; i++) px(x + i, y + j, c);
  }
  void drawRect(int x, int y, int w, int h, uint16_t c) {
    for (int i = 0; i < w; i++) { px(x + i, y, c); px(x + i, y + h - 1, c); }
    for (int j = 0; j < h; j++) { px(x, y + j, c); px(x + w - 1, y + j, c); }
  }
  void drawFastHLine(int x, int y, int w, uint16_t c) { for (int i = 0; i < w; i++) px(x + i, y, c); }
  void drawFastVLine(int x, int y, int h, uint16_t c) { for (int j = 0; j < h; j++) px(x, y + j, c); }
  void fillScreen(uint16_t c) { for (int i = 0; i < W * H; i++) fb[i] = c; }

  int textWidth(const char* s, uint8_t font) {
    int w = 0;
    if (font == 2) {
      for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        w += (c > 31 && c < 128) ? widtbl_f16[c - 32] : widtbl_f16[0];
      }
    } else {
      for (; *s; ++s) w += 6;
    }
    return w * tsize;
  }
  int fontHeight(uint8_t font) { return (font == 2 ? 16 : 8) * tsize; }

  // GLCD (font 1): 5 columns from font[c*5+i], +1 blank column; bit j = row j.
  int drawCharGLCD(unsigned char c, int x, int y) {
    if (fillbg) fillRect(x, y, 6 * tsize, 8 * tsize, bg);
    for (int i = 0; i < 5; i++) {
      uint8_t line = font[c * 5 + i];
      for (int j = 0; j < 8; j++)
        if (line & (1 << j)) fillRect(x + i * tsize, y + j * tsize, tsize, tsize, fg);
    }
    return 6 * tsize;
  }

  // Font16 (font 2): width from widtbl_f16, height 16, bytes/row=(w+6)/8, MSB-first.
  int drawCharF2(unsigned char c, int x, int y) {
    int idx = (c > 31 && c < 128) ? c - 32 : 0;
    int width = widtbl_f16[idx];
    const unsigned char* bm = chrtbl_f16[idx];
    int wb = (width + 6) / 8;
    if (fillbg) fillRect(x, y, width * tsize, 16 * tsize, bg);
    for (int i = 0; i < 16; i++) {
      for (int k = 0; k < wb; k++) {
        uint8_t line = bm[wb * i + k];
        for (int b = 0; b < 8; b++)
          if (line & (0x80 >> b))
            fillRect(x + (k * 8 + b) * tsize, y + i * tsize, tsize, tsize, fg);
      }
    }
    return width * tsize;
  }

  int16_t drawString(const char* s, int32_t x, int32_t y, uint8_t font = 1) {
    int cw = textWidth(s, font), ch = fontHeight(font);
    int poX = x, poY = y;
    if (datum == TC_DATUM || datum == MC_DATUM || datum == BC_DATUM) poX -= cw / 2;
    if (datum == TR_DATUM || datum == MR_DATUM || datum == BR_DATUM) poX -= cw;
    if (datum == ML_DATUM || datum == MC_DATUM || datum == MR_DATUM) poY -= ch / 2;
    if (datum == BL_DATUM || datum == BC_DATUM || datum == BR_DATUM) poY -= ch;
    int cx = poX;
    for (; *s; ++s) {
      unsigned char c = (unsigned char)*s;
      cx += (font == 2) ? drawCharF2(c, cx, poY) : drawCharGLCD(c, cx, poY);
    }
    return cw;
  }
  int16_t drawString(const String& s, int32_t x, int32_t y, uint8_t font = 1) {
    return drawString(s.c_str(), x, y, font);
  }
};
