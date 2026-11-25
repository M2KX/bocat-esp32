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
#include <Preferences.h>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "driver/rtc_io.h"
#include "theCat.h"

// ---------- settings you may want to change ----------
#define ROTATION      2        // 0 or 2: both are vertical, 2 is flipped 180 degrees
#define LEFT_BUTTON   0        // stock left button  (GPIO0)
#define RIGHT_BUTTON  35       // stock right button (GPIO35)
#define EXT_LEFT      32       // external left switch:  between header pin 32 and 3V
#define EXT_RIGHT     33       // external right switch: between header pin 33 and 3V
#define SWAP_PAWS     false    // set true if the wrong paw moves
#define BACK_COLOR    ST77XX_BLACK   // background of the message screens
#define SLEEP_AFTER_MS 30000   // no button touched for this long: deep sleep (any button wakes it)
#define CPU_MHZ       80       // slower CPU while awake = less current; plenty for this sketch
#define THEME_HOLD_MS 2000     // hold both buttons this long, then let go: next colour theme
#define HOLD_ONE_MS   5000     // hold one button this long to show its message
#define HOLD_BOTH_MS  10000    // hold both buttons this long to show the third message
#define MESSAGE_MS    6000     // how long a message stays on screen
// The stock buttons are active LOW (they connect their pin to GND when pressed).
// The external switches are active HIGH: wire each one between its pin and 3V.
// They use the chip internal pull-downs, so no resistors are needed, and being
// active HIGH is what lets either of them wake the board from deep sleep.
// An external switch does exactly what the stock button on the same side does.

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
#define OUT_W    SCREEN_W                      // 67 * 2 = 134, plus one repeated edge column
#define OUT_H    (VIEW_H * SCALE)              // 72
#define OUT_Y    ((SCREEN_H - OUT_H) / 2)      // centred top to bottom

Adafruit_ST7789 tft = Adafruit_ST7789(TFT_CS, TFT_DC, TFT_RST);
GFXcanvas16 canvas(OUT_W, OUT_H);     // off-screen buffer, pushed in one go (no flicker)

// ---------- hidden messages (one screen line per entry) ----------
const char *const MSG_RIGHT[] = { "I LOVE", "YOU", "BIH" };
const char *const MSG_LEFT[]  = { "i want", "to be", "with", "you", "forever" };
const char *const MSG_BOTH[]  = { "i wanna", "make you", "pregnant" };
#define COUNT(a) (sizeof(a) / sizeof(a[0]))

// ---------- colour themes ----------
#define RGB(r, g, b) ((uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3)))
struct Theme { uint16_t sky, table, fur, line; };
const Theme THEMES[] = {
  { RGB(0, 0, 0),       RGB(0, 0, 0),      RGB(0, 0, 0),       RGB(255, 255, 255) },  // 0 classic black and white
  { RGB(135, 206, 235), RGB(181, 101, 29), RGB(255, 255, 255), RGB(40, 26, 13)    },  // 1 white cat, blue wall, wooden table
  { RGB(255, 236, 179), RGB(121, 85, 72),  RGB(255, 152, 0),   RGB(62, 39, 35)    },  // 2 ginger cat
  { RGB(255, 205, 225), RGB(236, 64, 122), RGB(255, 255, 255), RGB(74, 20, 60)    },  // 3 pink
  { RGB(13, 27, 62),    RGB(38, 50, 56),   RGB(120, 144, 156), RGB(236, 239, 241) },  // 4 night, grey cat
};
#define THEME_COUNT (sizeof(THEMES) / sizeof(THEMES[0]))
#define DEFAULT_THEME 1
Preferences prefs;                  // remembers the chosen theme while the power is off
uint8_t theme = DEFAULT_THEME;

int lastState = -1;                 // button state seen on the previous pass
int drawnState = -1;                // cat frame currently on screen (-1 = none)
unsigned long stateSince = 0;       // when the current button state began
bool holdFired = false;             // this hold has already shown its message
bool showingMessage = false;
unsigned long messageSince = 0;
unsigned long lastActivity = 0;     // last button change (or end of a message)

// Screen and backlight off, then deep sleep. Either button wakes the board, which
// restarts the sketch from setup(). The colour theme is kept (it is stored in flash).
void goToSleep() {
  Serial.println("Idle - going to deep sleep");
  Serial.flush();
  tft.enableDisplay(false);
  tft.enableSleep(true);                       // ST7789 into its own sleep mode
  digitalWrite(TFT_BL, LOW);
  gpio_hold_en((gpio_num_t)TFT_BL);            // keep the backlight pin low while asleep
  gpio_deep_sleep_hold_en();

  // Wake-up sources. The classic ESP32 has one "single pin" source (ext0) and one
  // "any of these pins HIGH" source (ext1), so:
  //   ext0 = stock right button going LOW
  //   ext1 = either external switch going HIGH
  // The stock left button (GPIO0) cannot be added to that mix and does not wake it.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // keeps the pull-downs alive
  rtc_gpio_pullup_dis((gpio_num_t)EXT_LEFT);
  rtc_gpio_pulldown_en((gpio_num_t)EXT_LEFT);
  rtc_gpio_pullup_dis((gpio_num_t)EXT_RIGHT);
  rtc_gpio_pulldown_en((gpio_num_t)EXT_RIGHT);
  esp_sleep_enable_ext0_wakeup((gpio_num_t)RIGHT_BUTTON, 0);
  esp_sleep_enable_ext1_wakeup((1ULL << EXT_LEFT) | (1ULL << EXT_RIGHT), ESP_EXT1_WAKEUP_ANY_HIGH);
  esp_deep_sleep_start();
}

