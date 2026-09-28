#include "gfx.h"
#include "bigmem.h"
#include <string.h>

namespace gfx {

Canvas canvas;

// Four sizes, all stock U8g2 bitmap fonts (so no font data lives in this
// sketch). Chosen by rendering every screen through tools/preview/.
const uint8_t* const FONT_SMALL = u8g2_font_5x8_tr;
const uint8_t* const FONT_BODY  = u8g2_font_8x13_tr;
const uint8_t* const FONT_BOLD  = u8g2_font_8x13B_tr;
const uint8_t* const FONT_BIG   = u8g2_font_9x15B_tr;

// ── U8g2 bridge ─────────────────────────────────────────────────────────────
// U8g2 renders everything through one low-level "draw a horizontal or
// vertical run" callback. Pointing that at the canvas, in whatever palette
// colour is current, is the entire bridge.
static uint8_t s_col = TEXT;

static void llHvline(u8g2_t* u8g2, u8g2_uint_t x, u8g2_uint_t y, u8g2_uint_t len, uint8_t dir) {
    (void)u8g2;
    if (dir == 0) canvas.hline((int)x, (int)y, (int)len, s_col);
    else          canvas.vline((int)x, (int)y, (int)len, s_col);
}

// A 320x240 "display" with no bus behind it: only the geometry matters.
static u8x8_display_info_t s_info;
static uint8_t virtualDisplayCb(u8x8_t* u8x8, uint8_t msg, uint8_t argInt, void* argPtr) {
    (void)argInt; (void)argPtr;
    if (msg == U8X8_MSG_DISPLAY_SETUP_MEMORY) u8x8_d_helper_display_setup_memory(u8x8, &s_info);
    return 1;
}
// u8g2_SetupBuffer() wants a tile buffer; the custom callback never touches
// it, and nothing here calls ClearBuffer/SendBuffer, so one tile row is only
// there to keep the pointer valid.
static uint8_t s_dummyTiles[W];

void Canvas::begin() {
#if defined(ESP_PLATFORM)
    if (!_buf) _buf = (uint8_t*)bigAlloc((size_t)W * H / 2);
    // This is the first allocation of the boot, when even internal RAM has
    // far more than 38 KB free, so it does not fail in practice. If it ever
    // did there would be no screen to say so on: stop with a panic (the
    // reason is printed on the USB serial port) rather than draw through a
    // null pointer.
    if (!_buf) abort();
#endif
    memset(&s_info, 0, sizeof(s_info));
    s_info.tile_width   = W / 8;
    s_info.tile_height  = H / 8;
    s_info.pixel_width  = W;
    s_info.pixel_height = H;
    u8g2_SetupDisplay(&_u8, virtualDisplayCb, u8x8_cad_empty, u8x8_byte_empty, u8x8_dummy_cb);
    u8g2_SetupBuffer(&_u8, s_dummyTiles, H / 8, llHvline, U8G2_R0);
    u8g2_SetFontMode(&_u8, 1);           // transparent: glyph background untouched
    u8g2_SetDrawColor(&_u8, 1);
    u8g2_SetFontPosBaseline(&_u8);
    u8g2_SetFont(&_u8, FONT_BODY);
    clear(BG);
    invalidate();
}

void Canvas::setPalette(uint8_t idx, uint32_t rgb) {
    if (idx >= NUM_COLS) return;
    uint8_t r = (rgb >> 16) & 0xFF, g = (rgb >> 8) & 0xFF, b = rgb & 0xFF;
    _pal[idx] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    // CRT odd rows at 76% -- the exact ratio between each lit colour and its
    // scanline partner on the NightShift reference screens (#ACF26A over
    // #83B652, #CDE2B4 over #9CAA8B).
    r = (uint8_t)(r * 76 / 100); g = (uint8_t)(g * 76 / 100); b = (uint8_t)(b * 76 / 100);
    _palDim[idx] = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
    invalidate();
}

uint16_t Canvas::rgb565(uint8_t idx, bool oddRow) const {
    idx &= 0x0F;
    return (_crt && oddRow) ? _palDim[idx] : _pal[idx];
}

void Canvas::invalidate() {
    for (int i = 0; i < NUM_BANDS; i++) _bandHash[i] = 0xFFFFFFFFu;
}

// ── Pixels ──────────────────────────────────────────────────────────────────
static inline void putPx(uint8_t* buf, int x, int y, uint8_t c) {
    uint8_t& b = buf[(y * W + x) >> 1];
    if (x & 1) b = (uint8_t)((b & 0x0F) | (c << 4));
    else       b = (uint8_t)((b & 0xF0) | (c & 0x0F));
}

void Canvas::clear(uint8_t c) {
    c &= 0x0F;
    memset(_buf, (c << 4) | c, (size_t)W * H / 2);
}

void Canvas::pixel(int x, int y, uint8_t c) {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return;
    putPx(_buf, x, y, c);
}

uint8_t Canvas::get(int x, int y) const {
    if ((unsigned)x >= (unsigned)W || (unsigned)y >= (unsigned)H) return BG;
    uint8_t b = _buf[(y * W + x) >> 1];
    return (x & 1) ? (b >> 4) : (b & 0x0F);
}

void Canvas::hline(int x, int y, int w, uint8_t c) {
    if ((unsigned)y >= (unsigned)H) return;
    if (x < 0) { w += x; x = 0; }
    if (x + w > W) w = W - x;
    if (w <= 0) return;
    c &= 0x0F;
    // Odd leading pixel, then whole bytes, then an odd trailing pixel.
    if (x & 1) { putPx(_buf, x, y, c); x++; w--; }
    uint8_t* p = &_buf[(y * W + x) >> 1];
    int n = w >> 1;
    if (n > 0) memset(p, (c << 4) | c, (size_t)n);
    if (w & 1) putPx(_buf, x + w - 1, y, c);
}

void Canvas::vline(int x, int y, int h, uint8_t c) {
    if ((unsigned)x >= (unsigned)W) return;
    if (y < 0) { h += y; y = 0; }
    if (y + h > H) h = H - y;
    for (int j = 0; j < h; j++) putPx(_buf, x, y + j, c);
}

void Canvas::fill(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++) hline(x, y + j, w, c);
}

