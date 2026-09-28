#pragma once
// ============================================================================
//  gfx.h — the 320x240 colour canvas the whole panel UI draws into.
//
//  A 16-colour (4 bits/pixel) frame buffer: 320*240/2 = 38,400 bytes. Every
//  colour the NightShift-style panel uses is
//  one of the sixteen palette slots below; the palette is expanded to RGB565
//  only as each band is sent to the glass.
//
//  Text and line primitives are not reimplemented here. U8g2's own C core is
//  set up as a 320x240 "display" whose low-level line callback writes into
//  this canvas in the current palette colour, so every u8g2_font_* bitmap font
//  and every u8g2 primitive (glyphs, XBM, discs, lines) is available at full
//  colour resolution with no second font engine. The same code runs on the
//  host, which is what tools/preview/ uses to render the screens to PNG.
//
//  flush() compares a hash of each 8-row band against the last frame's and
//  only sends bands that actually changed, so a moving playhead costs two or
//  three bands of SPI traffic, not a whole frame.
// ============================================================================
#include <stdint.h>
#include <stddef.h>

// On the board, <U8g2lib.h> is included (rather than clib/u8g2.h directly)
// because the Arduino builder finds libraries by their top-level header name.
// The host preview links the U8g2 C core on its own.
#ifdef ARDUINO
#include <U8g2lib.h>
#else
extern "C" {
#include "u8g2.h"
}
#endif

namespace gfx {

constexpr int W = 320;
constexpr int H = 240;
constexpr int BAND_H = 8;
constexpr int NUM_BANDS = H / BAND_H;

// Palette slots. Names say what a colour MEANS on this panel, following the
// NightShift manual's colour rules: LIME is live/primary (titles, playhead,
// running data), AMBER is the cursor (what the encoder will change; solid
// amber = editing it now), RED is recording, muted or destructive.
enum Col : uint8_t {
    BG = 0,      // glass
    PANEL,       // cell / card fill
    HILITE,      // selected-row band, beat cells
    GRID,        // frames, hairlines
    DIM,         // ghost text, disabled numbers
    MUTED,       // secondary text
    TEXT,        // labels
    LIME,        // live / primary: titles, playhead, running data
    LIME_D,      // lime's darker partner (bar bodies, lamps off)
    BAR,         // velocity bars, segment meters
    BAR_D,       // dotted leaders, unlit segments
    AMBER,       // cursor / editing
    AMBER_D,
    RED,         // record, mute, destructive
    RED_D,
    FOOT,        // footer and soft-key band
    NUM_COLS,
    INK = BG     // dark text knocked out of a lit bar
};

// What flush() sends pixels to. The ILI9341 driver implements it on the
// device; the host preview implements it with an RGB image.
class Sink {
public:
    virtual ~Sink() {}
    // One band: w*h pixels, RGB565, already in the byte order the panel wants.
    virtual void pushRect(int x, int y, int w, int h, const uint16_t* px) = 0;
};

// Fonts, picked once here so every screen uses the same four sizes.
extern const uint8_t* const FONT_SMALL;   // cell numbers, box captions
extern const uint8_t* const FONT_BODY;    // list rows, status lines
extern const uint8_t* const FONT_BOLD;    // title box, top bar
extern const uint8_t* const FONT_BIG;     // value boxes, chain frames

class Canvas {
public:
    void begin();

    // Palette. RGB888 in, stored as RGB565.
    void setPalette(uint8_t idx, uint32_t rgb);
    void setCrt(bool on)            { _crt = on; invalidate(); }
    bool crt() const                { return _crt; }
    void invalidate();              // force every band out on the next flush

    // ── Primitives (all clip to the canvas) ────────────────────────────────
    void clear(uint8_t c);
    void pixel(int x, int y, uint8_t c);
    uint8_t get(int x, int y) const;
    void hline(int x, int y, int w, uint8_t c);
    void vline(int x, int y, int h, uint8_t c);
    void fill(int x, int y, int w, int h, uint8_t c);
    void frame(int x, int y, int w, int h, uint8_t c);
    // 50% checkerboard: "this may not happen" (probability < 100%).
    void checker(int x, int y, int w, int h, uint8_t c);
    // Dotted rules: `on` pixels lit out of every `period`.
    void dotH(int x, int y, int w, uint8_t c, int on = 1, int period = 2);
    void dotV(int x, int y, int h, uint8_t c, int on = 1, int period = 2);
    void dotFrame(int x, int y, int w, int h, uint8_t c);
    // Segment bar: n segments of 2px + 1px gap, `lit` of them in `on`.
    void segBar(int x, int y, int w, int h, float v, uint8_t on, uint8_t off);
    void line(int x0, int y0, int x1, int y1, uint8_t c);
    void disc(int x, int y, int r, uint8_t c);
    void circle(int x, int y, int r, uint8_t c);
    void xbm(int x, int y, int w, int h, const uint8_t* bits, uint8_t c);

    // ── Text (y is the baseline) ────────────────────────────────────────────
    void setFont(const uint8_t* f);
    int  text(int x, int y, const char* s, uint8_t c);        // returns end x
    int  textRight(int xr, int y, const char* s, uint8_t c);  // right-aligned at xr
    int  textCenter(int cx, int y, const char* s, uint8_t c);
    int  textW(const char* s);
    int  ascent();
    // Draws as much of `s` as fits in `maxW`, ending with '.' when clipped.
    int  textFit(int x, int y, const char* s, int maxW, uint8_t c);

    void flush(Sink& sink);

    // Raw access for the host preview.
    const uint8_t* buffer() const  { return _buf; }
    uint16_t rgb565(uint8_t idx, bool oddRow) const;

    u8g2_t* u8g2() { return &_u8; }

private:
    // 38,400 bytes. A static array on the host; on the S3 it is allocated in
    // begin() (PSRAM when fitted, see bigmem.h) -- the band flush is the only
    // bulk reader and it is nowhere near PSRAM's bandwidth.
#if defined(ESP_PLATFORM)
    uint8_t* _buf = nullptr;
#else
    uint8_t  _buf[W * H / 2];
#endif
    uint16_t _pal[NUM_COLS];        // RGB565, host order
    uint16_t _palDim[NUM_COLS];     // the same, darkened for CRT odd rows
    uint32_t _bandHash[NUM_BANDS];
    bool     _crt = true;
    u8g2_t   _u8;
};

extern Canvas canvas;

} // namespace gfx
