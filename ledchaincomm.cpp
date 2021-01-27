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
  #define GPIO_PIN 18 // P1 Pin 12, GPIO 18 (PCM_CLK)
  #define GPIO_INVERT 0 // set to 1 if there is an inverting driver between GPIO 18 and the WS281x LEDs
  #define DMA 5 // don't change unless you know why
  #define MAX_BRIGHTNESS 255 // full brightness range
#endif



LEDChainComm::LEDChainComm(
  LedType aLedType,
  const string aDeviceName,
  uint16_t aNumLeds,
  uint16_t aLedsPerRow,
  bool aXReversed,
  bool aAlternating,
  bool aSwapXY,
  bool aYReversed,
  uint16_t aInactiveStartLeds,
  uint16_t aInactiveBetweenLeds,
  uint16_t aInactiveEndLeds
) :
  initialized(false)
  #ifdef ESP_PLATFORM
  ,gpioNo(18) // sensible default
  ,pixels(NULL)
  #elif ENABLE_RPIWS281X
  #else
  ,ledFd(-1)
  ,ledbuffer(NULL)
  #endif
{
  // type and device
  ledType = aLedType;
  deviceName = aDeviceName;
  numColorComponents = ledType==ledtype_sk6812 ? 4 : 3;
  inactiveStartLeds = aInactiveStartLeds;
  inactiveBetweenLeds = aInactiveBetweenLeds;
  inactiveEndLeds = aInactiveEndLeds;
  // number of LEDs
  numLeds = aNumLeds;
  if (aLedsPerRow==0) {
    ledsPerRow = aNumLeds-aInactiveStartLeds; // single row
    numRows = 1;
  }
  else {
    ledsPerRow = aLedsPerRow; // set row size
    numRows = (numLeds-1-inactiveStartLeds-inactiveEndLeds)/(ledsPerRow+inactiveBetweenLeds)+1; // calculate number of (full or partial) rows
  }
  xReversed = aXReversed;
  yReversed = aYReversed;
  alternating = aAlternating;
  swapXY = aSwapXY;
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
  chainDriver = aLedChainComm;
}


// MARK: - LEDChainComm physical LED chain driver

#ifdef ESP_PLATFORM

static bool gEsp32_ws281x_initialized = false;

#define ESP32_LEDCHAIN_MAX_RETRIES 3

#endif // CONFIG_P44UTILS_DIGITAL_LED_LIB



