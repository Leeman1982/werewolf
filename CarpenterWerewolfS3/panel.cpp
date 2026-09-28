// ============================================================================
//  panel.cpp — ILI9341 TFT + XPT2046 touch on the ESP32-S3.
//
//  Hand-rolled on purpose rather than TFT_eSPI/LovyanGFX: the UI already
//  renders into its own canvas (gfx.h) and only ever needs "send this band
//  of RGB565", which is a dozen register writes. Doing it here means the
//  sketch needs no display library, and no library-global User_Setup.h that
//  would silently re-wire every other project on the same computer.
// ============================================================================
#include "panel.h"
#include "config.h"
#include <SPI.h>
#include <Preferences.h>

// ─── TFT ────────────────────────────────────────────────────────────────────
static SPIClass s_tftSpi(FSPI);
static const SPISettings TFT_SPI(TFT_SPI_HZ, MSBFIRST, SPI_MODE0);

static inline void tftCmd(uint8_t c) {
    digitalWrite(PIN_TFT_DC, LOW);
    s_tftSpi.write(c);
    digitalWrite(PIN_TFT_DC, HIGH);
}

static void tftCmdData(uint8_t c, const uint8_t* d, uint8_t n) {
    s_tftSpi.beginTransaction(TFT_SPI);
    digitalWrite(PIN_TFT_CS, LOW);
    tftCmd(c);
    for (uint8_t i = 0; i < n; i++) s_tftSpi.write(d[i]);
    digitalWrite(PIN_TFT_CS, HIGH);
    s_tftSpi.endTransaction();
}

// The standard ILI9341 power-up table (the same values every mainstream
// driver sends), then pixel format, orientation, sleep-out and display-on.
static const uint8_t INIT[] = {
    0xEF, 3, 0x03, 0x80, 0x02,
    0xCF, 3, 0x00, 0xC1, 0x30,
    0xED, 4, 0x64, 0x03, 0x12, 0x81,
    0xE8, 3, 0x85, 0x00, 0x78,
    0xCB, 5, 0x39, 0x2C, 0x00, 0x34, 0x02,
    0xF7, 1, 0x20,
    0xEA, 2, 0x00, 0x00,
    0xC0, 1, 0x23,             // power control 1
    0xC1, 1, 0x10,             // power control 2
    0xC5, 2, 0x3E, 0x28,       // VCOM 1
    0xC7, 1, 0x86,             // VCOM 2
    0x37, 1, 0x00,             // vertical scroll start
    0x3A, 1, 0x55,             // 16 bits/pixel
    0xB1, 2, 0x00, 0x18,       // frame rate 79 Hz
    0xB6, 3, 0x08, 0x82, 0x27, // display function
    0xF2, 1, 0x00,             // 3-gamma off
    0x26, 1, 0x01,             // gamma curve 1
    0xE0, 15, 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00,
    0xE1, 15, 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F,
    0x00
};

class IliSink : public gfx::Sink {
public:
    void pushRect(int x, int y, int w, int h, const uint16_t* px) override {
        uint8_t ca[4] = { (uint8_t)(x >> 8), (uint8_t)x, (uint8_t)((x + w - 1) >> 8), (uint8_t)(x + w - 1) };
        uint8_t pa[4] = { (uint8_t)(y >> 8), (uint8_t)y, (uint8_t)((y + h - 1) >> 8), (uint8_t)(y + h - 1) };
        s_tftSpi.beginTransaction(TFT_SPI);
        digitalWrite(PIN_TFT_CS, LOW);
        tftCmd(0x2A); s_tftSpi.writeBytes(ca, 4);
        tftCmd(0x2B); s_tftSpi.writeBytes(pa, 4);
        tftCmd(0x2C);
        // gfx::Canvas::flush() already hands over big-endian RGB565.
        s_tftSpi.writeBytes((const uint8_t*)px, (uint32_t)w * h * 2);
        digitalWrite(PIN_TFT_CS, HIGH);
        s_tftSpi.endTransaction();
    }
};
static IliSink s_sink;
gfx::Sink& panelSink() { return s_sink; }

void panelBegin() {
    pinMode(PIN_TFT_CS, OUTPUT);  digitalWrite(PIN_TFT_CS, HIGH);
    pinMode(PIN_TFT_DC, OUTPUT);  digitalWrite(PIN_TFT_DC, HIGH);
    s_tftSpi.begin(PIN_TFT_SCLK, PIN_TFT_MISO, PIN_TFT_MOSI, -1);

    if (PIN_TFT_RST >= 0) {
        pinMode(PIN_TFT_RST, OUTPUT);
        digitalWrite(PIN_TFT_RST, HIGH); delay(5);
        digitalWrite(PIN_TFT_RST, LOW);  delay(20);
        digitalWrite(PIN_TFT_RST, HIGH); delay(150);
    } else {
        tftCmdData(0x01, nullptr, 0);    // software reset
        delay(150);
    }
    for (const uint8_t* p = INIT; *p; ) {
        uint8_t cmd = *p++, n = *p++;
        tftCmdData(cmd, p, n);
        p += n;
    }
    uint8_t mad = TFT_MADCTL;
    tftCmdData(0x36, &mad, 1);
    if (TFT_INVERT) tftCmdData(0x21, nullptr, 0);
    tftCmdData(0x11, nullptr, 0);        // sleep out
    delay(120);
    tftCmdData(0x29, nullptr, 0);        // display on

    // Backlight on PWM, full until the saved level is applied.
    ledcAttach(PIN_TFT_BL, 20000, 8);
    ledcWrite(PIN_TFT_BL, 255);
}

