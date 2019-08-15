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
  uint16_t aInactiveBetweenLeds
) :
  initialized(false)
{
  // type and device
  ledType = aLedType;
  deviceName = aDeviceName;
  numColorComponents = ledType==ledtype_sk6812 ? 4 : 3;
  inactiveStartLeds = aInactiveStartLeds;
  inactiveBetweenLeds = aInactiveBetweenLeds;
  // number of LEDs
  numLeds = aNumLeds;
  if (aLedsPerRow==0) {
    ledsPerRow = aNumLeds-aInactiveStartLeds; // single row
    numRows = 1;
  }
  else {
    ledsPerRow = aLedsPerRow; // set row size
    numRows = (numLeds-1-inactiveStartLeds)/(ledsPerRow+inactiveBetweenLeds)+1; // calculate number of (full or partial) rows
  }
  xReversed = aXReversed;
  yReversed = aYReversed;
  alternating = aAlternating;
  swapXY = aSwapXY;
  // prepare hardware related stuff
  #if ENABLE_RPIWS281X
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
  #else
  ledbuffer = NULL;
  ledFd = -1;
  #endif
  // make sure operation ends when mainloop terminates
  MainLoop::currentMainLoop().registerCleanupHandler(boost::bind(&LEDChainComm::end, this));
}

LEDChainComm::~LEDChainComm()
{
  // end the operation when object gets deleted
  end();
}


uint16_t LEDChainComm::getNumLeds()
{
  return numLeds-inactiveStartLeds-(numRows-1)*inactiveBetweenLeds;
}


uint16_t LEDChainComm::getSizeX()
{
  return swapXY ? numRows : ledsPerRow;
}


uint16_t LEDChainComm::getSizeY()
{
  return swapXY ? ledsPerRow : numRows;
}



