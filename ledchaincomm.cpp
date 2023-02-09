//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0


#include "ledchaincomm.hpp"
#if ENABLE_APPLICATION_SUPPORT
  #include "application.hpp"
  #include "p44script.hpp"
#endif

using namespace p44;


// MARK: - LedChainComm

#if ENABLE_RPIWS281X
  #define TARGET_FREQ WS2811_TARGET_FREQ // in Hz, default is 800kHz
  #define GPIO_DEFAULT_PIN 18 // P1 Pin 12, GPIO 18 (PCM_CLK)
  #define DMA 5 // don't change unless you know why
  #define MAX_BRIGHTNESS 255 // full brightness range
#endif


// Power consumption according to
// https://www.thesmarthomehookup.com/the-complete-guide-to-selecting-individually-addressable-led-strips/
static const LEDChainComm::LedChipDesc ledChipDescriptors[LEDChainComm::num_ledchips] = {
  { "none",    0,   0,  0, false },
  { "WS2811",  8,  64,  0, false },
  { "WS2812",  4,  60,  0, false },
  { "WS2813",  4,  85,  0, false },
  { "WS2815", 24, 120,  0, true },
  { "P9823",   8,  80,  0, false }, // no real data, rough assumption
  { "SK6812",  6,  50, 95, false }
};


static const char* ledLayoutNames[LEDChainComm::num_ledlayouts] = {
  "none",
  "RGB",
  "GRB",
  "RGBW",
  "GRBW",
  // new since 2022-11-23
  "RBG",
  "GBR",
  "BRG",
  "BGR",
  "RBGW",
  "GBRW",
  "BRGW",
  "BGRW",
};


LEDChainComm::LEDChainComm(
  const string aLedType,
  const string aDeviceName,
  uint16_t aNumLeds,
  uint16_t aLedsPerRow,
  bool aXReversed,
  bool aAlternating,
  bool aXYSwap,
  bool aYReversed,
  uint16_t aInactiveStartLeds,
  uint16_t aInactiveBetweenLeds,
  uint16_t aInactiveEndLeds
) :
  mInitialized(false)
  #ifdef ESP_PLATFORM
  ,gpioNo(18) // sensible default
  ,pixels(NULL)
  #elif ENABLE_RPIWS281X
  #else
  ,ledFd(-1)
  ,rawBuffer(NULL)
  ,ledBuffer(NULL)
  ,rawBytes(0)
  #endif
{
  // set defaults
  mLedChip = ledchip_none; // none defined yet
  mLedLayout = ledlayout_none; // none defined yet
  mTMaxPassive_uS = 0;
  mMaxRetries = 0;
  mNumColorComponents = 3;
  // Parse led type string
  // - check legacy type names
  if (aLedType=="SK6812") {
    mLedChip = ledchip_sk6812;
    mLedLayout = ledlayout_grbw;
  }
  else if (aLedType=="P9823") {
    mLedChip = ledchip_p9823;
    mLedLayout = ledlayout_rgb;
  }
  else if (aLedType=="WS2815_RGB") {
    mLedChip = ledchip_ws2815;
    mLedLayout = ledlayout_rgb;
  }
  else if (aLedType=="WS2812") {
    mLedChip = ledchip_ws2812;
    mLedLayout = ledlayout_grb;
  }
  else if (aLedType=="WS2813") {
    mLedChip = ledchip_ws2813;
    mLedLayout = ledlayout_grb;
  }
  else {
    // Modern led definitions
    // <chip>.<layout>[.<TMaxPassive_uS>[.<maxRetries>]]
    const char* cP = aLedType.c_str();
    string part;
    if (nextPart(cP, part, '.')) {
      // chip
      for (int i=0; i<num_ledchips; i++) { if (strucmp(part.c_str(), ledChipDescriptors[i].name )==0) { mLedChip = (LedChip)i; break; } };
    }
    if (nextPart(cP, part, '.')) {
      // layout
      for (int i=0; i<num_ledlayouts; i++) { if (strucmp(part.c_str(), ledLayoutNames[i])==0) { mLedLayout = (LedLayout)i; break; } };
    }
    if (nextPart(cP, part, '.')) {
      // custom TPassive_max_nS
      mTMaxPassive_uS = atoi(part.c_str());
    }
    if (nextPart(cP, part, '.')) {
      // custom maxrepeat
      mMaxRetries = atoi(part.c_str());
    }
  }
  // device name/channel
  mDeviceName = aDeviceName;
  mNumColorComponents = ledChipDescriptors[mLedChip].whiteChannelMw>0 ? 4 : 3;
  mInactiveStartLeds = aInactiveStartLeds;
  mInactiveBetweenLeds = aInactiveBetweenLeds;
  mInactiveEndLeds = aInactiveEndLeds;
  // number of LEDs
  mNumLeds = aNumLeds;
  if (aLedsPerRow==0) {
    mLedsPerRow = aNumLeds-aInactiveStartLeds; // single row
    mNumRows = 1;
  }
  else {
    mLedsPerRow = aLedsPerRow; // set row size
    mNumRows = (mNumLeds-1-mInactiveStartLeds-mInactiveEndLeds)/(mLedsPerRow+mInactiveBetweenLeds)+1; // calculate number of (full or partial) rows
  }
  mXReversed = aXReversed;
  mYReversed = aYReversed;
  mAlternating = aAlternating;
  mXYSwap = aXYSwap;
  // make sure operation ends when mainloop terminates
  MainLoop::currentMainLoop().registerCleanupHandler(boost::bind(&LEDChainComm::end, this));
}

LEDChainComm::~LEDChainComm()
{
  // end the operation when object gets deleted
  end();
}


void LEDChainComm::setChainDriver(LEDChainCommPtr aLedChainComm)
{
  mChainDriver = aLedChainComm;
}


const LEDChainComm::LedChipDesc &LEDChainComm::ledChipDescriptor() const
{
  return ledChipDescriptors[mLedChip];
}


// MARK: - LEDChainComm physical LED chain driver

#ifdef ESP_PLATFORM

static bool gEsp32_ws281x_initialized = false;

#define ESP32_LEDCHAIN_MAX_RETRIES 3

#endif // CONFIG_P44UTILS_DIGITAL_LED_LIB