void panelBacklight(uint8_t level) { ledcWrite(PIN_TFT_BL, level); }

// ─── Touch ──────────────────────────────────────────────────────────────────
static SPIClass s_touchSpi(HSPI);
static const SPISettings TOUCH_SPI(TOUCH_SPI_HZ, MSBFIRST, SPI_MODE0);
static bool s_touchUp = false;
static TouchCal s_cal = {
    // Rough placeholder only (raw ~200..3800 on each axis, X mirrored).
    // It is never relied on: with nothing saved in NVS the sketch runs the
    // three-point calibration at boot before anything else is touchable.
    -0.0865f, 0.0f, 336.0f,
    0.0f, 0.0667f, -15.0f
};

static void touchInit() {
    if (s_touchUp) return;
    pinMode(PIN_TOUCH_CS, OUTPUT);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    if (PIN_TOUCH_IRQ >= 0) pinMode(PIN_TOUCH_IRQ, INPUT);
    s_touchSpi.begin(PIN_TOUCH_CLK, PIN_TOUCH_MISO, PIN_TOUCH_MOSI, -1);
    s_touchUp = true;
}

static inline uint16_t xpt(uint8_t cmd) {
    s_touchSpi.transfer(cmd);
    uint16_t v = s_touchSpi.transfer16(0);
    return (uint16_t)(v >> 3);
}

// One measurement: pressure first, then 4 X/Y pairs with the outliers
// thrown away, then power down (0x80) so PENIRQ stays armed between polls.
bool touchRaw(uint16_t& rx, uint16_t& ry) {
    touchInit();
    s_touchSpi.beginTransaction(TOUCH_SPI);
    digitalWrite(PIN_TOUCH_CS, LOW);
    int z1 = xpt(0xB1), z2 = xpt(0xC1);
    int z = z1 + 4095 - z2;
    bool pressed = z >= TOUCH_Z_MIN;
    uint16_t xs[4], ys[4];
    if (pressed) {
        xpt(0x91);                                   // settle, discarded
        for (int i = 0; i < 4; i++) { ys[i] = xpt(0x91); xs[i] = xpt(0xD1); }
    }
    xpt(0x80);
    digitalWrite(PIN_TOUCH_CS, HIGH);
    s_touchSpi.endTransaction();
    if (!pressed) return false;

    // Mean of the middle two of four.
    auto mid2 = [](uint16_t* v) {
        for (int i = 0; i < 3; i++) for (int j = i + 1; j < 4; j++)
            if (v[j] < v[i]) { uint16_t t = v[i]; v[i] = v[j]; v[j] = t; }
        return (uint16_t)((v[1] + v[2]) / 2);
    };
    rx = mid2(xs); ry = mid2(ys);
    // A reading pinned at either rail is a glitch, not a touch.
    return rx > 50 && rx < 4050 && ry > 50 && ry < 4050;
}

bool touchRead(int& x, int& y) {
    uint16_t rx, ry;
    if (!touchRaw(rx, ry)) return false;
    float fx = s_cal.ax * rx + s_cal.bx * ry + s_cal.cx;
    float fy = s_cal.ay * rx + s_cal.by * ry + s_cal.cy;
    x = constrain((int)(fx + 0.5f), 0, gfx::W - 1);
    y = constrain((int)(fy + 0.5f), 0, gfx::H - 1);
    return true;
}

// Calibration lives in NVS, not on the FAT partition: a safe boot or a
// FORMAT FS must never leave the panel untouchable.
bool touchCalLoad() {
    Preferences p;
    if (!p.begin("wolfpanel", true)) return false;
    bool ok = p.getBytesLength("cal") == sizeof(TouchCal) &&
              p.getBytes("cal", &s_cal, sizeof(TouchCal)) == sizeof(TouchCal);
    p.end();
    return ok;
}

void touchCalSet(const TouchCal& c, bool save) {
    s_cal = c;
    if (!save) return;
    Preferences p;
    if (!p.begin("wolfpanel", false)) return;
    p.putBytes("cal", &s_cal, sizeof(TouchCal));
    p.end();
}

uint32_t panelFreeHeap() { return ESP.getFreeHeap(); }
uint32_t panelCpuMhz()   { return getCpuFrequencyMhz(); }
