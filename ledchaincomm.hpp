//
//  Copyright (c) 2016-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#include "colorutils.hpp"

#if ENABLE_P44LRGRAPHICS
#include "p44view.hpp"
#endif

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

  class LEDChainComm;
  typedef boost::intrusive_ptr<LEDChainComm> LEDChainCommPtr;

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
    uint16_t inactiveStartLeds; // number of inactive LEDs at the start of the chain
    uint16_t inactiveBetweenLeds; // number of inactive LEDs between physical rows
    uint16_t inactiveEndLeds; // number of inactive LEDs at the end of the chain (we have buffer for them, but do not set colors for them)
    uint16_t numLeds; // number of LEDs
    uint16_t ledsPerRow; // number of LEDs per row (physically, along WS2812 chain)
    uint16_t numRows; // number of rows (sections of WS2812 chain)
    bool xReversed; // even (0,2,4...) rows go backwards, or all if not alternating
    bool yReversed; // columns go backwards
    bool alternating; // direction changes after every row
    bool swapXY; // x and y swapped

    bool initialized;
    uint8_t numColorComponents; // depends on ledType

    LEDChainCommPtr chainDriver; // the LED chain used for outputting LED values. Usually: myself, but if this instance just maps a second part of another chain, this will point to the other chain

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
    /// @param aNumLeds number of LEDs in the chain (physically)
    /// @param aLedsPerRow number of consecutive LEDs in the WS2812 chain that build a row (active LEDs, not counting ainactiveBetweenLeds)
    ///   (usually x direction, y if swapXY was set). Set to 0 for single line of LEDs
    /// @param aXReversed X direction is reversed
    /// @param aAlternating X direction is reversed in first row, normal in second, reversed in third etc..
    /// @param aSwapXY X and Y reversed (for up/down wiring)
    /// @param aYReversed Y direction is reversed
    /// @param aInactiveStartLeds number of extra LEDs at the beginning of the chain that are not active
    /// @param aInactiveBetweenLeds number of extra LEDs between rows that are not active
    /// @param aInactiveEndLeds number of LEDs at the end of the chain that are not mapped by this instance (but might be in use by other instances which use this one with setChainDriver())
    LEDChainComm(
      LedType aLedType,
      const string aDeviceName,
      uint16_t aNumLeds,
      uint16_t aLedsPerRow=0,
      bool aXReversed=false,
      bool aAlternating=false,
      bool aSwapXY=false,
      bool aYReversed=false,
      uint16_t aInactiveStartLeds=0,
      uint16_t aInactiveBetweenLeds=0,
      uint16_t aInactiveEndLeds=0
    );

    /// destructor
    ~LEDChainComm();

    /// @return device (driver) name of this chain. Must be unqiuely identify the actual hardware output channel
    const string &getDeviceName() { return deviceName; };

    /// set this LedChainComm to use another chain's actual output driver, i.e. only act as mapping
    /// layer. This allows to have multiple different mappings on the same chain (i.e. part of the chain wound as tube,
    /// extending into a linear chain etc.)
    /// @param aLedChainComm a LedChainComm to be used to output LED values
    /// @note must be called before begin()
    void setChainDriver(LEDChainCommPtr aLedChainComm);

    /// @return true if this LedChainComm acts as a hardware driver (and not as a secondary)
    bool isHardwareDriver() { return chainDriver==NULL; };

    /// begin using the driver
    bool begin();

    /// end using the driver
    void end();

    /// transfer RGB values to LED chain
    /// @note this must be called to update the actual LEDs after modifying RGB values
    /// with setColor() and/or setColorDimmed()
    /// @note if this is a secondary mapping, show() does nothing - only the driving chain can transfer value to the hardware
    void show();

    /// clear all LEDs to off (but must call show() to actually show it
    void clear();

    /// get minimal color intensity that does not completely switch off the color channel of the LED
    /// @return minimum r,g,b value
    uint8_t getMinVisibleColorIntensity();

    /// set color of one LED
    /// @param aRed intensity of red component, 0..255
    /// @param aGreen intensity of green component, 0..255
    /// @param aBlue intensity of blue component, 0..255
    /// @param aWhite intensity of separate white component for RGBW LEDs, 0..255
    /// @note aLedNumber is the logical LED number, and aX/aY are logical coordinates, excluding any inactive LEDs
    void setColorXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite=0);
    void setColor(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite=0);

    /// set color of one LED, scaled by a visible brightness (non-linear) factor
    /// @param aRed intensity of red component, 0..255
    /// @param aGreen intensity of green component, 0..255
    /// @param aBlue intensity of blue component, 0..255
    /// @param aWhite intensity of separate white component for RGBW LEDs, 0..255
    /// @param aBrightness overall brightness, 0..255
    /// @note aLedNumber is the logical LED number, and aX/aY are logical coordinates, excluding any inactive LEDs
    void setColorDimmedXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite, uint8_t aBrightness);
    void setColorDimmed(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite, uint8_t aBrightness);

    /// get current color of LED
    /// @param aRed set to intensity of red component, 0..255
    /// @param aGreen set to intensity of green component, 0..255
    /// @param aBlue set to intensity of blue component, 0..255
    /// @note for LEDs set with setColorDimmed(), this returns the scaled down RGB values,
    ///   not the original r,g,b parameters. Note also that internal brightness resolution is 5 bits only.
    /// @note aLedNumber is the logical LED number, and aX/aY are logical coordinates, excluding any inactive LEDs
    void getColorXY(uint16_t aX, uint16_t aY, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);
    void getColor(uint16_t aLedNumber, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    /// @return number of active LEDs in the chain (that are active, i.e. minus inactiveStartLeds/inactiveBetweenLeds/inactiveEndLeds)
    uint16_t getNumLeds();

    /// @return size of array in X direction (x range is 0..getSizeX()-1)
    uint16_t getSizeX();

    /// @return size of array in Y direction (y range is 0..getSizeY()-1)
    uint16_t getSizeY();

  private:

    uint16_t ledIndexFromXY(uint16_t aX, uint16_t aY);

    /// set color at raw LED index with no mapping calculations in between
    void setColorAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite);

    /// set color from raw LED index with no mapping calculations in between
    void getColorAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);


  };


  #if ENABLE_P44LRGRAPHICS

  class LEDChainArrangement;
  typedef boost::intrusive_ptr<LEDChainArrangement> LEDChainArrangementPtr;

  class LEDChainArrangement : public P44Obj
  {

    class LEDChainFixture {
    public:
      LEDChainFixture(LEDChainCommPtr aLedChain, PixelRect aCovers, PixelPoint aOffset) : ledChain(aLedChain), covers(aCovers), offset(aOffset) {};
      LEDChainCommPtr ledChain; ///< a LED chain
      PixelRect covers; ///< the rectangle in the arrangement covered by this LED chain
      PixelPoint offset; ///< offset within the LED chain where to start coverage
    };

    typedef vector<LEDChainFixture> LedChainVector;

    LedChainVector ledChains;
    P44ViewPtr rootView;
    PixelRect covers;
    bool hasWhiteLEDs;

    MLMicroSeconds lastUpdate;
    MLTicket autoStepTicket;
    uint8_t maxOutValue;
    uint32_t powerLimit; // max power (accumulated PWM values of all LEDs)
    bool powerLimited; // set while power is limited

  public:

    /// minimum interval kept between updates to LED chain hardware
    MLMicroSeconds minUpdateInterval;

    /// maximum interval during which noisy view children are prevented from requesting rendering updates
    /// after prioritized (localTimingPriority==true) parent view did
    MLMicroSeconds maxPriorityInterval;

    LEDChainArrangement();
    virtual ~LEDChainArrangement();

    /// clear all LEDs to off
    void clear();

    /// set the root view
    /// @param  aRootView the view to ask for pixels in the arrangement
    void setRootView(P44ViewPtr aRootView);

    /// @return return current root view
    P44ViewPtr getRootView() { return rootView; }

    /// add a LED chain to the arrangement
    /// @param aLedChain the LED chain
    /// @param aCover the rectangle of pixels in the arrangement the led chain covers
    /// @param aOffset an offset within the chain where coverage starts
    void addLEDChain(LEDChainCommPtr aLedChain, PixelRect aCover, PixelPoint aOffset);

    /// add a LED chain to the arrangement by specification
    /// @param aChainSpec string describing the LED chain parameters and the coverage in the arrangement
    /// @note config syntax is as follows:
    ///   [chaintype:[leddevicename:]]numberOfLeds[:x:dx:y:dy:firstoffset:betweenoffset][:XYSA]
    ///   - chaintype can be SK6812, P9823 or WS281x
    ///   - XYSA are optional flags for X,Y reversal, x<>y Swap, Alternating chain direction
    void addLEDChain(const string &aChainSpec);

    /// returns the enclosing rectangle over all LED chains
    PixelRect totalCover() { return covers; }

    /// get minimal color intensity that does not completely switch off the color channel of the LED
    /// @return minimum r,g,b value
    uint8_t getMinVisibleColorIntensity();

    /// set max output value (0..255) to be sent to the LED chains
    void setMaxOutValue(uint8_t aMaxOutValue) { maxOutValue = aMaxOutValue; }

    /// get max output value (0..255) to be sent to the LED chains
    uint8_t getMaxOutValue() { return maxOutValue; }

    /// limit total power, dim LED chain output accordingly
    /// @param aMilliWatts how many milliwatts (approximatively) the total chain may use, 0=no limit
    void setPowerLimit(int aMilliWatts);

    /// @return true if any of the chains do have a separate (fourth) white channel
    bool hasWhite() { return hasWhiteLEDs; }

    /// start LED chains
    /// @param aAutoStep if true, step() will be called automatically as demanded by the view hierarchy
    void begin(bool aAutoStep);

    /// request re-rendering now
    /// @note this should be called when views have been changed from the outside
    /// @note a step() will be triggered ASAP
    void render();

    /// stop LED chains and auto updates
    void end();

    #if ENABLE_APPLICATION_SUPPORT

    /// - option to construct LEDChainArrangement from command line
    #define CMDLINE_LEDCHAIN_OPTIONS \
      { 0,   "ledchain",      true,  "[chaintype:[leddevicename:]]numberOfLeds:[x:dx:y:dy:firstoffs:betweenoffs][XYSA];" \
                                     "enable support for LED chains forming one or multiple RGB lights" \
                                     "\n- chaintype can be WS2812 (GRB, default), SK6812 (RGBW), P9823 (RGB)" \
                                     "\n- leddevicename can be a device name when chain is driven by a kernel module" \
                                     "\n- x,dx,y,dy,firstoffs,betweenoffs specify how the chain is mapped to the display space" \
                                     "\n- XYSA are flags: X or Y: x or y reversed, S: x/y swapped, A: alternating (zigzag)" \
                                     "\nNote: this option can be used multiple times to combine ledchains" }, \
      { 0,   "ledchainmax",   true,  "max;max output value (0..255) sent to LED. Defaults to 255." }, \
      { 0,   "ledpowerlimit", true,  "max_mW;maximal power in milliwatts the entire LED arrangement is allowed to consume (approximately)." \
                                     "If power would exceed limit, all LEDs are dimmed to stay below limit." \
                                     "Standby/off power of LED chains is not included in the calculation. Defaults to 0=no limit" }

    /// Factory helper to create ledchain arrangement with one or multiple led chains from --ledchain command line options
    /// @param aLedChainArrangement if not set, new arrangement will be created, otherwise existing one is used
    /// @param aChainSpec string describing the LED chain parameters and the coverage in the arrangement
    static void addLEDChain(LEDChainArrangementPtr &aLedChainArrangement, const string &aChainSpec);

    /// process ledchain arrangement specific command line options
    void processCmdlineOptions();

    #endif

  private:

    /// perform needed LED chain updates
    /// @return time when step() should be called again latest
    /// @note use stepASAP() to request a step an automatically schedule it at the next possible time
    ///    in case it cannot be executed right noow
    MLMicroSeconds step();

    void recalculateCover();
    MLMicroSeconds updateDisplay();

    void autoStep(MLTimer &aTimer);

    void rootViewRequestsUpdate();


  };


  #endif // ENABLE_P44LRGRAPHICS


} // namespace p44

#endif // __p44utils__ledchaincomm__

