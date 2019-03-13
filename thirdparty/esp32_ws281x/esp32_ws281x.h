/* Created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * This is a driver for the WS281x RGB LEDs using the RMT peripheral on the ESP32.
 *
 * This code is placed in the public domain (or CC0 licensed, at your option).
 *
 * Adapted to p44utils context by Lukas Zeller <luz@plan44.ch>
 */

#ifndef ESP32_WS281x_H
#define ESP32_WS281x_H

#include <stdint.h>

typedef union {
  struct __attribute__ ((packed)) {
    uint8_t r, g, b;
  };
  uint32_t num;
} rgbVal;

extern void ws281x_init(int gpioNum);
extern void ws281x_setColors(unsigned int length, rgbVal *array);

inline rgbVal makeRGBVal(uint8_t r, uint8_t g, uint8_t b)
{
  rgbVal v;


  v.r = r;
  v.g = g;
  v.b = b;
  return v;
}

#endif /* ESP32_WS281x_H */