// Heart on top, then the lines centred, in the biggest text size that fits the width.
void showMessage(const char *const lines[], int count, uint16_t color) {
  int longest = 0;
  for (int i = 0; i < count; i++) {
    int n = strlen(lines[i]);
    if (n > longest) longest = n;
  }
  int size = 3;
  while (size > 1 && longest * 6 * size - size > SCREEN_W - 4) size--;

  const int lineH = 8 * size, gap = 3 * size, heartH = 40, heartGap = 14;
  int textH = count * lineH + (count - 1) * gap;
  int top = (SCREEN_H - (heartH + heartGap + textH)) / 2;

  tft.fillScreen(BACK_COLOR);
  int cx = SCREEN_W / 2, cy = top + 11;
  tft.fillCircle(cx - 11, cy, 11, ST77XX_RED);
  tft.fillCircle(cx + 11, cy, 11, ST77XX_RED);
  tft.fillTriangle(cx - 21, cy + 5, cx + 21, cy + 5, cx, cy + 28, ST77XX_RED);

  tft.setTextWrap(false);
  tft.setTextSize(size);
  tft.setTextColor(color);
  int y = top + heartH + heartGap;
  for (int i = 0; i < count; i++) {
    int w = strlen(lines[i]) * 6 * size - size;
    tft.setCursor((SCREEN_W - w) / 2, y);
    tft.print(lines[i]);
    y += lineH + gap;
  }
  showingMessage = true;
  messageSince = millis();
}

// The art is 1-bit line drawing. To colour it, every frame is split into areas:
// the wall behind the cat (reached from the top corners), the table (reached from
// the bottom corners), the outline itself, and whatever is left enclosed = the cat.
enum { AREA_FUR = 0, AREA_LINE, AREA_SKY, AREA_TABLE };
uint8_t area[VIEW_W * VIEW_H];
uint16_t fillQueue[VIEW_W * VIEW_H];

void floodArea(int x0, int y0, uint8_t mark) {
  if (area[y0 * VIEW_W + x0] != AREA_FUR) return;
  int head = 0, tail = 0;
  area[y0 * VIEW_W + x0] = mark;
  fillQueue[tail++] = y0 * VIEW_W + x0;
  while (head < tail) {
    int i = fillQueue[head++];
    int x = i % VIEW_W, y = i / VIEW_W;
    const int nx[4] = { x + 1, x - 1, x, x };
    const int ny[4] = { y, y, y + 1, y - 1 };
    for (int k = 0; k < 4; k++) {
      if (nx[k] < 0 || ny[k] < 0 || nx[k] >= VIEW_W || ny[k] >= VIEW_H) continue;
      int j = ny[k] * VIEW_W + nx[k];
      if (area[j] != AREA_FUR) continue;
      area[j] = mark;
      fillQueue[tail++] = j;
    }
  }
}

uint16_t areaColor(uint8_t a) {
  const Theme &t = THEMES[theme];
  switch (a) {
    case AREA_LINE:  return t.line;
    case AREA_SKY:   return t.sky;
    case AREA_TABLE: return t.table;
    default:         return t.fur;
  }
}

// Wall colour above the cat, table colour below it.
void drawBackground() {
  tft.fillRect(0, 0, SCREEN_W, OUT_Y, THEMES[theme].sky);
  tft.fillRect(0, OUT_Y + OUT_H, SCREEN_W, SCREEN_H - OUT_Y - OUT_H, THEMES[theme].table);
}

void drawCat(const unsigned char *frame) {
  const int bytesPerRow = BMP_W / 8;
  for (int y = 0; y < VIEW_H; y++) {
    int sy = y + CROP_Y;
    for (int x = 0; x < VIEW_W; x++) {
      int sx = x + CROP_X;
      uint8_t b = pgm_read_byte(frame + sy * bytesPerRow + (sx >> 3));
      area[y * VIEW_W + x] = (b & (0x80 >> (sx & 7))) ? AREA_LINE : AREA_FUR;
    }
  }
  floodArea(0, 0, AREA_SKY);
  floodArea(VIEW_W - 1, 0, AREA_SKY);
  floodArea(0, VIEW_H - 1, AREA_TABLE);
  floodArea(VIEW_W - 1, VIEW_H - 1, AREA_TABLE);

  for (int y = 0; y < VIEW_H; y++) {
    for (int x = 0; x < VIEW_W; x++) {
      canvas.fillRect(x * SCALE, y * SCALE, SCALE, SCALE, areaColor(area[y * VIEW_W + x]));
    }
    // the screen is one pixel wider than the scaled art: repeat the last column
    canvas.fillRect(VIEW_W * SCALE, y * SCALE, OUT_W - VIEW_W * SCALE, SCALE,
                    areaColor(area[y * VIEW_W + VIEW_W - 1]));
  }
  tft.drawRGBBitmap(0, OUT_Y, canvas.getBuffer(), OUT_W, OUT_H);
}