bool LEDChainComm::begin()
{
  if (!initialized) {
    #if ENABLE_RPIWS281X
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
  return initialized;
}


void LEDChainComm::clear()
{
  if (!initialized) return;
  #if ENABLE_RPIWS281X
  for (uint16_t i=0; i<numLeds; i++) ledstring.channel[0].leds[i] = 0;
  #else
  memset(ledbuffer, 0, numColorComponents*numLeds);
  #endif
}


void LEDChainComm::end()
{
  if (initialized) {
    #if ENABLE_RPIWS281X
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
  initialized = false;
}


void LEDChainComm::show()
{
  if (!initialized) return;
  #if ENABLE_RPIWS281X
  ws2811_render(&ledstring);
  #else
  write(ledFd, ledbuffer, numLeds*numColorComponents);
  #endif
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
  FOCUSLOG("ledIndexFromXY: X=%d, Y=%d", aX, aY);
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
  FOCUSLOG("--> ledIndex=%d", ledindex);
  return ledindex+inactiveStartLeds;
}


void LEDChainComm::setColorXY(uint16_t aX, uint16_t aY, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  if (ledindex>=numLeds) return;
  #if ENABLE_RPIWS281X
  ws2811_led_t pixel =
    (pwmtable[aRed] << 16) |
    (pwmtable[aGreen] << 8) |
    (pwmtable[aBlue]);
  if (numColorComponents>3) {
    pixel |= (pwmtable[aWhite] << 24);
  }
  ledstring.channel[0].leds[ledindex] = pixel;
  #else
  ledbuffer[numColorComponents*ledindex] = pwmtable[aRed];
  ledbuffer[numColorComponents*ledindex+1] = pwmtable[aGreen];
  ledbuffer[numColorComponents*ledindex+2] = pwmtable[aBlue];
  if (numColorComponents>3) {
    ledbuffer[numColorComponents*ledindex+3] = pwmtable[aWhite];
  }
  #endif
}


void LEDChainComm::setColor(uint16_t aLedNumber, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  FOCUSLOG("setColor: ledNumber=%d - sizeX=%d, sizeY=%d", aLedNumber, getSizeX(), getSizeY());
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
  if (ledindex>=numLeds) return;
  #if ENABLE_RPIWS281X
  ws2811_led_t pixel = ledstring.channel[0].leds[ledindex];
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
  aRed = brightnesstable[ledbuffer[numColorComponents*ledindex]];
  aGreen = brightnesstable[ledbuffer[numColorComponents*ledindex+1]];
  aBlue = brightnesstable[ledbuffer[numColorComponents*ledindex+2]];
  if (numColorComponents>3) {
    aWhite = brightnesstable[ledbuffer[numColorComponents*ledindex+3]];
  }
  else {
    aWhite = 0;
  }
  #endif
}


#if ENABLE_P44LRGRAPHICS

// MARK: - LEDChainArrangement

LEDChainArrangement::LEDChainArrangement() :
  covers(zeroRect),
  hasWhiteLEDs(false),
  lastUpdate(Never)
{
}


LEDChainArrangement::~LEDChainArrangement()
{
  end();
}


void LEDChainArrangement::clear()
{
  for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
    pos->ledChain->clear();
    pos->ledChain->show();
  }
}


void LEDChainArrangement::setRootView(P44ViewPtr aRootView)
{
  rootView = aRootView;
}


void LEDChainArrangement::addLEDChain(const string &aChainSpec)
{
  LEDChainComm::LedType ledType = LEDChainComm::ledtype_ws281x; // assume WS2812/13
  string deviceName;
  int numleds = 200;
  bool xReversed = false;
  bool alternating = false;
  bool swapXY = false;
  bool yReversed = false;
  uint16_t inactiveStartLeds = 0;
  uint16_t inactiveBetweenLeds = 0;
  PixelRect covers;
  covers.x = 0;
  covers.y = 0;
  covers.dx = numleds;
  covers.dy = 1;
  PixelCoord offsets = { 0, 0 };
  // parse chain specification
  // Syntax: [chaintype:[leddevicename:]]numberOfLeds:[x:dx:y:dy:firstoffset:betweenoffset][XYSA]
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
            hasWhiteLEDs = true;
          }
          else if (part=="P9823") {
            ledType = LEDChainComm::ledtype_p9823;
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
        for (int i=0; i<part.size(); i++) {
          switch (part[i]) {
            case 'X': xReversed = true; break;
            case 'Y': yReversed = true; break;
            case 'S': swapXY = true; break;
            case 'A': alternating = true; break;
          }
        }
      }
    }
    else {
      // number
      int n = atoi(part.c_str());
      switch (nmbrcnt) {
        case 0: numleds = n; covers.dx = n; break;
        case 1: covers.x = n; break;
        case 2: covers.dx = n; break;
        case 3: covers.y = n; break;
        case 4: covers.dy = n; break;
        case 5: inactiveStartLeds = n; break;
        case 6: inactiveBetweenLeds = n; break;
        default: break;
      }
      nmbrcnt++;
    }
  }
  LEDChainCommPtr ledChain = LEDChainCommPtr(new LEDChainComm(
    ledType,
    deviceName,
    numleds,
    covers.dx, // ledsPerRow
    xReversed,
    alternating,
    swapXY,
    yReversed,
    inactiveStartLeds,
    inactiveBetweenLeds
  ));
  addLEDChain(ledChain, covers, offsets);
}


void LEDChainArrangement::addLEDChain(LEDChainCommPtr aLedChain, PixelRect aCover, PixelCoord aOffset)
{
  if (!aLedChain) return; // no chain
  ledChains.push_back(LEDChainFixture(aLedChain, aCover, aOffset));
  recalculateCover();
}


void LEDChainArrangement::recalculateCover()
{
  for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
    LEDChainFixture& l = *pos;
    if (l.covers.dx>0 && l.covers.dy>0) {
      if (covers.dx==0 || l.covers.x<covers.x) covers.x = l.covers.x;
      if (covers.dy==0 || l.covers.y<covers.y) covers.y = l.covers.y;
      if (covers.dx==0 || l.covers.x+l.covers.dx>covers.x+covers.dx) covers.dx = l.covers.x+l.covers.dx-covers.x;
      if (covers.dy==0 || l.covers.y+l.covers.dy>covers.y+covers.dy) covers.dy = l.covers.y+l.covers.dy-covers.y;
    }
  }
}