bool LEDChainComm::begin(size_t aHintAtTotalChains)
{
  if (!mInitialized) {
    if (mChainDriver) {
      // another LED chain outputs to the hardware, make sure that one is up
      mInitialized = mChainDriver->begin();
    }
    else {
      #ifdef ESP_PLATFORM
      if (mDeviceName.substr(0,4)=="gpio") {
        sscanf(mDeviceName.c_str()+4, "%d", &gpioNo);
      }
      // make sure library is initialized
      if (!gEsp32_ws281x_initialized) {
        FOCUSLOG("calling esp_ws281x_init for %d chains", aHintAtTotalChains);
        esp_ws281x_init((int)aHintAtTotalChains);
        gEsp32_ws281x_initialized = true;
      }
      // add this chain
      // TODO: refactor low level driver to use separate chip/layout params, too
      Esp_ws281x_LedType elt;
      switch (mLedChip) {
        case ledchip_ws2811:
        case ledchip_ws2812:
          elt = esp_ledtype_ws2812;
          break;
        case ledchip_sk6812:
          elt = esp_ledtype_sk6812;
          break;
        case ledchip_p9823:
          elt = esp_ledtype_p9823;
          break;
        case ledchip_ws2815:
          if (mLedLayout==ledlayout_rgb) elt = esp_ledtype_ws2815_rgb;
          else elt = esp_ledtype_ws2813;
          break;
        case ledchip_ws2813:
        default:
          elt = esp_ledtype_ws2813;
          break;
      }
      espLedChain = esp_ws281x_newChain(elt, gpioNo, ESP32_LEDCHAIN_MAX_RETRIES);
      if (espLedChain) {
        // prepare buffer
        if (pixels) {
          delete[] pixels;
          pixels = NULL;
        }
        pixels = new Esp_ws281x_pixel[mNumLeds];
        mInitialized = true;
        clear();
      }
      else {
        LOG(LOG_ERR, "Error: esp_ws281x_newChain failed");
        return false; // cannot initialize LED chain
      }
      #elif ENABLE_RPIWS281X
      int gpio = GPIO_DEFAULT_PIN;
      bool inverted = false;
      size_t n=0;
      if (mDeviceName.substr(n,1)=="!") {
        inverted = true;
        n++;
      }
      if (mDeviceName.substr(n,4)=="gpio") {
        sscanf(mDeviceName.c_str()+4, "%d", &gpio);
      }
      // prepare hardware related stuff
      memset(&mRPiWS281x, 0, sizeof(mRPiWS281x));
      // initialize the led string structure
      mRPiWS281x.freq = TARGET_FREQ;
      mRPiWS281x.dmanum = DMA;
      mRPiWS281x.device = NULL; // private data pointer for library
      // channel 0
      mRPiWS281x.channel[0].gpionum = gpio;
      mRPiWS281x.channel[0].count = mNumLeds;
      mRPiWS281x.channel[0].invert = inverted ? 1 : 0;
      mRPiWS281x.channel[0].brightness = MAX_BRIGHTNESS;
      // map chips/layouts
      switch (mLedChip) {
        case ledchip_sk6812: {
          // 4 channel, "SK6812" in RPIWS281X lingo
          switch (mLedLayout) {
            default:
            case ledlayout_rgbw: mRPiWS281x.channel[0].strip_type = SK6812_STRIP_RGBW; break;
            case ledlayout_rbgw: mRPiWS281x.channel[0].strip_type = SK6812_STRIP_RBGW; break;
            case ledlayout_grbw: mRPiWS281x.channel[0].strip_type = SK6812_STRIP_GRBW; break;
            case ledlayout_gbrw: mRPiWS281x.channel[0].strip_type = SK6812_STRIP_GBRW; break;
            case ledlayout_brgw: mRPiWS281x.channel[0].strip_type = SK6812_STRIP_BRGW; break;
            case ledlayout_bgrw: mRPiWS281x.channel[0].strip_type = SK6812_STRIP_BGRW; break;
          }
          break;
        }
        default:
        case ledchip_ws2811:
        case ledchip_ws2812:
        case ledchip_ws2813:
        case ledchip_ws2815:
        case ledchip_p9823: {
          switch (mLedLayout) {
            case ledlayout_rgb: mRPiWS281x.channel[0].strip_type = WS2811_STRIP_RGB; break;
            case ledlayout_rbg: mRPiWS281x.channel[0].strip_type = WS2811_STRIP_RBG; break;
            default:
            case ledlayout_grb: mRPiWS281x.channel[0].strip_type = WS2811_STRIP_GRB; break;
            case ledlayout_gbr: mRPiWS281x.channel[0].strip_type = WS2811_STRIP_GBR; break;
            case ledlayout_brg: mRPiWS281x.channel[0].strip_type = WS2811_STRIP_BRG; break;
            case ledlayout_bgr: mRPiWS281x.channel[0].strip_type = WS2811_STRIP_BGR; break;
          }
          break;
        }
      }
      mRPiWS281x.channel[0].leds = NULL; // will be allocated by the library
      // channel 1 - unused
      mRPiWS281x.channel[1].gpionum = 0;
      mRPiWS281x.channel[1].count = 0;
      mRPiWS281x.channel[1].invert = 0;
      mRPiWS281x.channel[1].brightness = MAX_BRIGHTNESS;
      mRPiWS281x.channel[1].leds = NULL; // will be allocated by the library
      // initialize library
      ws2811_return_t ret = ws2811_init(&mRPiWS281x);
      if (ret==WS2811_SUCCESS) {
        mInitialized = true;
      }
      else {
        LOG(LOG_ERR, "Error: ws281x init for GPIO%d failed: %s", gpio, ws2811_get_return_t_str(ret));
        mInitialized = false;
      }
      #else
      // Allocate led buffer
      if (rawBuffer) {
        delete[] rawBuffer;
        rawBuffer = NULL;
        ledBuffer = NULL;
        rawBytes = 0;
      }
      if (mLedChip!=ledchip_none) {
        const int hdrsize = 5; // v6 header size
        rawBytes = mNumColorComponents*mNumLeds+1+hdrsize;
        rawBuffer = new uint8_t[rawBytes];
        ledBuffer = rawBuffer+1+hdrsize; // led data starts here
        // prepare header for p44-ledchain v6 and later compatible drivers
        rawBuffer[0] = hdrsize; // driver v6 header size
        rawBuffer[1] = mLedLayout;
        rawBuffer[2] = mLedChip;
        rawBuffer[3] = (mTMaxPassive_uS>>8) & 0xFF;
        rawBuffer[4] = (mTMaxPassive_uS) & 0xFF;
        rawBuffer[5] = mMaxRetries;
      }
      else {
        // chip not known here: must be legacy driver w/o header
        rawBytes = mNumColorComponents*mNumLeds;
        rawBuffer = new uint8_t[rawBytes];
        ledBuffer = rawBuffer;
      }
      memset(ledBuffer, 0, mNumColorComponents*mNumLeds);
      ledFd = open(mDeviceName.c_str(), O_RDWR);
      if (ledFd>=0) {
        mInitialized = true;
      }
      else {
        LOG(LOG_ERR, "Error: Cannot open LED chain device '%s'", mDeviceName.c_str());
        mInitialized = false;
      }
      #endif
    }
  }
  return mInitialized;
}