bool LEDChainComm::begin(size_t aHintAtTotalChains)
{
  if (!initialized) {
    if (chainDriver) {
      // another LED chain outputs to the hardware, make sure that one is up
      initialized = chainDriver->begin();
    }
    else {
      #ifdef ESP_PLATFORM
      if (deviceName.substr(0,4)=="gpio") {
        sscanf(deviceName.c_str()+4, "%d", &gpioNo);
      }
      // make sure library is initialized
      if (!gEsp32_ws281x_initialized) {
        FOCUSLOG("calling esp_ws281x_init for %d chains", aHintAtTotalChains);
        esp_ws281x_init(aHintAtTotalChains);
        gEsp32_ws281x_initialized = true;
      }
      // add this chain
      Esp_ws281x_LedType elt;
      switch (ledType) {
        case ledtype_ws2812 : elt = esp_ledtype_ws2812; break;
        case ledtype_sk6812 : elt = esp_ledtype_sk6812; break;
        case ledtype_p9823 : elt = esp_ledtype_p9823; break;
        case ledtype_ws2815_rgb : elt = esp_ledtype_ws2815_rgb; break;
        case ledtype_ws281x :
        default: elt = esp_ledtype_ws2813; break; // most common
      }
      espLedChain = esp_ws281x_newChain(elt, gpioNo, ESP32_LEDCHAIN_MAX_RETRIES);
      if (espLedChain) {
        // prepare buffer
        if (pixels) {
          delete[] pixels;
          pixels = NULL;
        }
        pixels = new Esp_ws281x_pixel[numLeds];
        initialized = true;
        clear();
      }
      else {
        LOG(LOG_ERR, "Error: esp_ws281x_newChain failed");
        return false; // cannot initialize LED chain
      }
      #elif ENABLE_RPIWS281X
      // prepare hardware related stuff
      memset(&ledstring, 0, sizeof(ledstring));
      // initialize the led string structure
      ledstring.freq = TARGET_FREQ;
      ledstring.dmanum = DMA;
      ledstring.device = NULL; // private data pointer for library
      // channel 0
      ledstring.channel[0].gpionum = GPIO_PIN;
      ledstring.channel[0].count = numLeds;
      ledstring.channel[0].invert = GPIO_INVERT;
      ledstring.channel[0].brightness = MAX_BRIGHTNESS;
      switch (ledType) {
        case ledtype_sk6812: ledstring.channel[0].strip_type = SK6812_STRIP_RGBW; break; // our SK6812 means RGBW
        case ledtype_p9823: ledstring.channel[0].strip_type = WS2811_STRIP_RGB; break; // some WS2811 might also use RGB order
        default: ledstring.channel[0].strip_type = WS2812_STRIP; break; // most common order, such as in WS2812,12B,13
      }
      ledstring.channel[0].strip_type = ledType==ledtype_sk6812 ? SK6812_STRIP_RGBW : WS2812_STRIP;
      ledstring.channel[0].leds = NULL; // will be allocated by the library
      // channel 1 - unused
      ledstring.channel[1].gpionum = 0;
      ledstring.channel[1].count = 0;
      ledstring.channel[1].invert = 0;
      ledstring.channel[1].brightness = MAX_BRIGHTNESS;
      ledstring.channel[1].leds = NULL; // will be allocated by the library
      // initialize library
      ws2811_return_t ret = ws2811_init(&ledstring);
      if (ret==WS2811_SUCCESS) {
        initialized = true;
      }
      else {
        LOG(LOG_ERR, "Error: ws2811_init failed: %s", ws2811_get_return_t_str(ret));
        initialized = false;
      }
      #else
      // Allocate led buffer
      if (ledbuffer) {
        delete[] ledbuffer;
        ledbuffer = NULL;
      }
      ledbuffer = new uint8_t[numColorComponents*numLeds];
      memset(ledbuffer, 0, numColorComponents*numLeds);
      ledFd = open(deviceName.c_str(), O_RDWR);
      if (ledFd>=0) {
        initialized = true;
      }
      else {
        LOG(LOG_ERR, "Error: Cannot open LED chain device '%s'", deviceName.c_str());
        initialized = false;
      }
      #endif
    }
  }
  return initialized;
}


void LEDChainComm::clear()
{
  if (!initialized) return;
  if (chainDriver) {
    // this is just a secondary mapping on a primary chain: clear only the actually mapped LEDs
    for (uint16_t led = inactiveStartLeds; led<numLeds-inactiveEndLeds; led++) {
      chainDriver->setColor(led, 0, 0, 0, 0);
    }
  }
  else {
    // this is the master driver, clear the entire buffer
    #ifdef ESP_PLATFORM
    for (uint16_t i=0; i<numLeds; i++) pixels[i].num = 0;
    #elif ENABLE_RPIWS281X
    for (uint16_t i=0; i<numLeds; i++) ledstring.channel[0].leds[i] = 0;
    #else
    memset(ledbuffer, 0, numColorComponents*numLeds);
    #endif
  }
}


void LEDChainComm::end()
{
  if (initialized) {
    if (!chainDriver) {
      #ifdef ESP_PLATFORM
      if (pixels) {
        delete[] pixels;
        pixels = NULL;
      }
      esp_ws281x_freeChain(espLedChain);
      espLedChain = NULL;
      #elif ENABLE_RPIWS281X
      // deinitialize library
      ws2811_fini(&ledstring);
      #else
      if (ledbuffer) {
        delete[] ledbuffer;
        ledbuffer = NULL;
      }
      if (ledFd>=0) {
        close(ledFd);
        ledFd = -1;
      }
      #endif
    }
  }
  initialized = false;
}