void Canvas::frame(int x, int y, int w, int h, uint8_t c) {
    if (w <= 0 || h <= 0) return;
    hline(x, y, w, c);
    hline(x, y + h - 1, w, c);
    vline(x, y, h, c);
    vline(x + w - 1, y, h, c);
}

void Canvas::checker(int x, int y, int w, int h, uint8_t c) {
    for (int j = 0; j < h; j++)
        for (int i = ((x + y + j) & 1); i < w; i += 2) pixel(x + i, y + j, c);
}

void Canvas::dotH(int x, int y, int w, uint8_t c, int on, int period) {
    if (period < 1) period = 1;
    for (int i = 0; i < w; i++) if ((i % period) < on) pixel(x + i, y, c);
}

void Canvas::dotV(int x, int y, int h, uint8_t c, int on, int period) {
    if (period < 1) period = 1;
    for (int j = 0; j < h; j++) if ((j % period) < on) pixel(x, y + j, c);
}

void Canvas::dotFrame(int x, int y, int w, int h, uint8_t c) {
    dotH(x, y, w, c); dotH(x, y + h - 1, w, c);
    dotV(x, y, h, c); dotV(x + w - 1, y, h, c);
}

void Canvas::segBar(int x, int y, int w, int h, float v, uint8_t on, uint8_t off) {
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    int n = (w + 1) / 3;
    if (n <= 0) return;
    int lit = (int)(v * (float)n + 0.5f);
    for (int i = 0; i < n; i++) fill(x + i * 3, y, 2, h, i < lit ? on : off);
}