void LEDChainComm::clear()
{
  if (!mInitialized) return;
  if (mChainDriver) {
    // this is just a secondary mapping on a primary chain: clear only the actually mapped LEDs
    for (uint16_t led = mInactiveStartLeds; led<mNumLeds-mInactiveEndLeds; led++) {
      mChainDriver->setPower(led, 0, 0, 0, 0);
    }
  }
  else {
    // this is the master driver, clear the entire buffer
    #ifdef ESP_PLATFORM
    for (uint16_t i=0; i<mNumLeds; i++) pixels[i].num = 0;
    #elif ENABLE_RPIWS281X
    for (uint16_t i=0; i<mNumLeds; i++) mRPiWS281x.channel[0].leds[i] = 0;
    #else
    memset(ledBuffer, 0, mNumColorComponents*mNumLeds);
    #endif
  }
}


void LEDChainComm::end()
{
  if (mInitialized) {
    if (!mChainDriver) {
      #ifdef ESP_PLATFORM
      if (pixels) {
        delete[] pixels;
        pixels = NULL;
      }
      esp_ws281x_freeChain(espLedChain);
      espLedChain = NULL;
      #elif ENABLE_RPIWS281X
      // deinitialize library
      ws2811_fini(&mRPiWS281x);
      #else
      if (rawBuffer) {
        delete[] rawBuffer;
        rawBuffer = NULL;
        ledBuffer = NULL;
        rawBytes = 0;
      }
      if (ledFd>=0) {
        close(ledFd);
        ledFd = -1;
      }
      #endif
    }
  }
  mInitialized = false;
}


void LEDChainComm::show()
{
  if (!mChainDriver) {
    // Note: no operation if this is only a secondary mapping - primary driver will update the hardware
    if (!mInitialized) return;
    #ifdef ESP_PLATFORM
    esp_ws281x_setColors(espLedChain, mNumLeds, pixels);
    #elif ENABLE_RPIWS281X
    ws2811_render(&mRPiWS281x);
    #else
    write(ledFd, rawBuffer, rawBytes); // with header
    #endif
  }
}


#if LEDCHAIN_LEGACY_API

void LEDChainComm::setColorAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  // get power (PWM values)
  uint8_t r = pwmtable[aRed];
  uint8_t g = pwmtable[aGreen];
  uint8_t b = pwmtable[aBlue];
  uint8_t w = pwmtable[aWhite];
  setPowerAtLedIndex(aLedIndex, r, g, b, w);
}


void LEDChainComm::getColorAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  uint8_t r,g,b,w;
  getPowerAtLedIndex(aLedIndex, r, g, b, w);
  aRed = brightnesstable[r];
  aGreen = brightnesstable[g];
  aBlue = brightnesstable[b];
  aWhite = brightnesstable[w];
}

#endif // LEDCHAIN_LEGACY_API


void LEDChainComm::setPowerAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  if (mChainDriver) {
    // delegate actual output
    mChainDriver->setPowerAtLedIndex(aLedIndex, aRed, aGreen, aBlue, aWhite);
  }
  else {
    // local driver, store change in my own LED buffer
    if (aLedIndex>=mNumLeds) return;
    #ifdef ESP_PLATFORM
    if (!pixels) return;
    pixels[aLedIndex] = esp_ws281x_makeRGBVal(aRed, aGreen, aBlue, aWhite);
    #elif ENABLE_RPIWS281X
    ws2811_led_t pixel =
      ((uint32_t)aRed << 16) |
      ((uint32_t)aGreen << 8) |
      ((uint32_t)aBlue);
    if (mNumColorComponents>3) {
      pixel |= ((uint32_t)aWhite << 24);
    }
    mRPiWS281x.channel[0].leds[aLedIndex] = pixel;
    #else
    ledBuffer[mNumColorComponents*aLedIndex] = aRed;
    ledBuffer[mNumColorComponents*aLedIndex+1] = aGreen;
    ledBuffer[mNumColorComponents*aLedIndex+2] = aBlue;
    if (mNumColorComponents>3) {
      ledBuffer[mNumColorComponents*aLedIndex+3] = aWhite;
    }
    #endif
  }
}


void LEDChainComm::getPowerAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  if (mChainDriver) {
    // delegate actual output
    mChainDriver->getPowerAtLedIndex(aLedIndex, aRed, aGreen, aBlue, aWhite);
  }
  else {
    if (aLedIndex>=mNumLeds) return;
    #ifdef ESP_PLATFORM
    if (!pixels) return;
    Esp_ws281x_pixel &pixel = pixels[aLedIndex];
    aRed = pixel.r;
    aGreen = pixel.g;
    aBlue = pixel.b;
    aWhite = pixel.w;
    #elif ENABLE_RPIWS281X
    ws2811_led_t pixel = mRPiWS281x.channel[0].leds[aLedIndex];
    aRed = (pixel>>16) & 0xFF;
    aGreen = (pixel>>8) & 0xFF;
    aBlue = pixel & 0xFF;
    if (mNumColorComponents>3) {
      aWhite = (pixel>>24) & 0xFF;
    }
    else {
      aWhite = 0;
    }
    #else
    aRed = ledBuffer[mNumColorComponents*aLedIndex];
    aGreen = ledBuffer[mNumColorComponents*aLedIndex+1];
    aBlue = ledBuffer[mNumColorComponents*aLedIndex+2];
    if (mNumColorComponents>3) {
      aWhite = ledBuffer[mNumColorComponents*aLedIndex+3];
    }
    else {
      aWhite = 0;
    }
    #endif
  }
}