void LEDChainComm::show()
{
  if (!chainDriver) {
    // Note: no operation if this is only a secondary mapping - primary driver will update the hardware
    if (!initialized) return;
    #ifdef ESP_PLATFORM
    esp_ws281x_setColors(espLedChain, numLeds, pixels);
    #elif ENABLE_RPIWS281X
    ws2811_render(&ledstring);
    #else
    write(ledFd, ledbuffer, numLeds*numColorComponents);
    #endif
  }
}


void LEDChainComm::setColorAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  if (chainDriver) {
    // delegate actual output
    chainDriver->setColorAtLedIndex(aLedIndex, aRed, aGreen, aBlue, aWhite);
  }
  else {
    // local driver, store change in my own LED buffer
    if (aLedIndex>=numLeds) return;
    // get power (PWM values)
    uint8_t r = pwmtable[aRed];
    uint8_t g = pwmtable[aGreen];
    uint8_t b = pwmtable[aBlue];
    uint8_t w = pwmtable[aWhite];
    #ifdef ESP_PLATFORM
    if (!pixels) return;
    pixels[aLedIndex] = esp_ws281x_makeRGBVal(r, g, b, w);
    #elif ENABLE_RPIWS281X
    ws2811_led_t pixel =
      ((uint32_t)r << 16) |
      ((uint32_t)g << 8) |
      ((uint32_t)b);
    if (numColorComponents>3) {
      pixel |= ((uint32_t)w << 24);
    }
    ledstring.channel[0].leds[aLedIndex] = pixel;
    #else
    ledbuffer[numColorComponents*aLedIndex] = r;
    ledbuffer[numColorComponents*aLedIndex+1] = g;
    ledbuffer[numColorComponents*aLedIndex+2] = b;
    if (numColorComponents>3) {
      ledbuffer[numColorComponents*aLedIndex+3] = w;
    }
    #endif
  }
}


void LEDChainComm::getColorAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  if (chainDriver) {
    // delegate actual output
    chainDriver->getColorAtLedIndex(aLedIndex, aRed, aGreen, aBlue, aWhite);
  }
  else {
    if (aLedIndex>=numLeds) return;
    #ifdef ESP_PLATFORM
    if (!pixels) return;
    Esp_ws281x_pixel &pixel = pixels[aLedIndex];
    aRed = brightnesstable[pixel.r];
    aGreen = brightnesstable[pixel.g];
    aBlue = brightnesstable[pixel.b];
    aWhite = brightnesstable[pixel.w];
    #elif ENABLE_RPIWS281X
    ws2811_led_t pixel = ledstring.channel[0].leds[aLedIndex];
    aRed = brightnesstable[(pixel>>16) & 0xFF];
    aGreen = brightnesstable[(pixel>>8) & 0xFF];
    aBlue = brightnesstable[pixel & 0xFF];
    if (numColorComponents>3) {
      aWhite = brightnesstable[(pixel>>24) & 0xFF];
    }
    else {
      aWhite = 0;
    }
    #else
    aRed = brightnesstable[ledbuffer[numColorComponents*aLedIndex]];
    aGreen = brightnesstable[ledbuffer[numColorComponents*aLedIndex+1]];
    aBlue = brightnesstable[ledbuffer[numColorComponents*aLedIndex+2]];
    if (numColorComponents>3) {
      aWhite = brightnesstable[ledbuffer[numColorComponents*aLedIndex+3]];
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
  return numLeds-inactiveStartLeds-inactiveEndLeds-(numRows-1)*inactiveBetweenLeds;
}


uint16_t LEDChainComm::getSizeX()
{
  return swapXY ? numRows : ledsPerRow;
}


uint16_t LEDChainComm::getSizeY()
{
  return swapXY ? ledsPerRow : numRows;
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
  if (swapXY) { uint16_t tmp = aY; aY = aX; aX = tmp; }
  if (yReversed) { aY = numRows-1-aY; }
  uint16_t ledindex = aY*(ledsPerRow+inactiveBetweenLeds);
  bool reversed = xReversed;
  if (alternating) {
    if (aY & 0x1) reversed = !reversed;
  }
  if (reversed) {
    ledindex += (ledsPerRow-1-aX);
  }
  else {
    ledindex += aX;
  }
  //FOCUSLOG("--> ledIndex=%d", ledindex);
  return ledindex+inactiveStartLeds;
}


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



LEDChainArrangement::LEDChainArrangement() :
  mCovers(zeroRect),
  mMaxOutValue(255),
  mPowerLimit(0),
  mRequestedLightPower(0),
  mActualLightPower(0),
  mPowerLimited(false),
  mLastUpdate(Never),
  minUpdateInterval(DEFAULT_MIN_UPDATE_INTERVAL),
  maxPriorityInterval(DEFAULT_MAX_PRIORITY_INTERVAL)
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
    mRootView->setNeedUpdateCB(NULL, 0); // make sure previous rootview will not call back any more!
  }
  mRootView = aRootView;
  mRootView->setDefaultLabel("rootview");
  mRootView->setNeedUpdateCB(boost::bind(&LEDChainArrangement::externalUpdateRequest, this), minUpdateInterval);
}


