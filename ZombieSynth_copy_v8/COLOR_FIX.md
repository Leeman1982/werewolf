# ZOMBIE SS Color Troubleshooting

## Current Color Scheme (RGB Order)

```cpp
#define THEME_BG         0x0000  // Pure Black
#define THEME_PRIMARY    0xF800  // Pure Red (R=31, G=0, B=0)
#define THEME_ACCENT     0xFFFF  // Pure White
#define THEME_OUTLINE    0xFFFF  // White outlines
#define THEME_TEXT       0xF800  // Red text
#define THEME_TEXT_DIM   0x8800  // Dim Red (R=17, G=0, B=0)
```

## What You Should See

- **Background:** Pure black
- **Main text & buttons:** Bright red
- **Outlines & accents:** White
- **Status text:** Dim red

## If Colors Look Wrong

### Symptom 1: Red appears as Blue
**Problem:** Display has BGR color order instead of RGB

**Solution:** Use these colors instead:
```cpp
#define THEME_BG         0x0000  // Pure Black
#define THEME_PRIMARY    0x001F  // Pure Blue (swapped from red)
#define THEME_ACCENT     0xFFFF  // Pure White
#define THEME_OUTLINE    0xFFFF  // White outlines
#define THEME_TEXT       0x001F  // Blue text
#define THEME_TEXT_DIM   0x0011  // Dim Blue
```

### Symptom 2: Colors are inverted (white on red)
**Problem:** Display inversion is on

**Solution:** Add to `User_Setup.h`:
```cpp
#define TFT_INVERSION_OFF
```

### Symptom 3: Want brighter/different red
**Alternative red shades (RGB565):**
```cpp
0xF800  // Pure red (current)
0xF000  // Slightly dimmer red
0xFA00  // Red with slight green tint
0xF810  // Red-orange
0xC000  // Dark red
```

## Quick Color Test

Add this to your main menu to test colors:

```cpp
void testColors() {
  tft.fillScreen(0x0000);  // Black background

  // Test red
  tft.fillRect(10, 10, 50, 50, 0xF800);
  tft.drawRect(10, 10, 50, 50, 0xFFFF);

  // Test white
  tft.fillRect(70, 10, 50, 50, 0xFFFF);

  // Text
  tft.setTextColor(0xF800, 0x0000);
  tft.drawString("RED", 15, 70, 2);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.drawString("WHITE", 75, 70, 2);
}
```

## Screen Flashing Fix

The flashing was caused by forced redraws in `zombieSynthUpdate()`. This has been fixed.

**If still flashing:**
1. Make sure you copied the latest `zombie_synth_mode.h`
2. Check that `needsRedraw` flags are working
3. Verify display SPI speed (55MHz in User_Setup.h)

## To Apply Color Fix

1. Open `common_definitions.h`
2. Replace the color values with the correct set for your display
3. Upload to CYD
4. Colors should now be correct!