// MARK: - LEDChainComm logical LED access

uint16_t LEDChainComm::getNumLeds()
{
  return mNumLeds-mInactiveStartLeds-mInactiveEndLeds-(mNumRows-1)*mInactiveBetweenLeds;
}


uint16_t LEDChainComm::getSizeX()
{
  return mXYSwap ? mNumRows : mLedsPerRow;
}


uint16_t LEDChainComm::getSizeY()
{
  return mXYSwap ? mLedsPerRow : mNumRows;
}


uint8_t LEDChainComm::getMinVisibleColorIntensity()
{
  // return highest brightness that still produces lowest non-zero output.
  // (which is: lowest brightness that produces 2, minus 1)
  // we take the upper limit so the chance of seeing something even for not pure r,g,b combinations is better
  return brightnesstable[2]-1;
}


uint16_t LEDChainComm::ledIndexFromXY(uint16_t aX, uint16_t aY)
{
  //FOCUSLOG("ledIndexFromXY: X=%d, Y=%d", aX, aY);
  if (mXYSwap) { uint16_t tmp = aY; aY = aX; aX = tmp; }
  if (mYReversed) { aY = mNumRows-1-aY; }
  uint16_t ledindex = aY*(mLedsPerRow+mInactiveBetweenLeds);
  bool reversed = mXReversed;
  if (mAlternating) {
    if (aY & 0x1) reversed = !reversed;
  }
  if (reversed) {
    ledindex += (mLedsPerRow-1-aX);
  }
  else {
    ledindex += aX;
  }
  //FOCUSLOG("--> ledIndex=%d", ledindex);
  return ledindex+mInactiveStartLeds;
}


#if LEDCHAIN_LEGACY_API

void LEDChainComm::setColorXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  setColorAtLedIndex(ledindex, aRed, aGreen, aBlue, aWhite);
}


void LEDChainComm::setColor(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  //FOCUSLOG("setColor: ledNumber=%d - sizeX=%d, sizeY=%d", aLedNumber, getSizeX(), getSizeY());
  int y = aLedNumber / getSizeX();
  int x = aLedNumber % getSizeX();
  setColorXY(x, y, aRed, aGreen, aBlue, aWhite);
}


void LEDChainComm::setColorDimmedXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite, uint8_t aBrightness)
{
  setColorXY(aX, aY, (aRed*aBrightness)>>8, (aGreen*aBrightness)>>8, (aBlue*aBrightness)>>8, (aWhite*aBrightness)>>8);
}


void LEDChainComm::setColorDimmed(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite, uint8_t aBrightness)
{
  int y = aLedNumber / getSizeX();
  int x = aLedNumber % getSizeX();
  setColorDimmedXY(x, y, aRed, aGreen, aBlue, aWhite, aBrightness);
}


void LEDChainComm::getColor(uint16_t aLedNumber, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  int y = aLedNumber / getSizeX();
  int x = aLedNumber % getSizeX();
  getColorXY(x, y, aRed, aGreen, aBlue, aWhite);
}


void LEDChainComm::getColorXY(uint16_t aX, uint16_t aY, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  getColorAtLedIndex(ledindex, aRed, aGreen, aBlue, aWhite);
}

#endif // LEDCHAIN_LEGACY_API


void LEDChainComm::setPowerXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  setPowerAtLedIndex(ledindex, aRed, aGreen, aBlue, aWhite);
}


void LEDChainComm::setPower(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  int y = aLedNumber / getSizeX();
  int x = aLedNumber % getSizeX();
  setPowerXY(x, y, aRed, aGreen, aBlue, aWhite);
}


void LEDChainComm::getPowerXY(uint16_t aX, uint16_t aY, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  getPowerAtLedIndex(ledindex, aRed, aGreen, aBlue, aWhite);
}




#if ENABLE_P44LRGRAPHICS

// MARK: - LEDChainArrangement

#if DEBUG
  #define MAX_STEP_INTERVAL (10*Second) // run a step at least in this interval, even if view step() indicates no need to do so early
  #define MAX_UPDATE_INTERVAL (10*Second) // send an update at least this often, even if no changes happen (LED refresh)
#else
  #define MAX_STEP_INTERVAL (1*Second) // run a step at least in this interval, even if view step() indicates no need to do so early
  #define MAX_UPDATE_INTERVAL (500*MilliSecond) // send an update at least this often, even if no changes happen (LED refresh)
#endif
#define DEFAULT_MIN_UPDATE_INTERVAL (15*MilliSecond) // do not send updates faster than this
#define DEFAULT_MAX_PRIORITY_INTERVAL (50*MilliSecond) // allow synchronizing prioritized timing for this timespan after the last LED refresh
#define MAX_SLOW_WARN_INTERVAL (10*Second) // how often (max) the timing violation detection will be logged

LEDChainArrangement::LEDChainArrangement() :
  mStarted(false),
  mCovers(zeroRect),
  mPowerLimitMw(0),
  mRequestedLightPowerMw(0),
  mActualLightPowerMw(0),
  mPowerLimited(false),
  mLastUpdate(Never),
  mMinUpdateInterval(DEFAULT_MIN_UPDATE_INTERVAL),
  mMaxPriorityInterval(DEFAULT_MAX_PRIORITY_INTERVAL),
  mSlowDetected(Never)
{
  #if ENABLE_P44SCRIPT && ENABLE_P44LRGRAPHICS && ENABLE_VIEWCONFIG
  // install p44script lookup providing "ledchain" global
  // Note: assuming only ONE ledchain arrangement per application.
  // TODO: Nothing currently prevents instantiating multiple, but makes no sense
  p44::P44Script::StandardScriptingDomain::sharedDomain().registerMemberLookup(
    new P44Script::LEDChainLookup(*this)
  );
  #endif // ENABLE_P44SCRIPT
}


LEDChainArrangement::~LEDChainArrangement()
{
  end();
}


string LEDChainArrangement::logContextPrefix()
{
  return "LEDchains";
}


void LEDChainArrangement::clear()
{
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    pos->ledChain->clear();
    pos->ledChain->show();
  }
}