void Canvas::line(int x0, int y0, int x1, int y1, uint8_t c) {
    // Bresenham directly: u8g2's DrawLine takes unsigned coordinates, and the
    // graphs here legitimately start off-canvas and clip.
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        int e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void Canvas::disc(int x, int y, int r, uint8_t c) {
    if (x < 0 || y < 0) return;
    s_col = c; u8g2_DrawDisc(&_u8, (u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)r, U8G2_DRAW_ALL);
}

void Canvas::circle(int x, int y, int r, uint8_t c) {
    if (x < 0 || y < 0) return;
    s_col = c; u8g2_DrawCircle(&_u8, (u8g2_uint_t)x, (u8g2_uint_t)y, (u8g2_uint_t)r, U8G2_DRAW_ALL);
}

void Canvas::xbm(int x, int y, int w, int h, const uint8_t* bits, uint8_t c) {
    // Pixel-by-pixel rather than u8g2_DrawXBM so a logo can sit partly
    // off-canvas (u8g2 coordinates are unsigned).
    int stride = (w + 7) >> 3;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++)
            if (bits[j * stride + (i >> 3)] & (1 << (i & 7))) pixel(x + i, y + j, c);
}

// ── Text ────────────────────────────────────────────────────────────────────
void Canvas::setFont(const uint8_t* f) { u8g2_SetFont(&_u8, f); }
int  Canvas::textW(const char* s)      { return (int)u8g2_GetStrWidth(&_u8, s); }
int  Canvas::ascent()                  { return (int)u8g2_GetAscent(&_u8); }

int Canvas::text(int x, int y, const char* s, uint8_t c) {
    if (!s || !*s) return x;
    if (x < 0 || y < 0) return x;       // u8g2 coordinates are unsigned
    s_col = c;
    return x + (int)u8g2_DrawStr(&_u8, (u8g2_uint_t)x, (u8g2_uint_t)y, s);
}

int Canvas::textRight(int xr, int y, const char* s, uint8_t c) {
    return text(xr - textW(s), y, s, c);
}

int Canvas::textCenter(int cx, int y, const char* s, uint8_t c) {
    return text(cx - textW(s) / 2, y, s, c);
}

int Canvas::textFit(int x, int y, const char* s, int maxW, uint8_t c) {
    if (textW(s) <= maxW) return text(x, y, s, c);
    char buf[48];
    size_t n = strlen(s);
    if (n >= sizeof(buf)) n = sizeof(buf) - 1;
    memcpy(buf, s, n);
    buf[n] = 0;
    while (n > 1) {
        buf[--n] = 0;
        buf[n - 1] = '.';
        if (textW(buf) <= maxW) break;
    }
    return text(x, y, buf, c);
}

// ── Flush ───────────────────────────────────────────────────────────────────
static uint32_t fnv1a(const uint8_t* p, size_t n) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

void Canvas::flush(Sink& sink) {
    static uint16_t px[W * BAND_H];
    const size_t bandBytes = (size_t)W * BAND_H / 2;
    for (int b = 0; b < NUM_BANDS; b++) {
        const uint8_t* src = &_buf[b * bandBytes];
        uint32_t h = fnv1a(src, bandBytes);
        if (h == _bandHash[b]) continue;
        _bandHash[b] = h;
        // Expand 4bpp -> RGB565, big-endian (the order an ILI9341 wants on
        // the wire, so the SPI driver can send the buffer untouched).
        for (int row = 0; row < BAND_H; row++) {
            bool odd = ((b * BAND_H + row) & 1) != 0;
            const uint16_t* pal = (_crt && odd) ? _palDim : _pal;
            const uint8_t* s = src + row * (W / 2);
            uint16_t* d = &px[row * W];
            for (int i = 0; i < W / 2; i++) {
                uint16_t lo = pal[s[i] & 0x0F], hi = pal[s[i] >> 4];
                d[2 * i]     = (uint16_t)((lo >> 8) | (lo << 8));
                d[2 * i + 1] = (uint16_t)((hi >> 8) | (hi << 8));
            }
        }
        sink.pushRect(0, b * BAND_H, W, BAND_H, px);
    }
}

} // namespace gfx
