//
//  Copyright (c) 2016-2018 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44utils is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44utils is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44utils. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef __p44utils__ledchaincomm__
#define __p44utils__ledchaincomm__

#include "p44utils_common.hpp"


#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#if ENABLE_RPIWS281X

// we use the rpi_ws281x library to communicate with the LED chains on RaspberryPi

extern "C" {

#include "clk.h"
#include "gpio.h"
#include "dma.h"
#include "pwm.h"

#include "ws2811.h"

}

#endif // ENABLE_RPIWS281X


using namespace std;

namespace p44 {


  /// convert PWM value to brightness
  /// @param aPWM PWM (energy) value 0..255
  /// @return brightness 0..255
  uint8_t pwmToBrightness(uint8_t aPWM);

  /// convert brightness value to PWM
  /// @param aBrightness brightness 0..255
  /// @return PWM (energy) value 0..255
  uint8_t brightnessToPwm(uint8_t aBrightness);


  class LEDChainComm : public P44Obj
  {
  public:

    typedef enum {
      ledtype_ws281x,  // RGB (with GRB subpixel order)
      ledtype_p9823,   // RGB (with RGB subpixel order)
      ledtype_sk6812   // RGBW (with RGBW subpixel order)
    } LedType;

  private:

    typedef P44Obj inherited;

    LedType ledType; // type of LED in the chain
    string deviceName; // the LED device name
    uint16_t numLeds; // number of LEDs
    uint16_t ledsPerRow; // number of LEDs per row (physically, along WS2812 chain)
    uint16_t numRows; // number of rows (sections of WS2812 chain)
    bool xReversed; // even (0,2,4...) rows go backwards, or all if not alternating
    bool yReversed; // columns go backwards
    bool alternating; // direction changes after every row
    bool swapXY; // x and y swapped

    bool initialized;
    uint8_t numColorComponents; // depends on ledType

    #if ENABLE_RPIWS281X
    ws2811_t ledstring; // the descriptor for the rpi_ws2811 library
    #else
    int ledFd; // the file descriptor for the LED device
    uint8_t *ledbuffer; // the raw bytes to be sent to the WS2812 device
    #endif

  public:
    /// create driver for a WS2812 LED chain
    /// @param aLedType type of LEDs
    /// @param aDeviceName the name of the LED chain device (if any, depends on platform)
    /// @param aNumLeds number of LEDs in the chain
    /// @param aLedsPerRow number of consecutive LEDs in the WS2812 chain that build a row
    ///   (usually x direction, y if swapXY was set). Set to 0 for single line of LEDs
    /// @param aXReversed X direction is reversed
    /// @param aAlternating X direction is reversed in first row, normal in second, reversed in third etc..
    /// @param aSwapXY X and Y reversed (for up/down wiring)
    /// @param aYReversed Y direction is reversed
    LEDChainComm(LedType aLedType, const string aDeviceName, uint16_t aNumLeds, uint16_t aLedsPerRow=0, bool aXReversed=false, bool aAlternating=false, bool aSwapXY=false, bool aYReversed=false);

    /// destructor
    ~LEDChainComm();

    /// begin using the driver
    bool begin();

    /// end using the driver
    void end();

    /// transfer RGB values to LED chain
    /// @note this must be called to update the actual LEDs after modifying RGB values
    /// with setColor() and/or setColorDimmed()
    void show();

    /// get minimal color intensity that does not completely switch off the color channel of the LED
    /// @return minimum r,g,b value
    uint8_t getMinVisibleColorIntensity();

    /// set color of one LED
    /// @param aRed intensity of red component, 0..255
    /// @param aGreen intensity of green component, 0..255
    /// @param aBlue intensity of blue component, 0..255
    /// @param aWhite intensity of separate white component for RGBW LEDs, 0..255
    void setColorXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite=0);
    void setColor(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite=0);

    /// set color of one LED, scaled by a visible brightness (non-linear) factor
    /// @param aRed intensity of red component, 0..255
    /// @param aGreen intensity of green component, 0..255
    /// @param aBlue intensity of blue component, 0..255
    /// @param aWhite intensity of separate white component for RGBW LEDs, 0..255
    /// @param aBrightness overall brightness, 0..255
    void setColorDimmedXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite, uint8_t aBrightness);
    void setColorDimmed(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite, uint8_t aBrightness);

    /// get current color of LED
    /// @param aRed set to intensity of red component, 0..255
    /// @param aGreen set to intensity of green component, 0..255
    /// @param aBlue set to intensity of blue component, 0..255
    /// @note for LEDs set with setColorDimmed(), this returns the scaled down RGB values,
    ///   not the original r,g,b parameters. Note also that internal brightness resolution is 5 bits only.
    void getColorXY(uint16_t aX, uint16_t aY, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);
    void getColor(uint16_t aLedNumber, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    /// @return number of LEDs
    uint16_t getNumLeds();

    /// @return size of array in X direction (x range is 0..getSizeX()-1)
    uint16_t getSizeX();

    /// @return size of array in Y direction (y range is 0..getSizeY()-1)
    uint16_t getSizeY();

  private:

    uint16_t ledIndexFromXY(uint16_t aX, uint16_t aY);

  };
  typedef boost::intrusive_ptr<LEDChainComm> LEDChainCommPtr;


} // namespace p44

#endif // __p44utils__ledchaincomm__