void LEDChainArrangement::setRootView(P44ViewPtr aRootView)
{
  if (mRootView) {
    mRootView->setNeedUpdateCB(NoOP); // make sure previous rootview will not call back any more!
  }
  mRootView = aRootView;
  mRootView->setDefaultLabel("rootview");
  mRootView->setNeedUpdateCB(boost::bind(&LEDChainArrangement::externalUpdateRequest, this));
  mRootView->setMinUpdateInterval(mMinUpdateInterval);
}


void LEDChainArrangement::setMinUpdateInterval(MLMicroSeconds aMinUpdateInterval)
{
  mMinUpdateInterval = aMinUpdateInterval;
  if (mRootView) mRootView->setMinUpdateInterval(mMinUpdateInterval);
}


void LEDChainArrangement::setMaxPriorityInterval(MLMicroSeconds aMaxPriorityInterval)
{
  mMaxPriorityInterval = aMaxPriorityInterval;
}


#if ENABLE_P44LRGRAPHICS
#include "viewfactory.hpp"
#endif

void LEDChainArrangement::addLEDChain(LEDChainArrangementPtr &aLedChainArrangement, const string &aChainSpec)
{
  if (aChainSpec.empty()) return;
  if (!aLedChainArrangement) {
    // create the arrangement, which registers global p44script functions for managing led chains
    aLedChainArrangement = LEDChainArrangementPtr(new LEDChainArrangement);
    #if ENABLE_P44SCRIPT && ENABLE_P44LRGRAPHICS && ENABLE_VIEWCONFIG
    // also install p44script lookup providing "lrg" global
    p44::P44Script::StandardScriptingDomain::sharedDomain().registerMemberLookup(
      new P44Script::P44lrgLookup(&(aLedChainArrangement->mRootView))
    );
    #endif // ENABLE_P44SCRIPT
  }
  // "adding" a ledchain with spec "none" just creates the ledchainarrangement, so ledchains can be added via p44script
  if (aChainSpec!="none") {
    // now add chain
    aLedChainArrangement->addLEDChain(aChainSpec);
  }
}


#if ENABLE_APPLICATION_SUPPORT

void LEDChainArrangement::processCmdlineOptions()
{
  int v;
  if (CmdLineApp::sharedCmdLineApp()->getIntOption("ledpowerlimit", v)) {
    setPowerLimit(v);
  }
  if (CmdLineApp::sharedCmdLineApp()->getIntOption("ledrefresh", v)) {
    mMinUpdateInterval = v*MilliSecond;
  }
}

#endif // ENABLE_APPLICATION_SUPPORT


void LEDChainArrangement::addLEDChain(const string &aChainSpec)
{
  string ledType = "WS2813.GRB"; // assume WS2812/13
  string deviceName;
  int numleds = 200;
  bool xReversed = false;
  bool alternating = false;
  bool swapXY = false;
  bool yReversed = false;
  PixelColor ledWhite = { 0xAA, 0xAA, 0xAA, 0xFF }; // Asume double white LED power compared with one of R,G,B
  uint16_t inactiveStartLeds = 0;
  uint16_t inactiveBetweenLeds = 0;
  int remainingInactive = 0;
  PixelRect newCover;
  newCover.x = 0;
  newCover.y = 0;
  newCover.dx = numleds;
  newCover.dy = 1;
  PixelPoint offsets = { 0, 0 };
  // parse chain specification
  // Syntax: [ledstype:[leddevicename:]]numberOfLeds:[x:dx:y:dy:firstoffset:betweenoffset][XYSA][W#whitecolor]
  // where:
  // - ledstype is either a single word for old-style drivers (p44-ledchain before v6) or of the form
  //   <chip>.<layout>[.<TMaxPassive_uS>] for drivers that allow controlling type directly (p44-ledchain from v6 onwards)
  //   Usually supported chips are: WS2811, WS2812, WS2813, WS2815, SK6812, P9823
  //   Uusually supported layouts are: RGB, GRB, RGBW, GRBW
  string part;
  const char *p = aChainSpec.c_str();
  int nmbrcnt = 0;
  int txtcnt = 0;
  while (nextPart(p, part, ':')) {
    if (!isdigit(part[0])) {
      // text
      if (nmbrcnt==0) {
        // texts before first number
        if (txtcnt==0) {
          // LEDs type
          ledType = part;
          txtcnt++;
        }
        else if (txtcnt==1) {
          // leddevicename
          deviceName = part;
          txtcnt++;
        }
      }
      else {
        // text after first number are options
        for (size_t i=0; i<part.size(); i++) {
          switch (part[i]) {
            case 'X': xReversed = true; break;
            case 'Y': yReversed = true; break;
            case 'S': swapXY = true; break;
            case 'A': alternating = true; break;
            case 'W':
              ledWhite = webColorToPixel(part.substr(i+1));
              i = part.size(); // W#whitecol ends the part (more options could follow after another colon
              break;
          }
        }
      }
    }
    else {
      // number
      int n = atoi(part.c_str());
      switch (nmbrcnt) {
        case 0: numleds = n; newCover.dx = n; break;
        case 1: newCover.x = n; break;
        case 2: newCover.dx = n; break;
        case 3: newCover.y = n; break;
        case 4: newCover.dy = n; break;
        case 5: inactiveStartLeds = n; break;
        case 6: inactiveBetweenLeds = n; break;
        default: break;
      }
      nmbrcnt++;
    }
  }
  // calculate remaining inactive LEDs at end of chain
  remainingInactive = numleds-inactiveStartLeds-newCover.dx*newCover.dy-((swapXY ? newCover.dx : newCover.dy)-1)*inactiveBetweenLeds;
  if (remainingInactive<0) {
    OLOG(LOG_WARNING, "Specified area needs %d more LEDs than actually are available: %s", -remainingInactive, aChainSpec.c_str());
    remainingInactive = 0; // overflow, nothing remains
  }
  // now instantiate chain
  LEDChainCommPtr ledChain = LEDChainCommPtr(new LEDChainComm(
    ledType,
    deviceName,
    numleds,
    (swapXY ? newCover.dy : newCover.dx), // ledsPerRow
    xReversed,
    alternating,
    swapXY,
    yReversed,
    inactiveStartLeds,
    inactiveBetweenLeds,
    remainingInactive
  ));
  ledChain->mLEDWhite[0] = (double)ledWhite.r/255;
  ledChain->mLEDWhite[1] = (double)ledWhite.g/255;
  ledChain->mLEDWhite[2] = (double)ledWhite.b/255;
  OLOG(LOG_INFO,
    "installed chain covering area: x=%d, dx=%d, y=%d, dy=%d on device '%s'. %d LEDs inactive at start, %d at end.",
    newCover.x, newCover.dx, newCover.y, newCover.dy, ledChain->getDeviceName().c_str(),
    inactiveStartLeds, remainingInactive
  );
  // check for being a secondary chain mapping for an already driven chain
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    LEDChainFixture& l = *pos;
    if (l.ledChain && l.ledChain->getDeviceName()==deviceName && l.ledChain->isHardwareDriver()) {
      // chain with same driver name already exists, install this chain as a secondary mapping only
      ledChain->setChainDriver(l.ledChain); // use found chain as actual output
      OLOG(LOG_INFO, "- chain is a secondary mapping for device '%s'", l.ledChain->getDeviceName().c_str());
      break;
    }
  }
  addLEDChain(ledChain, newCover, offsets);
}