void setup() {
  // undo what goToSleep() set up, in case this start is a wake-up
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)TFT_BL);
  rtc_gpio_deinit((gpio_num_t)LEFT_BUTTON);
  rtc_gpio_deinit((gpio_num_t)RIGHT_BUTTON);
  rtc_gpio_deinit((gpio_num_t)EXT_LEFT);
  rtc_gpio_deinit((gpio_num_t)EXT_RIGHT);
  setCpuFrequencyMhz(CPU_MHZ);
  Serial.begin(115200);
  switch (esp_sleep_get_wakeup_cause()) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Bongo cat: woken by the stock right button");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println((esp_sleep_get_ext1_wakeup_status() & (1ULL << EXT_LEFT))
                     ? "Bongo cat: woken by the external left switch (pin 32)"
                     : "Bongo cat: woken by the external right switch (pin 33)");
      break;
    default:
      Serial.println("Bongo cat: power-on start");
      break;
  }

  pinMode(LEFT_BUTTON, INPUT_PULLUP);
  pinMode(RIGHT_BUTTON, INPUT_PULLUP);   // GPIO35 has no internal pull-up; the board has its own

  pinMode(EXT_LEFT, INPUT_PULLDOWN);
  pinMode(EXT_RIGHT, INPUT_PULLDOWN);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);             // backlight stays off until the screen is painted

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);
  tft.init(135, 240);                    // 1.14" panel
  tft.setSPISpeed(40000000);
  tft.setRotation(ROTATION);

  prefs.begin("bongocat", false);
  theme = prefs.getUChar("theme", DEFAULT_THEME);
  if (theme >= THEME_COUNT) theme = DEFAULT_THEME;
  drawBackground();
  digitalWrite(TFT_BL, HIGH);            // backlight on
}

void loop() {
  bool left  = digitalRead(LEFT_BUTTON)  == LOW || digitalRead(EXT_LEFT)  == HIGH;
  bool right = digitalRead(RIGHT_BUTTON) == LOW || digitalRead(EXT_RIGHT) == HIGH;
  if (SWAP_PAWS) { bool t = left; left = right; right = t; }

  int state = (left ? 1 : 0) | (right ? 2 : 0);
  unsigned long now = millis();
  if (state != lastState) {              // a button went down or up: restart the hold timer
    // both buttons were held a couple of seconds and then let go: next colour theme
    if (lastState == 3 && !holdFired && !showingMessage && now - stateSince >= THEME_HOLD_MS) {
      theme = (theme + 1) % THEME_COUNT;
      prefs.putUChar("theme", theme);
      drawBackground();
      drawnState = -1;
    }
    lastState = state;
    stateSince = now;
    lastActivity = now;
    holdFired = false;
  }

  if (showingMessage) {
    if (now - messageSince >= MESSAGE_MS) {   // message is over: back to the cat
      showingMessage = false;
      lastActivity = now;
      drawBackground();
      drawnState = -1;
    }
  } else if (!holdFired) {
    unsigned long held = now - stateSince;
    if (state == 2 && held >= HOLD_ONE_MS) {
      holdFired = true;
      showMessage(MSG_RIGHT, COUNT(MSG_RIGHT), ST77XX_WHITE);
    } else if (state == 1 && held >= HOLD_ONE_MS) {
      holdFired = true;
      showMessage(MSG_LEFT, COUNT(MSG_LEFT), ST77XX_WHITE);
    } else if (state == 3 && held >= HOLD_BOTH_MS) {
      holdFired = true;
      showMessage(MSG_BOTH, COUNT(MSG_BOTH), ST77XX_WHITE);
    }
  }

  if (!showingMessage && state != drawnState) {   // redraw only when something changed
    drawnState = state;
    switch (state) {
      case 0: drawCat(_bothpawsup);    break;
      case 1: drawCat(_leftpawdown);   break;
      case 2: drawCat(_rightpawdown);  break;
      case 3: drawCat(_bothpawsdown);  break;
    }
  }

  // nothing pressed and nothing happening for a while: sleep to save the battery
  if (!showingMessage && state == 0 && now - lastActivity >= SLEEP_AFTER_MS) {
    goToSleep();
  }
  delay(5);
}