#if ENABLE_P44LRGRAPHICS
#include "viewfactory.hpp"
#endif

void LEDChainArrangement::addLEDChain(LEDChainArrangementPtr &aLedChainArrangement, const string &aChainSpec)
{
  if (aChainSpec.empty()) return;
  if (!aLedChainArrangement) {
    aLedChainArrangement = LEDChainArrangementPtr(new LEDChainArrangement);
    #if ENABLE_P44SCRIPT && ENABLE_P44LRGRAPHICS && ENABLE_VIEWCONFIG
    // also install p44script lookup providing "lrg" global
    p44::P44Script::StandardScriptingDomain::sharedDomain().registerMemberLookup(
      new P44Script::P44lrgLookup(&(aLedChainArrangement->mRootView))
    );
    #endif // ENABLE_P44SCRIPT
  }
  // now add chain
  aLedChainArrangement->addLEDChain(aChainSpec);
}


#if ENABLE_APPLICATION_SUPPORT

void LEDChainArrangement::processCmdlineOptions()
{
  int v;
  if (CmdLineApp::sharedCmdLineApp()->getIntOption("ledchainmax", v)) {
    setMaxOutValue(v);
  }
  if (CmdLineApp::sharedCmdLineApp()->getIntOption("ledpowerlimit", v)) {
    setPowerLimit(v);
  }
  if (CmdLineApp::sharedCmdLineApp()->getIntOption("ledrefresh", v)) {
    minUpdateInterval = v*MilliSecond;
  }
}

#endif // ENABLE_APPLICATION_SUPPORT