void LEDChainArrangement::addLEDChain(LEDChainCommPtr aLedChain, PixelRect aCover, PixelPoint aOffset)
{
  if (!aLedChain) return; // no chain
  mLedChains.push_back(LEDChainFixture(aLedChain, aCover, aOffset));
  recalculateCover();
  OLOG(LOG_INFO,
    "- enclosing rectangle of all covered areas: x=%d, dx=%d, y=%d, dy=%d",
      mCovers.x, mCovers.dx, mCovers.y, mCovers.dy
  );
}


void LEDChainArrangement::removeAllChains()
{
  clear(); // all LEDs off
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    pos->ledChain->end(); // end updates
  }
  mLedChains.clear(); // remove all chains
  mCovers = zeroRect; // nothing covered right now
}


void LEDChainArrangement::recalculateCover()
{
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    LEDChainFixture& l = *pos;
    if (l.covers.dx>0 && l.covers.dy>0) {
      if (mCovers.dx==0 || l.covers.x<mCovers.x) mCovers.x = l.covers.x;
      if (mCovers.dy==0 || l.covers.y<mCovers.y) mCovers.y = l.covers.y;
      if (mCovers.dx==0 || l.covers.x+l.covers.dx>mCovers.x+mCovers.dx) mCovers.dx = l.covers.x+l.covers.dx-mCovers.x;
      if (mCovers.dy==0 || l.covers.y+l.covers.dy>mCovers.y+mCovers.dy) mCovers.dy = l.covers.y+l.covers.dy-mCovers.y;
    }
  }
}


uint8_t LEDChainArrangement::getMinVisibleColorIntensity()
{
  uint8_t min = 1; // can't be lower than that, 0 would be off
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    uint8_t lmin = pos->ledChain->getMinVisibleColorIntensity();
    if (lmin>min) min = lmin;
  }
  return min;
}


void LEDChainArrangement::setPowerLimit(int aMilliWatts)
{
  // internal limit is in PWM units
  mPowerLimitMw = aMilliWatts;
  // make sure it gets applied immediately
  if (mRootView) mRootView->makeDirtyAndUpdate();
}


int LEDChainArrangement::getPowerLimit()
{
  // internal measurement is in PWM units
  return mPowerLimitMw;
}


int LEDChainArrangement::getNeededPower()
{
  // internal measurement is in PWM units
  return mRequestedLightPowerMw;
}


int LEDChainArrangement::getCurrentPower()
{
  // internal measurement is in PWM units
  return mActualLightPowerMw;
}