uint8_t LEDChainArrangement::getMinVisibleColorIntensity()
{
  uint8_t min = 1; // can't be lower than that, 0 would be off
  for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
    uint8_t lmin = pos->ledChain->getMinVisibleColorIntensity();
    if (lmin>min) min = lmin;
  }
  return min;
}



#define MAX_STEP_INTERVAL (1000*MilliSecond) // run a step at least in this interval
#define MAX_UPDATE_INTERVAL (500*MilliSecond) // send an update at least this often, even if no changes happen (LED refresh)
#define MIN_UPDATE_INTERVAL (15*MilliSecond) // do not send updates faster than this
#define MAX_PRIORITY_INTERVAL (50*MilliSecond) // allow synchronizing prioritized timing for this timespan after the last LED refresh


MLMicroSeconds LEDChainArrangement::step()
{
  MLMicroSeconds nextCall = Infinite;
  if (rootView) {
    do {
      nextCall = rootView->step(lastUpdate+MAX_PRIORITY_INTERVAL);
    } while (nextCall==0);
    MLMicroSeconds n = updateDisplay();
    if (nextCall<0 || (n>0 && n<nextCall)) {
      nextCall = n;
    }
  }
  return nextCall;
}


MLMicroSeconds LEDChainArrangement::updateDisplay()
{
  MLMicroSeconds now = MainLoop::now();
  if (rootView) {
    bool dirty = rootView->isDirty();
    if (dirty || now>lastUpdate+MAX_UPDATE_INTERVAL) {
      // needs update
      if (now<lastUpdate+MIN_UPDATE_INTERVAL) {
        // cannot update noew, but we should update soon
        return lastUpdate+MIN_UPDATE_INTERVAL;
      }
      else {
        // update now
        lastUpdate = now;
        if (dirty) {
          // update LED chain content buffers from view hierarchy
          for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
            LEDChainFixture& l = *pos;
            for (int x=0; x<l.covers.dx; ++x) {
              for (int y=0; y<l.covers.dy; ++y) {
                // get pixel from view
                PixelColor pix = rootView->colorAt({
                  l.covers.x+x,
                  l.covers.y+y
                });
                // set pixel in chain
                l.ledChain->setColorXY(
                  l.offset.x+x,
                  l.offset.y+y,
                  pix.r, pix.g, pix.b
                );
              }
            }
          }
          rootView->updated();
        }
        // update hardware (refresh actual LEDs, cleans away possible glitches
        for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
          pos->ledChain->show();
        }
      }
    }
  }
  // latest possible update
  return now+MAX_UPDATE_INTERVAL;
}


void LEDChainArrangement::begin(bool aAutoStep)
{
  for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
    pos->ledChain->begin();
    pos->ledChain->clear();
    pos->ledChain->show();
  }
  if (aAutoStep) {
    autoStepTicket.executeOnce(boost::bind(&LEDChainArrangement::autoStep, this, _1));
  }
}


void LEDChainArrangement::autoStep(MLTimer &aTimer)
{
  MLMicroSeconds nextCall = step();
  MLMicroSeconds now = MainLoop::now();
  if (nextCall<0 || nextCall-now>MAX_STEP_INTERVAL) {
    nextCall = now+MAX_STEP_INTERVAL;
  }
  MainLoop::currentMainLoop().retriggerTimer(aTimer, nextCall, 0, MainLoop::absolute);

}


void LEDChainArrangement::end()
{
  autoStepTicket.cancel();
  for(LedChainVector::iterator pos = ledChains.begin(); pos!=ledChains.end(); ++pos) {
    pos->ledChain->end();
  }
}



#endif // ENABLE_P44LRGRAPHICS

