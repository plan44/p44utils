/* Created 19 Nov 2016 by Chris Osborn <fozztexx@fozztexx.com>
 * http://insentricity.com
 *
 * This is a driver for the WS281x RGB LEDs using the RMT peripheral on the ESP32.
 *
 * This code is placed in the public domain (or CC0 licensed, at your option).
 *
 * Adapted to p44utils context and made flicker-free 2020 by Lukas Zeller <luz@plan44.ch>
 */

#ifndef ESP32_WS281x_H
#define ESP32_WS281x_H

#include <stdint.h>

// single pixel
typedef union {
  struct __attribute__ ((packed)) {
    uint8_t r, g, b, w;
  };
  uint32_t num;
} Esp_ws281x_pixel;


/// available LED types
typedef enum {
    esp_ledtype_ws2811,
    esp_ledtype_ws2812,
    esp_ledtype_ws2813,
    esp_ledtype_p9823,
    esp_ledtype_sk6812,
    esp_ledtype_ws2815_rgb,
  esp_num_ledtypes
} Esp_ws281x_LedType;


/// init the library
/// @param aMaxChains the max number of chains we'll probably use. Can be 1..8.
///   Lower number allow the driver to use more RMT memory per channel (more efficient,
///   more tolerant to slow IRQ response time w/o need for retries), so if you only need
///   few chains in an application, keep aMaxChains low as well.
extern void esp_ws281x_init(unsigned int aMaxChains);

/// add a chain
/// @param aLedType the type of LEDs connected
/// @param aGpioNo the GPIO to use for LED output
/// @param aMaxRetries max number of retry attempts to do before giving in case of too high IRQ response latency.
/// @return handle for the new chain
extern struct Esp_ws281x_LedChain* esp_ws281x_newChain(Esp_ws281x_LedType aLedType, unsigned int aGpioNo, unsigned int aMaxRetries);

/// remove a chain
/// @param aLedChain the led chain handle
extern void esp_ws281x_freeChain(struct Esp_ws281x_LedChain* aLedChain);

/// set new colors
/// @param aLedChain the led chain handle
/// @param aLength number of pixels
/// @param aPixels the pixel data
extern void esp_ws281x_setColors(struct Esp_ws281x_LedChain* aLedChain, unsigned int aLenght, Esp_ws281x_pixel *aPixels);


inline Esp_ws281x_pixel esp_ws281x_makeRGBVal(uint8_t r, uint8_t g, uint8_t b, uint8_t w)
{
  Esp_ws281x_pixel v;

  v.r = r;
  v.g = g;
  v.b = b;
  v.w = w;
  return v;
}

#endif /* ESP32_WS281x_H */