MLMicroSeconds LEDChainArrangement::updateDisplay()
{
  // current real time is the relevant time here (not the step start, which might be a while ago when step calc was expensive)
  MLMicroSeconds now = MainLoop::now();
  if (mRootView) {
    bool dirty = mRootView->isDirty();
    if (dirty || now>mLastUpdate+MAX_UPDATE_INTERVAL) {
      // needs update
      MLMicroSeconds earliestUpdate = mLastUpdate+mMinUpdateInterval;
      // can we start right now?
      if (now<earliestUpdate) {
        DBGFOCUSOLOG("\r- next display update (interval>=%lld µS) must wait at least %lld µS, mRootView.dirty=%d", mMinUpdateInterval, earliestUpdate-now, dirty);
        return earliestUpdate;
      }
      else {
        // update now
        mLastUpdate = now;
        uint32_t idlePowerMw = 0;
        uint32_t lightPowerMw = 0;
        uint32_t lightPowerPWM = 0;
        uint32_t lightPowerPWMWhite = 0;
        if (dirty) {
          uint8_t powerDim = 0; // undefined
          while (true) {
            idlePowerMw = 0;
            lightPowerMw = 0;
            // update LED chain content buffers from view hierarchy
            for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
              lightPowerPWM = 0; // accumulated PWM values per chain
              lightPowerPWMWhite = 0; // separate for white which has different wattage
              LEDChainFixture& l = *pos;
              bool hasWhite = l.ledChain->hasWhite();
              const LEDChainComm::LedChipDesc &chip = l.ledChain->ledChipDescriptor();
              for (int x=0; x<l.covers.dx; ++x) {
                for (int y=0; y<l.covers.dy; ++y) {
                  // get pixel from view
                  PixelColor pix = mRootView->colorAt({
                    l.covers.x+x,
                    l.covers.y+y
                  });
                  dimPixel(pix, pix.a);
                  #if DEBUG
                  //if (x==0 && y==0) pix={ 255,0,0,255 };
                  #endif
                  PixelColorComponent w = 0;
                  if (hasWhite) {
                    // transfer common white from RGB to white channel
                    double r = (double)pix.r/255;
                    double g = (double)pix.g/255;
                    double b = (double)pix.b/255;
                    int f = 255;
                    w = p44::transferToColor(l.ledChain->mLEDWhite, r, g, b)*f;
                    pix.r = r*f;
                    pix.g = g*f;
                    pix.b = b*f;
                  }
                  // transfer to power
                  uint8_t Pr, Pg, Pb, Pw;
                  if (powerDim) {
                    Pr = dimVal(pwmtable[pix.r], powerDim);
                    Pg = dimVal(pwmtable[pix.g], powerDim);
                    Pb = dimVal(pwmtable[pix.b], powerDim);
                    Pw = dimVal(pwmtable[w], powerDim);
                  }
                  else {
                    Pr = pwmtable[pix.r];
                    Pg = pwmtable[pix.g];
                    Pb = pwmtable[pix.b];
                    Pw = pwmtable[w];
                  }
                  // measure
                  // - every LED consumes the idle power
                  idlePowerMw += chip.idleChipMw;
                  // - sum up PWM values (multiply later, only once per chain)
                  if (chip.rgbCommonCurrent) {
                    // serially connected LEDs: max LEDs power determines total power
                    lightPowerPWM += Pr>Pg ? (Pb>Pr ? Pb : Pr) : (Pb>Pg ? Pb : Pg);
                  }
                  else {
                    // sum of RGB LEDs is total power
                    lightPowerPWM += Pr+Pg+Pb;
                  }
                  lightPowerPWMWhite += Pw;
                  // set pixel in chain
                  l.ledChain->setPowerXY(
                    l.offset.x+x,
                    l.offset.y+y,
                    Pr, Pg, Pb, Pw
                  );
                }
              }
              // end of one chain
              // - update actual power (according to chip type)
              lightPowerMw += lightPowerPWM*chip.rgbChannelMw/255 + lightPowerPWMWhite*chip.whiteChannelMw/255;
            }
            // update stats (including idle power)
            mActualLightPowerMw = lightPowerMw+idlePowerMw; // what we measured in this pass
            if (powerDim==0) {
              mRequestedLightPowerMw = mActualLightPowerMw; // what we measure without dim is the requested amount
            }
            // check if we need power limiting
            if (mPowerLimitMw && mActualLightPowerMw>mPowerLimitMw && powerDim==0) {
              powerDim = (uint32_t)255*(mPowerLimitMw-idlePowerMw)/lightPowerMw; // scale proportionally to PWM dependent part of consumption (idle power cannot be reduced)
              if (!mPowerLimited) {
                mPowerLimited = true;
                OLOG(LOG_INFO, "!!! LED power (%d mW active + %d mW idle) exceeds limit (%d mW) -> re-run dimmed to (%d%%)", lightPowerMw, idlePowerMw, mPowerLimitMw, powerDim*100/255);
              }
              if (powerDim!=0) continue; // run again with reduced power (but prevent endless loop in case reduction results in zero)
            }
            else if (powerDim) {
              DBGFOCUSOLOG("\r--- requested power is %d mW, reduced power is %d mW now (limit %d mW), dim=%d", mRequestedLightPowerMw, lightPowerMw, mPowerLimitMw, powerDim);
            }
            else {
              if (mPowerLimited) {
                mPowerLimited = false;
                OLOG(LOG_INFO, "!!! LED power (%d mW) back below limit (%d mW) -> no dimm-down active", lightPowerMw, mPowerLimitMw);
              }
            }
            break;
          }
          mRootView->updated();
        }
        // update hardware (refresh actual LEDs, cleans away possible glitches
        DBGFOCUSOLOG("\r######## calling show(), dirty=%d", dirty);
        for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
          pos->ledChain->show();
        }
        DBGFOCUSOLOG("\r######## show() called");
      }
    }
  }
  return Infinite; // no pending update
}


void LEDChainArrangement::startChains()
{
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    if (!pos->ledChain->mInitialized) {
      pos->ledChain->begin(mLedChains.size());
      pos->ledChain->clear();
      pos->ledChain->show();
    }
  }
}


void LEDChainArrangement::begin(bool aAutoStep)
{
  if (!mStarted) {
    mStarted = true;
    startChains();
    if (aAutoStep) {
      mAutoStepTicket.executeOnce(boost::bind(&LEDChainArrangement::autoStep, this, _1));
    }
  }
  else {
    OLOG(LOG_DEBUG, "LEDChainArrangement::begin() called while already started before");
  }
}


MLMicroSeconds LEDChainArrangement::step()
{
  MLMicroSeconds nextStep = Infinite;
  MLMicroSeconds stepNow = MainLoop::now();
  DBGFOCUSOLOG("\rSTEP at %011lld µS = lastStep+%09lld µS, lastupdate+%09lld µS", stepNow-MainLoop::currentMainLoop().startedAt(), stepNow-mLastStep, stepNow-mLastUpdate);
  if (mRootView) {
    // do view steps
    do {
      nextStep = mRootView->step(mLastUpdate+mMaxPriorityInterval, stepNow);
    } while (nextStep==0);
    // do update
    MLMicroSeconds nextDisp = updateDisplay();
    // now try to sync display updates with steps as much as possible
    // by avoiding starting an update of which we know in advance that it will likely not be complete before the next view step
    if (nextStep==Infinite) {
      // no next view step
      nextStep = stepNow+MAX_STEP_INTERVAL;
      if (nextDisp==Infinite) {
        // no need to update display
        if (nextStep<mLastUpdate+MAX_UPDATE_INTERVAL) {
          DBGFOCUSOLOG("\r- insert step to prevent view update stalling in currentstep+%lld µS", nextStep-stepNow);
        }
        else {
          nextStep = mLastUpdate+MAX_UPDATE_INTERVAL; // just schedule refresh
          DBGFOCUSOLOG("\r- insert step to ensure display refresh at currentstep+%lld µS", nextStep-stepNow);
        }
      }
      else {
        // specific time to update display
        DBGFOCUSOLOG("\r- no next view step but pending update step -> run it at currentstep+%lld µS", nextDisp-stepNow);
        nextStep = nextDisp;
      }
    }
    else if (nextDisp!=Infinite) {
      // we have both view and a disp step pending
      // - decide which one to run
      if (nextStep>nextDisp+mMinUpdateInterval) {
        // there's time to run another disp step before view step is required
        DBGFOCUSOLOG("\r- enough time to run a update step before next view step is due  -> run it at currentstep+%lld µS", nextDisp-stepNow);
        nextStep = nextDisp;
      }
      else {
        DBGFOCUSOLOG("\r- next view step due before pending update could finish -> SKIP update step and WAIT until currentstep+%lld µS", nextDisp-stepNow);
        if (mSlowDetected+MAX_SLOW_WARN_INTERVAL<stepNow) {
          OLOG(LOG_WARNING,"views change too quickly for minupdateinterval %lld µS -> display probably jumpy", mMinUpdateInterval);
        }
        mSlowDetected = stepNow;
      }
    }
    else {
      // only a next step is pending
      DBGFOCUSOLOG("\r- just view step pending -> run it at currentstep+%9lld µS", nextStep-stepNow);
    }
  }
  else {
    // no root view, just step again later
    nextStep = stepNow+MAX_STEP_INTERVAL;
  }
  // caller MUST call again at nextStep!
  if (nextStep<stepNow) {
    // apparently we are too slow
    if (mSlowDetected+MAX_SLOW_WARN_INTERVAL<stepNow) {
      OLOG(LOG_WARNING,"processing updates is too slow (step %lld µS late, minupdateinterval %lld µS) -> display probably jumpy or flickering", stepNow-nextStep, mMinUpdateInterval);
    }
    mSlowDetected = stepNow;
  }
  else if (mSlowDetected && stepNow>mSlowDetected+MAX_SLOW_WARN_INTERVAL) {
    // last warning is long enough since, re-enable warning
    OLOG(LOG_INFO, "processing seems fast enough again (smooth in last %lld µS)", MAX_SLOW_WARN_INTERVAL);
    mSlowDetected = Never;
  }
  mLastStep = stepNow;
  return nextStep;
}


