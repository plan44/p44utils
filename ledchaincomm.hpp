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

#include "p44utils_main.hpp"
#include "colorutils.hpp"

#if ENABLE_P44LRGRAPHICS
#include "p44view.hpp"
#endif

#ifndef LEDCHAIN_LEGACY_API
#define LEDCHAIN_LEGACY_API (!ENABLE_P44LRGRAPHICS) // with p44graphics, we don't need legacy API any more
#endif

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>

#ifdef ESP_PLATFORM

// we use the esp32_ws281x code using RMT peripheral to generate correct timing
extern "C" {
  #include "esp32_ws281x.h"
}

#elif ENABLE_RPIWS281X

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
    friend class LEDChainArrangement;
    
  public:

    typedef enum {
      ledlayout_none, ///< if MSB of LEDCHAIN_PARAM_LEDTYPE is set to this, MSB=layout, LSB=chip
      ledlayout_rgb,
      ledlayout_grb,
      ledlayout_rgbw,
      ledlayout_grbw,
      // new modes added 2022-11-23 (also in p44-ledchain)
      ledlayout_rbg,
      ledlayout_gbr,
      ledlayout_brg,
      ledlayout_bgr,
      ledlayout_rbgw,
      ledlayout_gbrw,
      ledlayout_brgw,
      ledlayout_bgrw,
      num_ledlayouts
    } LedLayout;

    typedef enum {
      ledchip_none,
      ledchip_ws2811,
      ledchip_ws2812,
      ledchip_ws2813,
      ledchip_ws2815,
      ledchip_p9823,
      ledchip_sk6812,
      num_ledchips
    } LedChip;

    typedef struct {
      const char *name; // name of the chip
      int idleChipMw; // [mW] idle power consumption per LED chip (LEDs all off)
      int rgbChannelMw; // [mW] power consumption per R,G,B LED channel with full brightness (or total in case of rgbCommonCurrent==true)
      int whiteChannelMw; // [mW] power consumption of white channel (0 if none)
      bool rgbCommonCurrent; // if set, the LEDs are in a serial circuit with bridging PWM shortcuts, so max(r,g,b) defines consumption, not sum(r,g,b)
    } LedChipDesc;

  private:

    typedef P44Obj inherited;

    LedChip mLedChip; // type of LED chips in the chain
    LedLayout mLedLayout; // color layout in the LED chips
    uint16_t mTMaxPassive_uS; // max passive bit time in uS
    uint8_t mMaxRetries; // max number of update retries (for some low level drivers that may need to retry when IRQ hits at the wrong time)
    string mDeviceName; // the LED device name
    uint16_t mInactiveStartLeds; // number of inactive LEDs at the start of the chain
    uint16_t mInactiveBetweenLeds; // number of inactive LEDs between physical rows
    uint16_t mInactiveEndLeds; // number of inactive LEDs at the end of the chain (we have buffer for them, but do not set colors for them)
    uint16_t mNumLeds; // number of LEDs
    uint16_t mLedsPerRow; // number of LEDs per row (physically, along WS2812 chain)
    uint16_t mNumRows; // number of rows (sections of WS2812 chain)
    bool mXReversed; // even (0,2,4...) rows go backwards, or all if not alternating
    bool mYReversed; // columns go backwards
    bool mAlternating; // direction changes after every row
    bool mXYSwap; // x and y swapped

    bool mInitialized;
    uint8_t mNumColorComponents; // depends on ledType

    LEDChainCommPtr mChainDriver; // the LED chain used for outputting LED values. Usually: myself, but if this instance just maps a second part of another chain, this will point to the other chain

    #ifdef ESP_PLATFORM
    int gpioNo; // the GPIO to be used
    Esp_ws281x_LedChain* espLedChain; // handle for the chain
    Esp_ws281x_pixel* pixels; // the pixel buffer
    #elif ENABLE_RPIWS281X
    ws2811_t mRPiWS281x; // the descriptor for the rpi_ws281x library
    #else
    int ledFd; // the file descriptor for the LED device
    uint8_t *rawBuffer; // the raw bytes to be sent to the WS2812 device
    size_t rawBytes; // number of bytes to send from ledbuffer, including header
    uint8_t *ledBuffer; // the first led in the raw buffer (in case there is a header)
    #endif

    #if ENABLE_P44LRGRAPHICS
    Row3 mLEDWhite; ///< the color and intensity of the white channel LED (if any). @note this is used only in LEDChainArrangement
    #endif


  public:
    /// create driver for a WS2812 LED chain
    /// @param aLedType type of LED chips in "<chip>.<layout>[.<TMaxPassive_uS>]" form or just "<ledtypename>"
    /// @param aDeviceName the name of the LED chain device (if any, depends on platform)
    /// - ledchain device: full path like /dev/ledchain1
    /// - ESP32: name must be "gpioX" with X being the output pin to be used
    /// - RPi library (ENABLE_RPIWS281X): unused
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
      const string aLedType,
      const string aDeviceName,
      uint16_t aNumLeds,
      uint16_t aLedsPerRow=0,
      bool aXReversed=false,
      bool aAlternating=false,
      bool aXYSwap=false,
      bool aYReversed=false,
      uint16_t aInactiveStartLeds=0,
      uint16_t aInactiveBetweenLeds=0,
      uint16_t aInactiveEndLeds=0
    );

    /// destructor
    ~LEDChainComm();

    /// @return device (driver) name of this chain. Must be unqiuely identify the actual hardware output channel
    const string &getDeviceName() { return mDeviceName; };

    /// set this LedChainComm to use another chain's actual output driver, i.e. only act as mapping
    /// layer. This allows to have multiple different mappings on the same chain (i.e. part of the chain wound as tube,
    /// extending into a linear chain etc.)
    /// @param aLedChainComm a LedChainComm to be used to output LED values
    /// @note must be called before begin()
    void setChainDriver(LEDChainCommPtr aLedChainComm);

    /// @return true if this LedChainComm acts as a hardware driver (and not as a secondary)
    bool isHardwareDriver() { return mChainDriver==NULL; };

    /// begin using the driver
    /// @param aHintAtTotalChains if not 0, this is a hint to the total number of total chains, which the driver can use
    ///   for efficiently allocating internal resources (e.g. ESP32 driver can use more RMT memory when less channels are in use)
    bool begin(size_t aHintAtTotalChains = 0);

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

    /// @return true if chains has a separate (fourth) white channel
    bool hasWhite() { return mNumColorComponents>=4; }

    /// @return the LED chip descriptor which includes information about power consumption
    const LedChipDesc &ledChipDescriptor() const;

    #if LEDCHAIN_LEGACY_API

    /// set color of one LED
    /// @param aRed intensity of red component, 0..255
    /// @param aGreen intensity of green component, 0..255
    /// @param aBlue intensity of blue component, 0..255
    /// @param aWhite intensity of separate white component for RGBW LEDs, 0..255
    /// @note aLedNumber is the logical LED number, and aX/aY are logical coordinates, excluding any inactive LEDs
    void setColorXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite = 0);
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
    /// @param aWhite set to intensity of separate white component for RGBW LEDs, 0..255
    /// @note for LEDs set with setColorDimmed(), this returns the scaled down RGB values,
    ///   not the original r,g,b parameters. Note also that internal brightness resolution is 5 bits only.
    /// @note aLedNumber is the logical LED number, and aX/aY are logical coordinates, excluding any inactive LEDs
    void getColorXY(uint16_t aX, uint16_t aY, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);
    void getColor(uint16_t aLedNumber, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    #endif // LEDCHAIN_LEGACY_API

    /// set raw power (PWM value) of one LED
    /// @param aX logical X coordinate
    /// @param aY logical Y coordinate
    /// @param aRed power of red component, 0..255
    /// @param aGreen power of green component, 0..255
    /// @param aBlue power of blue component, 0..255
    /// @param aWhite power of separate white component for RGBW LEDs, 0..255
    void setPowerXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite = 0);

    /// set raw power (PWM value) of one LED
    /// @param aLedNumber is the logical LED number
    /// @param aRed power of red component, 0..255
    /// @param aGreen power of green component, 0..255
    /// @param aBlue power of blue component, 0..255
    /// @param aWhite power of separate white component for RGBW LEDs, 0..255
    void setPower(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite = 0);

    /// set power (PWM value) of one LED
    /// @param aX logical X coordinate
    /// @param aY logical Y coordinate
    /// @param aRed set to power of red component, 0..255
    /// @param aGreen set to power of green component, 0..255
    /// @param aBlue set to power of blue component, 0..255
    /// @param aWhite set to power of separate white component for RGBW LEDs, 0..255
    void getPowerXY(uint16_t aX, uint16_t aY, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    /// @return number of active LEDs in the chain (that are active, i.e. minus inactiveStartLeds/inactiveBetweenLeds/inactiveEndLeds)
    uint16_t getNumLeds();

    /// @return size of array in X direction (x range is 0..getSizeX()-1)
    uint16_t getSizeX();

    /// @return size of array in Y direction (y range is 0..getSizeY()-1)
    uint16_t getSizeY();

  private:

    uint16_t ledIndexFromXY(uint16_t aX, uint16_t aY);

    /// set power at raw LED index with no mapping calculations in between
    void setPowerAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite);

    /// get power at raw LED index with no mapping calculations in between
    void getPowerAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    #if LEDCHAIN_LEGACY_API

    /// set color at raw LED index with no mapping calculations in between
    void setColorAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite);

    /// set color from raw LED index with no mapping calculations in between
    void getColorAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite);

    #endif // LEDCHAIN_LEGACY_API

  };


  #if ENABLE_P44LRGRAPHICS

  class LEDChainArrangement;
  typedef boost::intrusive_ptr<LEDChainArrangement> LEDChainArrangementPtr;

  class LEDChainArrangement : public P44LoggingObj
  {
    class LEDChainFixture {
    public:
      LEDChainFixture(LEDChainCommPtr aLedChain, PixelRect aCovers, PixelPoint aOffset) : ledChain(aLedChain), covers(aCovers), offset(aOffset) {};
      LEDChainCommPtr ledChain; ///< a LED chain
      PixelRect covers; ///< the rectangle in the arrangement covered by this LED chain
      PixelPoint offset; ///< offset within the LED chain where to start coverage
    };

    typedef vector<LEDChainFixture> LedChainVector;

    LedChainVector mLedChains;
    P44ViewPtr mRootView;
    PixelRect mCovers;

    MLMicroSeconds mLastUpdate;
    MLTicket mAutoStepTicket;
    uint32_t mPowerLimitMw; // max power (accumulated PWM values of all LEDs)
    uint32_t mRequestedLightPowerMw; // light power currently requested (but possibly not actually output if >powerLimit)
    uint32_t mActualLightPowerMw; // light power actually used after dimming down because of limit
    bool mPowerLimited; // set while power is limited

    MLMicroSeconds mMinUpdateInterval; ///< minimum interval kept between updates to LED chain hardware
    MLMicroSeconds mMaxPriorityInterval; ///< maximum interval during which noisy view children are prevented from requesting rendering updates after prioritized (localTimingPriority==true) parent view did

  public:

    LEDChainArrangement();
    virtual ~LEDChainArrangement();

    /// @return the prefix to be used for logging from this object
    virtual string logContextPrefix() P44_OVERRIDE;

    /// clear all LEDs to off
    void clear();

    /// set the root view
    /// @param  aRootView the view to ask for pixels in the arrangement
    void setRootView(P44ViewPtr aRootView);

    /// @return return current root view
    P44ViewPtr getRootView() { return mRootView; }

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

    /// remove all LED chains
    void removeAllChains();

    /// returns the enclosing rectangle over all LED chains
    PixelRect totalCover() { return mCovers; }

    /// get minimal color intensity that does not completely switch off the color channel of the LED
    /// @return minimum r,g,b value
    uint8_t getMinVisibleColorIntensity();

    /// limit total power, dim LED chain output accordingly
    /// @param aMilliWatts how many milliwatts (approximatively) the total arrangement chains may use, 0=no limit
    void setPowerLimit(int aMilliWatts);

    /// get current power limit
    /// @return currently set power limit in milliwatts, 0=no limit
    int getPowerLimit();

    /// Return the power it *would* need to display the current state (altough power limiting might actually reducing it)
    /// @return how many milliwatts (approximatively) the total of all chains would use if not limited
    int getNeededPower();

    /// Return the current power (possibly limited)
    /// @return how many milliwatts (approximatively) all chains currently consume
    int getCurrentPower();

    /// Return minimal update interval between LED chain updates (= max frame rate)
    /// @param aMinUpdateInterval the minimal interval between LED chain updates
    MLMicroSeconds getMinUpdateInterval() { return mMinUpdateInterval; }

    /// Set the minimal update interval between LED chain updates (= max frame rate)
    /// The installed LED chain drivers will not be called more often than that, and
    /// the root view will get informed of that update interval as a hint for scheduling
    /// of animations etc.
    /// @param aMinUpdateInterval the minimal interval between LED chain updates
    void setMinUpdateInterval(MLMicroSeconds aMinUpdateInterval);

    /// Set the maximum priority interval, that is the interval after an display update when only prioritized views
    /// (such as scrollers that must run smoothly) will request updates when they get dirty. Other views
    /// getting dirty during that time will have to wait.
    /// @param aMaxPriorityInterval the maximum interval after display updates can only be requested by prioritized views
    void setMaxPriorityInterval(MLMicroSeconds aMaxPriorityInterval);

    /// start LED chains
    /// @param aAutoStep if true, step() will be called automatically as demanded by the view hierarchy
    void begin(bool aAutoStep);

    /// start chains only recently added (after first call to begin()
    void startChains();

    /// request re-rendering now
    /// @note this should be called when views have been changed from the outside
    /// @note a step() will be triggered ASAP
    void render();

    /// stop LED chains and auto updates
    void end();

    /// Factory helper to create ledchain arrangement with one or multiple led chains from --ledchain command line options
    /// @param aLedChainArrangement if not set, new arrangement will be created, otherwise existing one is used
    /// @param aChainSpec string describing the LED chain parameters and the coverage in the arrangement,
    ///   or "none" to not really add a ledchain, but instantiate an empty arrangement that can be configured later
    ///   e.g. by using p44script.
    static void addLEDChain(LEDChainArrangementPtr &aLedChainArrangement, const string &aChainSpec);

    #if ENABLE_APPLICATION_SUPPORT

    /// - option to construct LEDChainArrangement from command line
    #define CMDLINE_LEDCHAIN_OPTIONS \
      { 0,   "ledchain",      true,  "[ledtype:[leddevicename:]]numberOfLeds:[x:dx:y:dy:firstoffs:betweenoffs][XYSA][W#whitecolor];" \
                                     " enable support for adressable LED chains forming RGB(W) pixel display areas:" \
                                     "\n- ledtype is of <chip>.<layout> form, or one of WS2812, WS2813, SK6812, P9823 standard types." \
                                     "\n  Chips: WS2811,WS2812,WS2813,WS2815,SK6812,P9823,none" \
                                     "\n  Layouts: RGB,GRB,RGBW,GRBW,RBG,GBR,BRG,BGR,RBGW,GBRW,BRGW,BGRW" \
                                     "\n- leddevicename specifies the hardware output (usually LED device path)" \
                                     "\n- x,dx,y,dy,firstoffs,betweenoffs specify how the chain is mapped to the display space." \
                                     "\n- XYSA are flags: X or Y: x or y reversed, S: x/y swapped, A: alternating (zigzag)." \
                                     "\n- #whitecolor is a web color specification for the white channel of RGBW chains." \
                                     "\nNote: this option can be used multiple times to combine ledchains." }, \
      { 0,   "ledpowerlimit", true,  "max_mW;maximal power in milliwatts the entire LED arrangement is allowed to consume (approximately)." \
                                     "If power would exceed limit, all LEDs are dimmed to stay below limit. Defaults to 0=no limit." }, \
      { 0,   "ledrefresh",    true,  "update_ms;minimal number of milliseconds between LED chain updates. Defaults to 15ms." }

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

    void externalUpdateRequest();

  };


  #if ENABLE_P44SCRIPT

  namespace P44Script {

    /// represents the (singular) LED chain arrangement in this setup
    class LEDChainLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
      LEDChainArrangement& mLedChainArrangement;
    public:
      LEDChainLookup(LEDChainArrangement& aLedChainArrangement);
      LEDChainArrangement& ledChainArrangement() { return mLedChainArrangement; }
    };

  } // namespace P44Script

  #endif // ENABLE_P44SCRIPT

  #endif // ENABLE_P44LRGRAPHICS


} // namespace p44

#endif // __p44utils__ledchaincomm__