void LEDChainArrangement::addLEDChain(const string &aChainSpec)
{
  LEDChainComm::LedType ledType = LEDChainComm::ledtype_ws281x; // assume WS2812/13
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
  // Syntax: [chaintype:[leddevicename:]]numberOfLeds:[x:dx:y:dy:firstoffset:betweenoffset][XYSA][W#whitecolor]
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
          // chain type
          if (part=="SK6812") {
            ledType = LEDChainComm::ledtype_sk6812;
          }
          else if (part=="P9823") {
            ledType = LEDChainComm::ledtype_p9823;
          }
          else if (part=="WS2815_RGB") {
            ledType = LEDChainComm::ledtype_ws2815_rgb;
          }
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
    LOG(LOG_WARNING, "Specified area needs %d more LEDs than actually are available", -remainingInactive);
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
  LOG(LOG_INFO,
    "Installed ledchain covering area: x=%d, dx=%d, y=%d, dy=%d on device '%s'. %d LEDs inactive at start, %d at end.",
    newCover.x, newCover.dx, newCover.y, newCover.dy, ledChain->getDeviceName().c_str(),
    inactiveStartLeds, remainingInactive
  );
  // check for being a secondary chain mapping for an already driven chain
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    LEDChainFixture& l = *pos;
    if (l.ledChain && l.ledChain->getDeviceName()==deviceName && l.ledChain->isHardwareDriver()) {
      // chain with same driver name already exists, install this chain as a secondary mapping only
      ledChain->setChainDriver(l.ledChain); // use found chain as actual output
      LOG(LOG_INFO, "- ledchain is a secondary mapping for device '%s'", l.ledChain->getDeviceName().c_str());
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
  LOG(LOG_INFO,
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


#define MILLIWATTS_PER_LED 95 // empirical measurement for WS2813B

void LEDChainArrangement::setPowerLimit(int aMilliWatts)
{
  // internal limit is in PWM units
  mPowerLimit = aMilliWatts*255/MILLIWATTS_PER_LED;
  // make sure it gets applied immediately
  if (mRootView) mRootView->makeDirtyAndUpdate();
}


int LEDChainArrangement::getPowerLimit()
{
  // internal measurement is in PWM units
  return mPowerLimit*MILLIWATTS_PER_LED/255;
}


int LEDChainArrangement::getNeededPower()
{
  // internal measurement is in PWM units
  return mRequestedLightPower*MILLIWATTS_PER_LED/255;
}


int LEDChainArrangement::getCurrentPower()
{
  // internal measurement is in PWM units
  return mActualLightPower*MILLIWATTS_PER_LED/255;
}



MLMicroSeconds LEDChainArrangement::updateDisplay()
{
  MLMicroSeconds now = MainLoop::now();
  if (mRootView) {
    bool dirty = mRootView->isDirty();
    if (dirty || now>mLastUpdate+MAX_UPDATE_INTERVAL) {
      // needs update
      if (now<mLastUpdate+minUpdateInterval) {
        // cannot update now, but return the time when we can update next time
        DBGFOCUSLOG("updateDisplay update postponed by %lld, mRootView.dirty=%d", lastUpdate+minUpdateInterval-now, dirty);
        return mLastUpdate+minUpdateInterval;
      }
      else {
        // update now
        mLastUpdate = now;
        uint32_t lightPower = 0;
        if (dirty) {
          uint8_t powerDim = 0; // undefined
          while (true) {
            lightPower = 0;
            // update LED chain content buffers from view hierarchy
            for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
              LEDChainFixture& l = *pos;
              bool hasWhite = l.ledChain->hasWhite();
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
                    // we can integrate powerdim while scaling up from 0..1 to 0..255
                    int f = powerDim>0 ? powerDim : 255;
                    w = p44::transferToColor(l.ledChain->mLEDWhite, r, g, b)*f;
                    pix.r = r*f;
                    pix.g = g*f;
                    pix.b = b*f;
                  }
                  else {
                    // just RGB pixel, dim down if power limit is active
                    if (powerDim) {
                      // limit
                      dimPixel(pix, powerDim);
                    }
                  }
                  // measure
                  lightPower += pwmtable[pix.r]+pwmtable[pix.g]+pwmtable[pix.b]+pwmtable[w];
                  // limit to max output value
                  if (mMaxOutValue<255) {
                    if (pix.r>mMaxOutValue) pix.r = mMaxOutValue;
                    if (pix.g>mMaxOutValue) pix.g = mMaxOutValue;
                    if (pix.b>mMaxOutValue) pix.b = mMaxOutValue;
                    if (w>mMaxOutValue) w = mMaxOutValue;
                  }
                  // set pixel in chain
                  l.ledChain->setColorXY(
                    l.offset.x+x,
                    l.offset.y+y,
                    pix.r, pix.g, pix.b, w
                  );
                }
              }
            }
            // update stats
            mActualLightPower = lightPower; // what we measured in this pass
            if (powerDim==0) {
              mRequestedLightPower = lightPower; // what we measure without dim is the requested amount
            }
            // check if we need power limiting
            if (mPowerLimit && lightPower>mPowerLimit && powerDim==0) {
              powerDim = brightnesstable[(uint32_t)255*mPowerLimit/lightPower];
              if (!mPowerLimited) {
                mPowerLimited = true;
                LOG(LOG_INFO, "!!! LED power (%d) exceeds limit (%d) -> re-run dimmed to (%d%%)", lightPower, mPowerLimit, powerDim*100/255);
              }
              if (powerDim!=0) continue; // run again with reduced power (but prevent endless loop in case reduction results in zero)
            }
            else if (powerDim) {
              DBGFOCUSLOG("--- requested power is %d, reduced power is %d now (limit %d), dim=%d", mRequestedLightPower, lightPower, powerLimit, powerDim);
            }
            else {
              if (mPowerLimited) {
                mPowerLimited = false;
                LOG(LOG_INFO, "!!! LED power (%d) back below limit (%d) -> no dimm-down active", lightPower, mPowerLimit);
              }
            }
            break;
          }
          mRootView->updated();
        }
        // update hardware (refresh actual LEDs, cleans away possible glitches
        DBGFOCUSLOG("######## calling show(), dirty=%d", dirty);
        for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
          pos->ledChain->show();
        }
        DBGFOCUSLOG("######## show() called");
      }
    }
  }
  // latest possible update
  return now+MAX_UPDATE_INTERVAL;
}


