// Bongo cat for the LILYGO TTGO T-Display (ESP32, 1.14" ST7789, 135x240)
// Ported from the SSD1306 OLED version. Screen runs vertically (135x240).
//
// Libraries (Arduino Library Manager):
//   - Adafruit GFX Library
//   - Adafruit ST7735 and ST7789 Library
// Board: "ESP32 Dev Module"
//
// theCat.h must sit in the same folder as this .ino file.

#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include "theCat.h"

// ---------- settings you may want to change ----------
#define ROTATION      2        // 0 or 2: both are vertical, 2 is flipped 180 degrees
#define LEFT_BUTTON   0        // stock left button  (GPIO0)
#define RIGHT_BUTTON  35       // stock right button (GPIO35)
#define SWAP_PAWS     false    // set true if the wrong paw moves
#define CAT_COLOR     ST77XX_WHITE
#define BACK_COLOR    ST77XX_BLACK
// Buttons are active LOW: each one connects its pin to GND when pressed.
// Keyboard switches soldered across the stock buttons need no code change.
// If you wire them to other pins instead, use pins that have an internal
// pull-up (for example 25, 26, 27, 32, 33), not 34-39.

// ---------- T-Display wiring (fixed on the board) ----------
#define TFT_MOSI  19
#define TFT_SCLK  18
#define TFT_CS    5
#define TFT_DC    16
#define TFT_RST   23
#define TFT_BL    4

// ---------- cat bitmap layout ----------
#define BMP_W    128           // source frames are 128x40, 1 bit per pixel
#define BMP_H    40
#define SCALE    2             // each source pixel becomes 2x2 on the TFT
#define CROP_X   26            // show source columns 26..92: the cat and both paws.
#define VIEW_W   67            //   The ends of the table line run off the screen edges.
#define CROP_Y   4             // the cat occupies rows 4..39
#define VIEW_H   36
#define SCREEN_W 135           // portrait screen
#define SCREEN_H 240
#define OUT_W    (VIEW_W * SCALE)              // 134
#define OUT_H    (VIEW_H * SCALE)              // 72
#define OUT_X    ((SCREEN_W - OUT_W) / 2)
#define OUT_Y    ((SCREEN_H - OUT_H) / 2)      // centred top to bottom

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(OUT_W, OUT_H);     // off-screen buffer, pushed in one go (no flicker)

int lastState = -1;

void drawCat(const unsigned char *frame) {
  canvas.fillScreen(BACK_COLOR);
  const int bytesPerRow = BMP_W / 8;
  for (int y = 0; y < VIEW_H; y++) {
    int sy = y + CROP_Y;
    for (int x = 0; x < VIEW_W; x++) {
      int sx = x + CROP_X;
      uint8_t b = pgm_read_byte(frame + sy * bytesPerRow + (sx >> 3));
      if (b & (0x80 >> (sx & 7))) {
        canvas.fillRect(x * SCALE, y * SCALE, SCALE, SCALE, CAT_COLOR);
      }
    }
  }
  tft.drawRGBBitmap(OUT_X, OUT_Y, canvas.getBuffer(), OUT_W, OUT_H);
}

void setup() {
  pinMode(LEFT_BUTTON, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON, INPUT_PULLUP);   // GPIO35 has no internal pull-up; the board has its own

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);            // backlight on

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(135, 240);                    // 1.14" panel
  tft.setSPISpeed(40000000);
  tft.setRotation(ROTATION);
  tft.fillScreen(BACK_COLOR);
}

void loop() {
  bool left  = digitalRead(LEFT_BUTTON)  == LOW;
  bool right = digitalRead(RIGHT_BUTTON) == LOW;
  if (SWAP_PAWS) { bool t = left; left = right; right = t; }

  int state = (left ? 1 : 0) | (right ? 2 : 0);
  if (state != lastState) {              // redraw only when something changed
    lastState = state;
    switch (state) {
      case 0: drawCat(_bothpawsup);    break;
      case 1: drawCat(_leftpawdown);   break;
      case 2: drawCat(_rightpawdown);  break;
      case 3: drawCat(_bothpawsdown);  break;
    }
  }
  delay(5);
}