void LEDChainArrangement::autoStep(MLTimer &aTimer)
{
  DBGFOCUSOLOG("\r\n\n######## autostep() called");
  MLMicroSeconds nextCall = step();
  MainLoop::currentMainLoop().retriggerTimer(aTimer, nextCall, 0, MainLoop::absolute);
}


void LEDChainArrangement::render()
{
  DBGFOCUSOLOG("\r######## render() called");
  MLMicroSeconds nextCall = step();
  mAutoStepTicket.executeOnceAt(boost::bind(&LEDChainArrangement::autoStep, this, _1), nextCall);
}


void LEDChainArrangement::externalUpdateRequest()
{
  DBGFOCUSOLOG("\r######## externalUpdateRequest()");
  if (mRootView) {
    if (mAutoStepTicket) {
      // interrupt autostepping timer
      DBGFOCUSOLOG("- externalUpdateRequest: interrupts scheduled autostop and inserts step right now");
      mAutoStepTicket.cancel();
      // start new with immediate step call
      mAutoStepTicket.executeOnce(boost::bind(&LEDChainArrangement::autoStep, this, _1));
    }
    else {
      // just step
      step();
    }
  }
}


void LEDChainArrangement::end()
{
  // note: can be repeatedly done, so do not check mStarted here
  mAutoStepTicket.cancel();
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    pos->ledChain->end();
  }
  mStarted = false;
}


// MARK: - script support

#if ENABLE_P44SCRIPT

using namespace P44Script;

// addledchain(ledchainconfigstring)
static const BuiltInArgDesc addledchain_args[] = { { text } };
static const size_t addledchain_numargs = sizeof(addledchain_args)/sizeof(BuiltInArgDesc);
static void addledchain_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  l->ledChainArrangement().addLEDChain(f->arg(0)->stringValue());
  l->ledChainArrangement().startChains(); // start chains that are not yet operating
  f->finish();
}


// removeledchains()
static void removeledchains_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  l->ledChainArrangement().removeAllChains();
  f->finish();
}


// neededledpower()
static void neededledpower_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  f->finish(new NumericValue(l->ledChainArrangement().getNeededPower()));
}


// currentledpower()
static void currentledpower_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  f->finish(new NumericValue(l->ledChainArrangement().getCurrentPower()));
}


// setmaxledpower()
static void setmaxledpower_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  l->ledChainArrangement().setPowerLimit(f->arg(0)->intValue());
  f->finish();
}


// setledrefresh(minUpdateInterval, [maxpriorityinterval])
static const BuiltInArgDesc setledrefresh_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t setledrefresh_numargs = sizeof(setledrefresh_args)/sizeof(BuiltInArgDesc);
static void setledrefresh_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  l->ledChainArrangement().setMinUpdateInterval(f->arg(0)->doubleValue()*Second);
  if (f->arg(1)->defined()) {
    l->ledChainArrangement().setMaxPriorityInterval(f->arg(1)->doubleValue()*Second);
  }
  f->finish();
}


// setrootview(view)
static const BuiltInArgDesc setrootview_args[] = { { object } };
static const size_t setrootview_numargs = sizeof(setrootview_args)/sizeof(BuiltInArgDesc);
static void setrootview_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  P44lrgViewObj* rootview = dynamic_cast<P44lrgViewObj*>(f->arg(0).get());
  if (!rootview) {
    f->finish(new ErrorValue(ScriptError::Invalid, "argument must be a view"));
    return;
  }
  l->ledChainArrangement().setRootView(rootview->view());
  f->finish();
}


// ledchaincover()
static void ledchaincover_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  PixelRect crect = l->ledChainArrangement().totalCover();
  JsonObjectPtr cover = JsonObject::newObj();
  cover->add("x", JsonObject::newInt32(crect.x));
  cover->add("y", JsonObject::newInt32(crect.y));
  cover->add("dx", JsonObject::newInt32(crect.dx));
  cover->add("dy", JsonObject::newInt32(crect.dy));
  f->finish(new JsonValue(cover));
}



static const BuiltInArgDesc setmaxledpower_args[] = { { numeric } };
static const size_t setmaxledpower_numargs = sizeof(setmaxledpower_args)/sizeof(BuiltInArgDesc);
static const BuiltinMemberDescriptor ledChainArrangementGlobals[] = {
  { "addledchain", executable, addledchain_numargs, addledchain_args, &addledchain_func },
  { "removeledchains", executable, 0, NULL, &removeledchains_func },
  { "ledchaincover", executable|object, 0, NULL, &ledchaincover_func },
  { "neededledpower", executable|numeric, 0, NULL, &neededledpower_func },
  { "currentledpower", executable|numeric, 0, NULL, &currentledpower_func },
  { "setmaxledpower", executable, setmaxledpower_numargs, setmaxledpower_args, &setmaxledpower_func },
  { "setrootview", executable, setrootview_numargs, setrootview_args, &setrootview_func },
  { "setledrefresh", executable, setledrefresh_numargs, setledrefresh_args, &setledrefresh_func },
  { NULL } // terminator
};


LEDChainLookup::LEDChainLookup(LEDChainArrangement& aLedChainArrangement) :
  inherited(ledChainArrangementGlobals),
  mLedChainArrangement(aLedChainArrangement)
{
}

#endif // ENABLE_P44SCRIPT



#endif // ENABLE_P44LRGRAPHICS