void LEDChainArrangement::startChains()
{
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    if (!pos->ledChain->initialized) {
      pos->ledChain->begin(mLedChains.size());
      pos->ledChain->clear();
      pos->ledChain->show();
    }
  }
}


void LEDChainArrangement::begin(bool aAutoStep)
{
  startChains();
  if (aAutoStep) {
    mAutoStepTicket.executeOnce(boost::bind(&LEDChainArrangement::autoStep, this, _1));
  }
}



MLMicroSeconds LEDChainArrangement::step()
{
  MLMicroSeconds nextStep = Infinite;
  if (mRootView) {
    do {
      nextStep = mRootView->step(mLastUpdate+maxPriorityInterval);
    } while (nextStep==0);
    MLMicroSeconds nextDisp = updateDisplay();
    if (nextStep<0 || (nextDisp>0 && nextDisp<nextStep)) {
      nextStep = nextDisp;
    }
  }
  MLMicroSeconds now = MainLoop::now();
  // now we have nextStep according to the view hierarchy's step needs and the display's updating needs
  // - insert extra steps to avoid stalling completeley in case something goes wrong
  if (nextStep<0 || nextStep-now>MAX_STEP_INTERVAL) {
    nextStep = now+MAX_STEP_INTERVAL;
  }
  // caller MUST call again at nextStep!
  return nextStep;
}




void LEDChainArrangement::autoStep(MLTimer &aTimer)
{
  DBGFOCUSLOG("######## autostep() called");
  MLMicroSeconds nextCall = step();
  MainLoop::currentMainLoop().retriggerTimer(aTimer, nextCall, 0, MainLoop::absolute);
}


void LEDChainArrangement::render()
{
  DBGFOCUSLOG("######## render() called");
  MLMicroSeconds nextCall = step();
  mAutoStepTicket.executeOnceAt(boost::bind(&LEDChainArrangement::autoStep, this, _1), nextCall);
}


void LEDChainArrangement::externalUpdateRequest()
{
  DBGFOCUSLOG("######## externalUpdateRequest()");
  if (mRootView) {
    if (mAutoStepTicket) {
      // interrupt autostepping timer
      mAutoStepTicket.cancel();
// FIXME: delete these lines, autoStep -> step already includes calling updateDisplay(), without loosing updateDisplay()'s return value!
//      // update display if dirty
//      updateDisplay();
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
  mAutoStepTicket.cancel();
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    pos->ledChain->end();
  }
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
  { "ledchaincover", executable|json|object, 0, NULL, &ledchaincover_func },
  { "neededledpower", executable|numeric, 0, NULL, &neededledpower_func },
  { "currentledpower", executable|numeric, 0, NULL, &currentledpower_func },
  { "setmaxledpower", executable, setmaxledpower_numargs, setmaxledpower_args, &setmaxledpower_func },
  { NULL } // terminator
};


LEDChainLookup::LEDChainLookup(LEDChainArrangement& aLedChainArrangement) :
  inherited(ledChainArrangementGlobals),
  mLedChainArrangement(aLedChainArrangement)
{
}

#endif // ENABLE_P44SCRIPT



#endif // ENABLE_P44LRGRAPHICS

