#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "theCat.h"

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);
#define LEFTBUTTON 13
#define RIGHTBUTTON 14
int left;
int right;

void setup() 
{
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);  // here the 0x3c is the I2C address, check your i2c address if u have multiple devices.
  display.clearDisplay();
  delay(2000);
  pinMode(LEFTBUTTON,OUTPUT);
  pinMode(RIGHTBUTTON,OUTPUT);
  
}

void loop() 
{
  /****************************************************************/
  left=digitalRead(LEFTBUTTON);
  right=digitalRead(RIGHTBUTTON);
  if(left==HIGH&&right==LOW){
    display.clearDisplay();
    display.drawBitmap(0,0,_leftpawdown, 128, 40, 1);
    display.display();
    delay(25);
  }
  /****************************************************************/
  if(left==LOW&&right==HIGH){
    display.clearDisplay();
    display.drawBitmap(0,0,_rightpawdown , 128, 40, 1);
    display.display();
    delay(25);
  }
  /****************************************************************/
  if(left==HIGH&&right==HIGH){
    display.clearDisplay();
    display.drawBitmap(0,0,_bothpawsdown , 128, 40, 1);
    display.display();
    delay(25);
  }
  /****************************************************************/
  if(left==LOW&&right==LOW){
    display.clearDisplay();
    display.drawBitmap(0,0,_bothpawsup , 128, 40, 1);
    display.display();
    delay(25);
  }
  /****************************************************************/
  display.display();
}
