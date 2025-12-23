//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include <math.h>

#if ENABLE_LEDCHAIN_UART
  #include <fcntl.h>
  #include <termios.h>
  #ifdef __APPLE__
  #include <sys/ioctl.h>
  #include <IOKit/serial/ioss.h> // for IOSSIOSPEED
  #endif
#endif // ENABLE_LEDCHAIN_UART


using namespace p44;

// MARK: - LEDPowerConverter

#define LEDCHAIN_DEFAULT_EXP 4 // the default exponent for the power translation


void LEDPowerConverter::createExpTable(int aTableNo, double aExponent, LEDChannelPower aMinPower)
{
  mTables[aTableNo] = new LEDPowerTable;
  mTables[aTableNo]->mData[0] = 0; // always 0
  // find min power from the curve at brightness 1
  LEDChannelPower bri1pwr = round(PWMMAX*((exp((1*aExponent)/PIXELMAX)-1)/(exp(aExponent)-1)));
  int offs = aMinPower>0 ? aMinPower-bri1pwr : 0; // offset
  // now calculate
  for (int b=1; b<=PIXELMAX; b++) {
    int pwr = 0;
    if (aExponent!=0) pwr = offs+round((PWMMAX-offs)*((exp((b*aExponent)/PIXELMAX)-1)/(exp(aExponent)-1)));
    mTables[aTableNo]->mData[b] = pwr>0 ? pwr : 0;
  }
}


LEDPowerConverter::LEDPowerConverter(double aExponent, LEDChannelPower aMinPower)
{
  createExpTable(0, aExponent, aMinPower);
  // use same table for all channels
  mRedPowers = mTables[0]->mData;
  mGreenPowers = mTables[0]->mData;
  mBluePowers = mTables[0]->mData;
  mWhitePowers = mTables[0]->mData;
}


LEDPowerConverter::LEDPowerConverter(double aColorExponent, LEDChannelPower aMinRedPower, LEDChannelPower aMinGreenPower, LEDChannelPower aMinBluePower, LEDChannelPower aMinWhitePower)
{
  createExpTable(0, aColorExponent, aMinRedPower);
  createExpTable(1, aColorExponent, aMinGreenPower);
  createExpTable(2, aColorExponent, aMinBluePower);
  createExpTable(3, aColorExponent, aMinWhitePower);
  // use separate table for each channel
  mRedPowers = mTables[0]->mData;
  mGreenPowers = mTables[1]->mData;
  mBluePowers = mTables[2]->mData;
  mWhitePowers = mTables[3]->mData;
}


LEDPowerConverter::LEDPowerConverter(double aColorExponent, LEDChannelPower aMinColorPower, double aWhiteExponent, LEDChannelPower aMinWhitePower)
{
  // common table for RGB
  createExpTable(0, aColorExponent, aMinColorPower);
  mRedPowers = mTables[0]->mData;
  mGreenPowers = mTables[0]->mData;
  mBluePowers = mTables[0]->mData;
  // separate table for white
  createExpTable(1, aWhiteExponent, aMinWhitePower);
  mWhitePowers = mTables[1]->mData;
}


LEDPowerConverter::~LEDPowerConverter()
{
}


static LEDPowerConverterPtr gStandardPowerConverter;

LEDPowerConverter& LEDPowerConverter::standardPowerConverter()
{
  if (!gStandardPowerConverter) {
    gStandardPowerConverter = new LEDPowerConverter(LEDCHAIN_DEFAULT_EXP, 0);
  }
  return *gStandardPowerConverter;
}


void LEDPowerConverter::powersForComponents(
  PixelColorComponent aDimDown,
  PixelColorComponent aRed, PixelColorComponent aGreen, PixelColorComponent aBlue, PixelColorComponent aWhite,
  LEDChannelPower& aRedPwr, LEDChannelPower& aGreenPwr, LEDChannelPower& aBluePwr, LEDChannelPower& aWhitePwr
) const
{
  if (aDimDown) {
    uint32_t f = (uint16_t)aDimDown+1;
    aRedPwr = (f*mRedPowers[aRed]) >> 8;
    aGreenPwr = (f*mRedPowers[aGreen]) >> 8;
    aBluePwr = (f*mRedPowers[aBlue]) >> 8;
    aWhitePwr = (f*mRedPowers[aWhite]) >> 8;
  }
  else {
    aRedPwr = mRedPowers[aRed];
    aGreenPwr = mRedPowers[aGreen];
    aBluePwr = mRedPowers[aBlue];
    aWhitePwr = mRedPowers[aWhite];
  }
}


// MARK: - LedChainComm

#if ENABLE_RPIWS281X
  #define TARGET_FREQ WS2811_TARGET_FREQ // in Hz, default is 800kHz
  #define GPIO_DEFAULT_PIN 18 // P1 Pin 12, GPIO 18 (PCM_CLK)
  #define DMA 5 // don't change unless you know why
  #define MAX_BRIGHTNESS 255 // full brightness range
#endif // ENABLE_RPIWS281X


// Power consumption according to
// https://www.thesmarthomehookup.com/the-complete-guide-to-selecting-individually-addressable-led-strips/
static const LEDChainComm::LedChipDesc ledChipDescriptors[LEDChainComm::num_ledchips] = {
  // type     Idle  rgb  Wh  by  common slowTm
  { "none",      0,   0,  0,  1, false, false },
  { "WS2811",    8,  64,  0,  1, false, true  },
  { "WS2812",    4,  60,  0,  1, false, true  },
  { "WS2813",    4,  85,  0,  1, false, false },
  { "WS2815",   24, 120,  0,  1, true,  false },
  { "P9823",     8,  80,  0,  1, false, true  }, // no real data, rough assumption
  { "SK6812",    6,  50, 95,  1, false, true  },
  { "WS2816",    4,  85,  0,  2, false, false }, // no real data, assume same as WS2813
  { "WS2813_N",  4,  85,  0,  1, false, false } // old/Normandled WS2813 timing with higher T0H
};

#define LEDCHAIN_INTERFRAME_PAUSE_MIN (350*MicroSecond) // min interframe pause time that certainly works for all chips (many could do with less)

typedef struct {
  const char *name; ///< name of the LED layout
  int channels; ///< number of channels, 3 or 4
  uint8_t fetchIdx[4]; ///< fetch indices - at what relative index to fetch bytes from input into output stream
} LedLayoutDescriptor_t;


static const LedLayoutDescriptor_t ledLayoutDescriptors[LEDChainComm::num_ledlayouts] = {
  // none
  { .name = "none", .channels = 1, .fetchIdx = { 1 } },
  // RGB data order
  { .name = "RGB", .channels = 3, .fetchIdx = { 0, 1, 2 } },
  // GRB data order
  { .name = "GRB", .channels = 3, .fetchIdx = { 1, 0, 2 } },
  // RGBW data order
  { .name = "RGBW", .channels = 4, .fetchIdx = { 0, 1, 2, 3 } },
  // GRBW data order
  { .name = "GRBW", .channels = 4, .fetchIdx = { 1, 0, 2, 3 } },
  // RBG data order
  { .name = "RBG", .channels = 3, .fetchIdx = { 0, 2, 1 } },
  // GBR data order
  { .name = "GBR", .channels = 3, .fetchIdx = { 1, 2, 0 } },
  // BRG data order
  { .name = "BRG", .channels = 3, .fetchIdx = { 2, 0, 1 } },
  // BGR data order
  { .name = "BGR", .channels = 3, .fetchIdx = { 2, 1, 0 } },
  // RBGW data order
  { .name = "RBGW", .channels = 4, .fetchIdx = { 0, 2, 1, 3 } },
  // GBRW data order
  { .name = "GBRW", .channels = 4, .fetchIdx = { 1, 2, 0, 3 } },
  // BRGW data order
  { .name = "BRGW", .channels = 4, .fetchIdx = { 2, 0, 1, 3 } },
  // BGRW data order
  { .name = "BGRW", .channels = 4, .fetchIdx = { 2, 1, 0, 3 } },
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
  ,mGpioNo(18) // sensible default
  ,mPixels(NULL)
  #elif ENABLE_RPIWS281X
  #else
  ,mLedFd(-1)
  ,mRawBuffer(NULL)
  ,mLedBuffer(NULL)
  ,mRawBytes(0)
  #if ENABLE_LEDCHAIN_UART
  ,mUartOutput(false)
  ,mUartSenderMutex(PTHREAD_MUTEX_INITIALIZER)
  ,mUartSenderCond(PTHREAD_COND_INITIALIZER)
  ,mUartSenderShutdown(false)
  ,mUartSendflag(false)
  #endif // ENABLE_LEDCHAIN_UART
  #endif
{
  // set defaults
  mLedChip = ledchip_none; // none defined yet
  mLedLayout = ledlayout_none; // none defined yet
  mTMaxPassive_uS = 0;
  mMaxRetries = 0;
  mNumColorComponents = 3;
  mNumBytesPerComponent = 1;
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
      for (int i=0; i<num_ledchips; i++) { if (uequals(part, ledChipDescriptors[i].name)) { mLedChip = (LedChip)i; break; } };
    }
    if (nextPart(cP, part, '.')) {
      // layout
      for (int i=0; i<num_ledlayouts; i++) { if (uequals(part, ledLayoutDescriptors[i].name)) { mLedLayout = (LedLayout)i; break; } };
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
  #if ENABLE_LEDCHAIN_UART
  if (mDeviceName.find("ledchain")==string::npos) {
    mUartOutput = true;
    FOCUSLOG("output device is not dedicated ledchain driver, but assumed UART");
  }
  #endif // ENABLE_LEDCHAIN_UART
  mNumColorComponents = ledChipDescriptors[mLedChip].whiteChannelMw>0 ? 4 : 3;
  mNumBytesPerComponent = ledChipDescriptors[mLedChip].numBytesPerChannel;
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


void LEDChainComm::setPowerConverter(const LEDPowerConverterPtr aLedPowerConverter)
{
  mLEDPowerConverter = aLedPowerConverter;
}


const LEDPowerConverter& LEDChainComm::powerConverter()
{
  if (!mLEDPowerConverter) {
    if (mChainDriver) {
      mLEDPowerConverter = const_cast<LEDPowerConverter *>(&(mChainDriver->powerConverter())); // use the chain driver's converter
    }
    else {
      mLEDPowerConverter = &LEDPowerConverter::standardPowerConverter(); // use the standard converter
    }
  }
  return *mLEDPowerConverter.get();
}


const LEDChainComm::LedChipDesc &LEDChainComm::ledChipDescriptor() const
{
  return ledChipDescriptors[mLedChip];
}


#if ENABLE_LEDCHAIN_UART
// MARK: - UART based WS28xx generator

static int setupUart(int aUartFd, bool aSlowTiming)
{
  int ret = 0;

  #ifdef __APPLE__
  // macOS does not have non-standard baud rate codes, but separate speed setting API
  int baudRateCode = B9600; // dummy but standard
  speed_t speed = aSlowTiming ? 2500000 : 3000000; // actual speed we need
  #else
  int baudRateCode = aSlowTiming ? B2500000 : B3000000;
  #endif
  struct termios newtio;
  memset(&newtio, 0, sizeof(newtio));
  // - charsize, stopbits, parity, no modem control lines (local), reading enabled
  newtio.c_cflag =
    CS7 |
    0 | // !CSTOPB : no second stop bit
    0 | // !PARENB and !PARODD : no parity
    CLOCAL | // ignore status lines
    0; // !CREAD : do not enable receiver
  // - ignore parity errors (just to make sure)
  newtio.c_iflag = IGNPAR;
  // - no output control
  newtio.c_oflag = 0;
  // - no input control (non-canonical)
  newtio.c_lflag = 0;
  // - no inter-char time
  newtio.c_cc[VTIME]    = 0;   /* inter-character timer unused */
  // - receive every single char seperately
  newtio.c_cc[VMIN]     = 1;   /* blocking read until 1 chars received */
  // - set speed (as this ors into c_cflag, this must be after setting c_cflag initial value)
  cfsetspeed(&newtio, baudRateCode);
  tcflush(aUartFd, TCIFLUSH);
  // now apply new settings
  ret = tcsetattr(aUartFd, TCSANOW, &newtio);
  #ifdef __APPLE__
  // on macOS, we need to set the non-standard speed separately
  if (ret>=0) {
    ret = ioctl(aUartFd, IOSSIOSPEED, &speed);
  }
  #endif // __APPLE__
  return ret;
}


static void sendToUart(int aUartFd, uint8_t* aLedData, size_t aLedDataSize, const LedLayoutDescriptor_t& aLedLayout)
{
  // We can generate appropriate WS28xx timing using 7-N-1 @ 3Mhz
  // - UART are LSBit first
  // - WS28xx are MSBit first
  // - we need 1 byte output for 3 bits LED data = 8 byte output for 3 byte LED data
  // - UART idle is H, start bit is L, stop bit is H
  // - as we need idle=L, start=H, stop=L for WS, we need to invert the UART data, and send inverted data
  // UART   : | start | Bit0 | Bit1 | Bit2 | Bit3 | Bit4 | Bit5 | Bit6 | Stop |
  // WS28xx : |   1   | LED7 |   0  |   1  | LED6 |   0  |   1  | LED5 |   0  |
  // WS28xx : |   1   | LED4 |   0  |   1  | LED3 |   0  |   1  | LED2 |   0  |
  // WS28xx : |   1   | LED1 |   0  |   1  | LED0 |   0  |   1  | next |   0  |
  size_t uartDataSize = (aLedDataSize*8+2)/3;
  uint8_t* uartData = new uint8_t[uartDataSize];
  uint8_t* outP = uartData;
  int outBitMask = 0x01; // first LED data bit in UART output
  uint8_t uartByte = 0;
  size_t lidx = 0;
  size_t cidx = 0;
  while(lidx<aLedDataSize) {
    uint8_t ledByte = aLedData[lidx+aLedLayout.fetchIdx[cidx++]];
    if (cidx>=aLedLayout.channels) {
      cidx = 0;
      lidx += aLedLayout.channels;
    }
    uint8_t ledBitMask = 0x80;
    while (ledBitMask) {
      if (outBitMask>1) uartByte |= (outBitMask>>1); // 2nd or 3rd bit in this uart byte: enable the bit position (not idle) by setting the always 1 sync bit (T0H)
      if (ledByte & ledBitMask) uartByte |= outBitMask; // actual data bit, extending T0H to become T1H in case bit is set
      ledBitMask >>= 1; // next input bit
      outBitMask <<= 3; // next output bit position
      if ((outBitMask & 0x7F)==0) {
        // uart byte complete
        *(outP++) = ~uartByte; // send inverted
        uartByte = 0;
        outBitMask = 0x01;
      }
    }
  }
  // send
  write(aUartFd, uartData, uartDataSize);
  // done
  delete[] uartData;
}


static void* uartSenderThreadRoutine(void *aArg)
{
  LEDChainComm* ledchainP = static_cast<LEDChainComm*>(aArg);
  if (ledchainP) ledchainP->uartSenderThreadWorker();
  return nullptr;
}


void LEDChainComm::uartSenderThreadWorker()
{
  // make blocking (again), so we can send large blocks at once
  int flags;
  if ((flags = fcntl(mLedFd, F_GETFL, 0))==-1) flags = 0;
  fcntl(mLedFd, F_SETFL, flags & ~O_NONBLOCK);
  // enter worker loop
  while (true) {
    pthread_mutex_lock(&mUartSenderMutex);
    while (!mUartSendflag && !mUartSenderShutdown) {
      pthread_cond_wait(&mUartSenderCond, &mUartSenderMutex);
    }
    if (mUartSenderShutdown) {
      pthread_mutex_unlock(&mUartSenderMutex);
      break;
    }
    pthread_mutex_unlock(&mUartSenderMutex);
    // send blocking, so UART sends everything at max speed, no pauses
    // Note: non-blocking can send max 4095 bytes on Linux, then would need
    //   chunking and possibly pauses between bytes

    sendToUart(mLedFd, mRawBuffer, mRawBytes, ledLayoutDescriptors[mLedLayout]);
    // make sure we have sent everything
    tcdrain(mLedFd);
    // make sure we have enough idle time
    MainLoop::sleep(LEDCHAIN_INTERFRAME_PAUSE_MIN);
    // - signal done
    pthread_mutex_lock(&mUartSenderMutex);
    mUartSendflag = false;
    pthread_mutex_unlock(&mUartSenderMutex);
  }
}


void LEDChainComm::triggerUartSend()
{
  pthread_mutex_lock(&mUartSenderMutex);
  // if still busy, just skip
  if (!mUartSendflag) {
    mUartSendflag = true;
    pthread_cond_signal(&mUartSenderCond);
  }
  else {
    LOG(LOG_DEBUG, "UART LED data sending retriggered too early - dropping this update");
  }
  pthread_mutex_unlock(&mUartSenderMutex);
}



#endif // ENABLE_LEDCHAIN_UART


// MARK: - LEDChainComm physical LED chain driver

#ifdef ESP_PLATFORM

static bool gEsp32_ws281x_initialized = false;

#define ESP32_LEDCHAIN_MAX_RETRIES 3

#endif // ESP_PLATFORM



bool LEDChainComm::begin(size_t aHintAtTotalChains)
{
  if (!mInitialized) {
    if (mChainDriver) {
      // another LED chain outputs to the hardware, make sure that one is up
      mInitialized = mChainDriver->begin();
    }
    else {
      #ifdef ESP_PLATFORM
      #if PWMBITS!=8
      #error "16-bit LEDs not yet implemented"
      #endif
      if (mDeviceName.substr(0,4)=="gpio") {
        sscanf(mDeviceName.c_str()+4, "%d", &mGpioNo);
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
        case ledchip_ws2813n:
        default:
          elt = esp_ledtype_ws2813;
          break;
      }
      mEspLedChain = esp_ws281x_newChain(elt, mGpioNo, ESP32_LEDCHAIN_MAX_RETRIES);
      if (mEspLedChain) {
        // prepare buffer
        if (mPixels) {
          delete[] mPixels;
          mPixels = nullptr;
        }
        mPixels = new Esp_ws281x_pixel[mNumLeds];
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
        case ledchip_ws2813n:
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
        case ledchip_ws2816:
          // FIXME: workaround, use two RGB (non-swapped) 8-bit for one 16-bit LED
          mRPiWS281x.channel[0].count = mNumLeds*2;
          mRPiWS281x.channel[0].strip_type = WS2811_STRIP_RGB;
          break;
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
      if (mRawBuffer) {
        delete[] mRawBuffer;
        mRawBuffer = NULL;
        mLedBuffer = NULL;
        mRawBytes = 0;
      }
      if (
        mLedChip!=ledchip_none
        #if ENABLE_LEDCHAIN_UART
        && !mUartOutput
        #endif
      ) {
        // p44ledchain-style driver, need a header
        const int hdrsize = 5; // v6 header size
        mRawBytes = mNumColorComponents*mNumBytesPerComponent*mNumLeds+1+hdrsize;
        mRawBuffer = new uint8_t[mRawBytes];
        mLedBuffer = mRawBuffer+1+hdrsize; // led data starts here
        // prepare header for p44-ledchain v6 and later compatible drivers
        mRawBuffer[0] = hdrsize; // driver v6 header size
        mRawBuffer[1] = mLedLayout;
        mRawBuffer[2] = mLedChip;
        mRawBuffer[3] = (mTMaxPassive_uS>>8) & 0xFF;
        mRawBuffer[4] = (mTMaxPassive_uS) & 0xFF;
        mRawBuffer[5] = mMaxRetries;
      }
      else {
        // chip not known here: legacy driver w/o header or UART, just need the raw buffer
        mRawBytes = mNumColorComponents*mNumBytesPerComponent*mNumLeds;
        mRawBuffer = new uint8_t[mRawBytes];
        mLedBuffer = mRawBuffer;
      }
      memset(mLedBuffer, 0, mNumColorComponents*mNumBytesPerComponent*mNumLeds);
      mLedFd = open(mDeviceName.c_str(), O_RDWR|O_NOCTTY|O_NONBLOCK);
      if (mLedFd>=0) {
        mInitialized = true;
        #if ENABLE_LEDCHAIN_UART
        if (mUartOutput) {
          if (setupUart(mLedFd, ledChipDescriptor().slowTiming)<0) {
            LOG(LOG_ERR, "Error: Cannot set UART for WS28xx output on '%s'", mDeviceName.c_str());
            mInitialized = false;
          }
          else {
            // start a sender thread
            pthread_create(&mUartSenderThread, NULL, uartSenderThreadRoutine, this);
          }
        }
        #endif // ENABLE_LEDCHAIN_UART
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
    for (uint16_t i=0; i<mNumLeds; i++) mPixels[i].num = 0;
    #elif ENABLE_RPIWS281X
    // FIXME: workaround, use two 8-bit for one 16-bit LED
    for (uint16_t i=0; i<mNumLeds*mNumBytesPerComponent; i++) mRPiWS281x.channel[0].leds[i] = 0;
    #else
    memset(mLedBuffer, 0, mNumColorComponents*mNumBytesPerComponent*mNumLeds);
    #endif
  }
}


void LEDChainComm::end()
{
  if (mInitialized) {
    if (!mChainDriver) {
      #ifdef ESP_PLATFORM
      if (mPixels) {
        delete[] mPixels;
        mPixels = nullptr;
      }
      esp_ws281x_freeChain(mEspLedChain);
      mEspLedChain = nullptr;
      #elif ENABLE_RPIWS281X
      // deinitialize library
      ws2811_fini(&mRPiWS281x);
      #else
      if (mRawBuffer) {
        delete[] mRawBuffer;
        mRawBuffer = nullptr;
        mLedBuffer = nullptr;
        mRawBytes = 0;
      }
      if (mUartOutput) {
        pthread_mutex_lock(&mUartSenderMutex);
        mUartSenderShutdown = true;
        pthread_cond_signal(&mUartSenderCond);
        pthread_mutex_unlock(&mUartSenderMutex);
        pthread_join(mUartSenderThread, nullptr);
      }
      if (mLedFd>=0) {
        close(mLedFd);
        mLedFd = -1;
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
    esp_ws281x_setColors(mEspLedChain, mNumLeds, mPixels);
    #elif ENABLE_RPIWS281X
    ws2811_render(&mRPiWS281x);
    #else
    #if ENABLE_LEDCHAIN_UART
    if (mUartOutput) {
      triggerUartSend();
    }
    else
    #endif // ENABLE_LEDCHAIN_UART
    {
      write(mLedFd, mRawBuffer, mRawBytes); // just data with header
    }
    #endif
  }
}


#if LEDCHAIN_LEGACY_API && PWMBITS==8

void LEDChainComm::setColorAtLedIndex(uint16_t aLedIndex, uint8_t aRed, uint8_t aGreen, uint8_t aBlue, uint8_t aWhite)
{
  // get power (PWM values)
  uint8_t r = pwmtable[aRed];
  uint8_t g = pwmtable[aGreen];
  uint8_t b = pwmtable[aBlue];
  uint8_t w = pwmtable[aWhite];
  setPowerAtLedIndex(aLedIndex, r, g, b, w);
}


#if LEDCHAIN_READBACK
void LEDChainComm::getColorAtLedIndex(uint16_t aLedIndex, uint8_t &aRed, uint8_t &aGreen, uint8_t &aBlue, uint8_t &aWhite)
{
  uint8_t r,g,b,w;
  getPowerAtLedIndex(aLedIndex, r, g, b, w);
  aRed = brightnesstable[r];
  aGreen = brightnesstable[g];
  aBlue = brightnesstable[b];
  aWhite = brightnesstable[w];
}
#endif // LEDCHAIN_READBACK

#endif // LEDCHAIN_LEGACY_API && PWMBITS==8


#if PWMBITS==8

static uint8_t pwmTo8Bits(LEDChannelPower aPWM)
{
  return aPWM;
}

#else

uint8_t pwmTo8Bits(LEDChannelPower aPWM)
{
  if (aPWM>=0xFF80) return 0xFF;
  return ((aPWM+0x66)>>8); // 0x66 empirically determined, least rounding errors compared with real 8bit table
}

#endif


void LEDChainComm::setPowerAtLedIndex(uint16_t aLedIndex, LEDChannelPower aRed, LEDChannelPower aGreen, LEDChannelPower aBlue, LEDChannelPower aWhite)
{
  if (mChainDriver) {
    // delegate actual output
    mChainDriver->setPowerAtLedIndex(aLedIndex, aRed, aGreen, aBlue, aWhite);
  }
  else {
    // local driver, store change in my own LED buffer
    if (aLedIndex>=mNumLeds) return;
    #ifdef ESP_PLATFORM
    #if PWMBITS!=8
    #error "16-bit LEDs not yet implemented"
    #endif
    if (!mPixels) return;
    mPixels[aLedIndex] = esp_ws281x_makeRGBVal(aRed, aGreen, aBlue, aWhite);
    #elif ENABLE_RPIWS281X
    #if PWMBITS!=16
    #error "implementation is for 16-bit PWM only"
    #endif
    if (mNumBytesPerComponent>1) {
      // FIXME: workaround, only works with GRB WS2816 for now
      // spread over 2 leds
      aLedIndex<<1;
      // - G-MSB, G-LSB, R-MSB
      ws2811_led_t pixel =
      ((uint32_t)((uint8_t)(aGreen>>8)) << 16) |
        ((uint32_t)((uint8_t)(aGreen&0xFF)) << 8) |
        ((uint32_t)((uint8_t)(aRed>>8)));
      mRPiWS281x.channel[0].leds[aLedIndex++] = pixel;
      // - R-LSB, B-MSB, B-LSB
      pixel =
        ((uint32_t)((uint8_t)(aRed&0xFF)) << 16) |
        ((uint32_t)((uint8_t)(aBlue>>8)) << 8) |
        ((uint32_t)((uint8_t)(aBlue&0xFF)));
      mRPiWS281x.channel[0].leds[aLedIndex] = pixel;
    }
    else {
      // 8-bit LEDs
      ws2811_led_t pixel =
        ((uint32_t)pwmTo8Bits(aRed) << 16) |
        ((uint32_t)pwmTo8Bits(aGreen) << 8) |
        ((uint32_t)pwmTo8Bits(aBlue));
      if (mNumColorComponents>3) {
        pixel |= ((uint32_t)pwmTo8Bits(aWhite) << 24);
      }
      mRPiWS281x.channel[0].leds[aLedIndex] = pixel;
    }
    #else
    #if PWMBITS!=16
    #error "implementation is for 16-bit PWM only"
    #endif
    int byteindex = mNumColorComponents*aLedIndex;
    if (mNumBytesPerComponent>1) {
      // 16 bit
      byteindex = byteindex<<1; // double number of bytes
      mLedBuffer[byteindex++] = aRed>>8;
      mLedBuffer[byteindex++] = aRed & 0xFF;
      mLedBuffer[byteindex++] = aGreen>>8;
      mLedBuffer[byteindex++] = aGreen & 0xFF;
      mLedBuffer[byteindex++] = aBlue>>8;
      mLedBuffer[byteindex++] = aBlue & 0xFF;
      if (mNumColorComponents>3) {
        mLedBuffer[byteindex++] = aWhite>>8;
        mLedBuffer[byteindex++] = aWhite & 0xFF;
      }
    }
    else {
      // 8bit, send MSB only
      mLedBuffer[byteindex++] = pwmTo8Bits(aRed);
      mLedBuffer[byteindex++] = pwmTo8Bits(aGreen);
      mLedBuffer[byteindex++] = pwmTo8Bits(aBlue);
      if (mNumColorComponents>3) {
        mLedBuffer[byteindex++] = pwmTo8Bits(aWhite);
      }
    }
    #endif
  }
}

#if LEDCHAIN_READBACK
void LEDChainComm::getPowerAtLedIndex(uint16_t aLedIndex, LEDChannelPower &aRed, LEDChannelPower &aGreen, LEDChannelPower &aBlue, LEDChannelPower &aWhite)
{
  if (mChainDriver) {
    // delegate actual output
    mChainDriver->getPowerAtLedIndex(aLedIndex, aRed, aGreen, aBlue, aWhite);
  }
  else {
    if (aLedIndex>=mNumLeds) return;
    #ifdef ESP_PLATFORM
    #if PWMBITS!=8
    #error "16-bit LEDs not yet implemented"
    #endif
    if (!mPixels) return;
    Esp_ws281x_pixel &pixel = mPixels[aLedIndex];
    aRed = pixel.r;
    aGreen = pixel.g;
    aBlue = pixel.b;
    aWhite = pixel.w;
    #elif ENABLE_RPIWS281X
    #if PWMBITS!=16
    #error "implementation is for 16-bit PWM only"
    #endif
    if (mNumBytesPerComponent>1) {
      // FIXME: workaround, only works with GRB WS2816 for now
      // spread over 2 leds
      aLedIndex<<1;
      // - G-MSB, G-LSB, R-MSB
      ws2811_led_t pixel1 = mRPiWS281x.channel[0].leds[aLedIndex];
      // - R-LSB, B-MSB, B-LSB
      ws2811_led_t pixel2 = mRPiWS281x.channel[1].leds[aLedIndex];
      aGreen = ((pixel1>>8) & 0xFFFF);
      aRed = ((pixel1<<8) & 0xFF00) + ((pixel2>>16) & 0xFF);
      aBlue = (pixel2 & 0xFFFF);
    }
    else {
      // 8-bit LED
      ws2811_led_t pixel = mRPiWS281x.channel[0].leds[aLedIndex];
      aRed = pwmFrom8Bits((pixel>>16) & 0xFF);
      aGreen = pwmFrom8Bits((pixel>>8) & 0xFF);
      aBlue = pwmFrom8Bits(pixel & 0xFF);
      if (mNumColorComponents>3) {
        aWhite = pwmFrom8Bits((pixel>>24) & 0xFF);
      }
      else {
        aWhite = 0;
      }
    }
    #else
    #if PWMBITS!=16
    #error "implementation is for 16-bit PWM only"
    #endif
    int byteindex = mNumColorComponents*aLedIndex;
    if (mNumBytesPerComponent>1) {
      // 16-bit
      byteindex = byteindex<<1;
      aRed = ((LEDChannelPower)ledBuffer[byteindex++]<<8);
      aRed |= ledBuffer[byteindex++];
      aGreen = ((LEDChannelPower)ledBuffer[byteindex++]<<8);
      aGreen |= ledBuffer[byteindex++];
      aBlue = ((LEDChannelPower)ledBuffer[byteindex++]<<8);
      aBlue |= ledBuffer[byteindex++];
      if (mNumColorComponents>3) {
        aWhite = ((LEDChannelPower)ledBuffer[byteindex++]<<8);
        aWhite |= ledBuffer[byteindex++];
      }
    }
    else {
      // 8-bit
      aRed = pwmFrom8Bits((LEDChannelPower)ledBuffer[byteindex++]);
      aGreen = pwmFrom8Bits((LEDChannelPower)ledBuffer[byteindex++]);
      aBlue = pwmFrom8Bits((LEDChannelPower)ledBuffer[byteindex++]);
      if (mNumColorComponents>3) {
        aWhite = pwmFrom8Bits((LEDChannelPower)ledBuffer[byteindex++]);
      }
      else {
        aWhite = 0;
      }
    }
    #endif
  }
}
#endif // LEDCHAIN_READBACK


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


PixelColorComponent LEDChainComm::getMinVisibleColorIntensity()
{
  // return highest brightness that still produces lowest non-zero output.
  // TODO: maybe find more accurate way to detect lowest visible brightness
  return 1;
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


#if LEDCHAIN_LEGACY_API && PWMBITS==8

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


#if LEDCHAIN_READBACK

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

#endif // LEDCHAIN_READBACK

#endif // LEDCHAIN_LEGACY_API && PWMBITS==8


void LEDChainComm::setPowerXY(uint16_t aX, uint16_t aY, LEDChannelPower aRed, LEDChannelPower aGreen, LEDChannelPower aBlue, LEDChannelPower aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  setPowerAtLedIndex(ledindex, aRed, aGreen, aBlue, aWhite);
}


void LEDChainComm::setPower(uint16_t aLedNumber, LEDChannelPower aRed, LEDChannelPower aGreen, LEDChannelPower aBlue, LEDChannelPower aWhite)
{
  int y = aLedNumber / getSizeX();
  int x = aLedNumber % getSizeX();
  setPowerXY(x, y, aRed, aGreen, aBlue, aWhite);
}


#if LEDCHAIN_READBACK
void LEDChainComm::getPowerXY(uint16_t aX, uint16_t aY, LEDChannelPower &aRed, LEDChannelPower &aGreen, LEDChannelPower &aBlue, LEDChannelPower &aWhite)
{
  uint16_t ledindex = ledIndexFromXY(aX,aY);
  getPowerAtLedIndex(ledindex, aRed, aGreen, aBlue, aWhite);
}
#endif


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
#define DEFAULT_BUFFER_FRAMES 2 // calculate this many (minimally timed) frames in advance
#define DEFAULT_MAX_PRIORITY_INTERVAL (3*DEFAULT_MIN_UPDATE_INTERVAL) // allow synchronizing prioritized timing for this timespan after the last LED refresh
#define MAX_SLOW_WARN_INTERVAL (10*Second) // how often (max) the timing violation detection will be logged
#define DEFAULT_RENDER_WITH_APPLY false // if true, rendering is considered constant time (not causing jitter) and thus done with apply (not pre-calculated)

LEDChainArrangement::LEDChainArrangement() :
  mStarted(false),
  mCovers(zeroRect),
  mPowerLimitMw(0),
  mRequestedLightPowerMw(0),
  mActualLightPowerMw(0),
  mMainDim(255), // 100% by default
  mPowerLimited(false),
  mCurrentStepShowTime(Infinite),
  mNextStepShowTime(Infinite),
  mEarliestNextDispApply(Infinite),
  mRenderPendingFor(Infinite),
  mDispApplyPendingFor(Infinite),
  mMinUpdateInterval(DEFAULT_MIN_UPDATE_INTERVAL),
  mBufferTime(DEFAULT_BUFFER_FRAMES*DEFAULT_MIN_UPDATE_INTERVAL),
  mRenderWithApply(DEFAULT_RENDER_WITH_APPLY),
  mMaxPriorityInterval(DEFAULT_MAX_PRIORITY_INTERVAL),
  mSlowDetected(Infinite)
{
  #if ENABLE_P44SCRIPT && ENABLE_P44LRGRAPHICS && ENABLE_VIEWCONFIG
  // install p44script lookup providing "ledchain" global
  // Note: assuming only ONE ledchain arrangement per application.
  // TODO: Nothing currently prevents instantiating multiple, but makes no sense
  p44::P44Script::StandardScriptingDomain::sharedDomain().registerMemberLookup(
    new P44Script::LEDChainLookup(*this)
  );
  #endif // ENABLE_P44SCRIPT && ENABLE_P44LRGRAPHICS && ENABLE_VIEWCONFIG
  #if LED_UPDATE_STATS
  resetStats();
  #endif
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
  mBufferTime = DEFAULT_BUFFER_FRAMES*aMinUpdateInterval;
  if (mRootView) mRootView->setMinUpdateInterval(mMinUpdateInterval);
}


void LEDChainArrangement::setBufferTime(MLMicroSeconds aBufferTime)
{
  mBufferTime = aBufferTime;
}


void LEDChainArrangement::setRenderWithApply(bool aRenderWithApply)
{
  mRenderWithApply = aRenderWithApply;
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
  LEDPowerConverterPtr powerConverter; // possibly use a custom converter
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
  // Syntax: [ledstype:[leddevicename:]]numberOfLeds:[x:dx:y:dy:firstoffset:betweenoffset][XYSA][W#whitecolor][;Cparam[,param…]]
  // where:
  // - ledstype is either a single word for old-style drivers (p44-ledchain before v6) or of the form
  //   <chip>.<layout>[.<TMaxPassive_uS>] for drivers that allow controlling type directly (p44-ledchain from v6 onwards)
  //   Usually supported chips are: WS2811, WS2812, WS2813, WS2815, SK6812, P9823
  //   Uusually supported layouts are: RGB, GRB, RGBW, GRBW
  // - Curve spec is either
  //   - 2 params: exponent,minout
  //   - 4 params: colorexponent,colorminout,whiteexponent,whiteminout
  //   - 5 params: exponent,redminout,greenminout,blueminout,whiteminout
  //   Where minout==0 means using curve as-is, minout>0 fit curve output for brightness 1..PIXELMAX to minout..PWMMAX

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
              // W#whilecol
              ledWhite = webColorToPixel(part.substr(i+1));
              i = part.size(); // W#whitecol ends the part (more options could follow after another colon)
              break;
            case 'C': {
              // Gamma curve:
              // Cexp,min
              // Ccolorexp,colormin,whiteexp,whitemin
              // Cexp,redmin,greenmin,bluemin,whitemin
              double p1 = 0, p2 = 0, p3 = 0, p4 = 0, p5 = 0;
              int n = sscanf(part.c_str()+i+1, "%lf,%lf,%lf,%lf,%lf", &p1, &p2, &p3, &p4, &p5);
              if (n==5) powerConverter = new LEDPowerConverter(p1, p2, p3, p4, p5); // common exponent, 4 separate minout
              else if (n==4) powerConverter = new LEDPowerConverter(p1, p2, p3, p4); // separate exponent/minout for RGB and white
              else if (n==2) powerConverter = new LEDPowerConverter(p1, p2); // exponent and minout for all channels
              ledWhite = webColorToPixel(part.substr(i+1));
              i = part.size(); // Cxxx ends the part (more options could follow after another colon)
              break;
            }
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
  // set white color
  ledChain->mLEDWhite[0] = (double)ledWhite.r/255;
  ledChain->mLEDWhite[1] = (double)ledWhite.g/255;
  ledChain->mLEDWhite[2] = (double)ledWhite.b/255;
  // set power converter
  if (powerConverter) ledChain->setPowerConverter(powerConverter);
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


double LEDChainArrangement::getMainDimFactor()
{
  return (double)mMainDim/255.0;
}


void LEDChainArrangement::setMainDimFactor(double aMainDimFactor)
{
  mMainDim = aMainDimFactor*255.0;
  // make sure it gets applied immediately
  if (mRootView) mRootView->makeDirtyAndUpdate();
}



void LEDChainArrangement::renderViewState()
{
  if (!mRootView) return;
  uint8_t powerDim = 0; // undefined
  uint32_t idlePowerMw = 0;
  uint32_t lightPowerMw = 0;
  uint32_t lightPowerPWM = 0;
  uint32_t lightPowerPWMWhite = 0;
  while (true) {
    idlePowerMw = 0;
    lightPowerMw = 0;
    // update LED chain content buffers from view hierarchy
    for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
      lightPowerPWM = 0; // accumulated PWM values per chain
      lightPowerPWMWhite = 0; // separate for white which has different wattage
      LEDChainFixture& l = *pos;
      const LEDPowerConverter& conv = l.ledChain->powerConverter(); // get the power converter
      bool hasWhite = l.ledChain->hasWhite();
      const LEDChainComm::LedChipDesc &chip = l.ledChain->ledChipDescriptor();
      for (int x=0; x<l.covers.dx; ++x) {
        for (int y=0; y<l.covers.dy; ++y) {
          // get pixel from view
          PixelColor pix = mRootView->colorAt({
            l.covers.x+x,
            l.covers.y+y
          });
          dimPixel(pix, scaleVal(pix.a, mMainDim)); // possibly scaled by mMainDim
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
          // transfer to output power
          LEDChannelPower Pr, Pg, Pb, Pw;
          conv.powersForComponents(powerDim, pix.r, pix.g, pix.b, w, Pr, Pg, Pb, Pw);
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
      lightPowerMw += (uint64_t)lightPowerPWM*chip.rgbChannelMw/PWMMAX + (uint64_t)lightPowerPWMWhite*chip.whiteChannelMw/PWMMAX;
    }
    // update stats (including idle power)
    mActualLightPowerMw = lightPowerMw+idlePowerMw; // what we measured in this pass
    if (powerDim==0) {
      mRequestedLightPowerMw = mActualLightPowerMw; // what we measure without dim is the requested amount
    }
    // check if we need power limiting
    // Note: too low mPowerLimitMw might get us here with lightPowerMw==0 and still too much (idle) power!
    if (mPowerLimitMw && mActualLightPowerMw>mPowerLimitMw && powerDim==0 && lightPowerMw>0) {
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


void LEDChainArrangement::applyDisplayUpdate()
{
  if (!mStarted) return;
  // update hardware (refresh actual LEDs, cleans away possible glitches
  DBGFOCUSOLOG("\r######## calling show(), dirty=%d", dirty);
  for(LedChainVector::iterator pos = mLedChains.begin(); pos!=mLedChains.end(); ++pos) {
    pos->ledChain->show();
  }
  DBGFOCUSOLOG("\r######## show() called");
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


#if LED_UPDATE_STATS

void LEDChainArrangement::resetStats()
{
  mStatsBase = MainLoop::now();
  mNumSteps = 0;
  mNumViewSteps = 0;
  mNumRenders = 0;
  mNumApplys = 0;
  mMinStepTime = 999999999;
  mMaxStepTime = -999999999;
  mMinRenderTime = 999999999;
  mMaxRenderTime = -999999999;
  mMinApplyDelay = 999999999;
  mMaxApplyDelay = -999999999;
  mNumBufferTimesInserted = 0;
  #if LED_UPDATE_STATS>1
  mStepLogIdx = 0;
  mLogged = 0;
  #endif
}


void LEDChainArrangement::showStats()
{
  LOG(LOG_NOTICE, "LEDChainArrangement Statistics:"
    "\n- mNumSteps                  = %12ld"
    "\n- mNumViewSteps              = %12ld"
    "\n- mNumRenders                = %12ld"
    "\n- mNumApplys                 = %12ld"
    "\n- mMinStepTime               = %12lld µS"
    "\n- mMaxStepTime               = %12lld µS"
    "\n- mMinRenderTime             = %12lld µS"
    "\n- mMaxRenderTime             = %12lld µS"
    "\n- mMinApplyDelay             = %12lld µS"
    "\n- mMaxApplyDelay             = %12lld µS"
    "\n- mNumBufferTimesInserted    = %12ld times",
    mNumSteps,
    mNumViewSteps,
    mNumRenders,
    mNumApplys,
    mMinStepTime,
    mMaxStepTime,
    mMinRenderTime,
    mMaxRenderTime,
    mMinApplyDelay,
    mMaxApplyDelay,
    mNumBufferTimesInserted
  );
  #if LED_UPDATE_STATS>1
  size_t numlogs = mLogged>cStepLogSize ? cStepLogSize : mLogged;
  ssize_t i = mStepLogIdx-numlogs;
  ssize_t i2 = i;
  if (i<0) i += cStepLogSize;
  if (LOGENABLED(LOG_INFO)) {
    while (numlogs>0) {
      LOG(LOG_INFO,
        "beg=%12lld (%+12lld), "
        "loop=%2ld, "
        "cShow=%12lld, "
        "nShow=%12lld, "
        "rendP=%12lld, "
        "dispP=%12lld (%+12lld), "
        "disp=%12lld (%+9lld late), "
        "sched=%12lld, "
        "end=%12lld",
        mStepLog[i].entered,
        mStepLog[i].entered-mStepLog[i2].entered,
        mStepLog[i].looped,
        mStepLog[i].currentStepShowTime,
        mStepLog[i].nextStepShowTime,
        mStepLog[i].renderPendingFor,
        mStepLog[i].dispApplyPendingFor, mStepLog[i].dispApplyPendingFor-mStepLog[i2].dispApplyPendingFor,
        mStepLog[i].dispApplied, mStepLog[i].dispApplied-mStepLog[i].dispApplyPendingFor,
        mStepLog[i].schedulenext,
        mStepLog[i].left
      );
      numlogs--;
      i2 = i;
      i++;
      if (i>=cStepLogSize) i = 0;
    }
  }
  else {
    LOG(LOG_WARNING, "use loglevel>=6 to see recorded step log with %d entries", cStepLogSize);
  }
  #endif // LED_UPDATE_STATS>1
  resetStats();
}
#else

void showStats() {}; // NOP

#endif // LED_UPDATE_STATS



MLMicroSeconds LEDChainArrangement::step()
{
  MLMicroSeconds scheduleNextFor = Infinite;
  // this is supposed to be the most precise point in time we do have
  MLMicroSeconds realNow = MainLoop::now();
  #if LED_UPDATE_STATS
  mNumSteps++;
  #if LED_UPDATE_STATS>1
  mStepLog[mStepLogIdx].entered = realNow;
  mStepLog[mStepLogIdx].looped = 0;
  mStepLog[mStepLogIdx].currentStepShowTime = mCurrentStepShowTime;
  mStepLog[mStepLogIdx].nextStepShowTime = mNextStepShowTime;
  mStepLog[mStepLogIdx].renderPendingFor = mRenderPendingFor;
  mStepLog[mStepLogIdx].dispApplyPendingFor = mDispApplyPendingFor;
  mStepLog[mStepLogIdx].dispApplied = Infinite;
  #endif // LED_UPDATE_STATS>1
  #endif // LED_UPDATE_STATS
  // prevent getting caught in endless loop, but also allow optimizing immediate apply&render
  for(int n=0; n<4; n++) {
    #if LED_UPDATE_STATS>1
    mStepLog[mStepLogIdx].looped++;
    #endif
    // first apply pending and due display update
    if (DEFINED_TIME(mDispApplyPendingFor)) {
      // there is an apply pending. Is it due?
      if (realNow>=mDispApplyPendingFor) {
        // yes, apply it
        applyDisplayUpdate();
        #if LED_UPDATE_STATS
        #if LED_UPDATE_STATS>1
        mStepLog[mStepLogIdx].dispApplied = realNow;
        #endif
        mNumApplys++;
        MLMicroSeconds applyDelay = realNow-mDispApplyPendingFor;
        if (applyDelay<mMinApplyDelay) mMinApplyDelay = applyDelay;
        if (applyDelay>mMaxApplyDelay) mMaxApplyDelay = applyDelay;
        #endif // LED_UPDATE_STATS
        mDispApplyPendingFor = Infinite;
        mEarliestNextDispApply = realNow+mMinUpdateInterval; // earliest next update possible
        realNow = MainLoop::now(); // but update it for stats
      }
      else {
        // no, apply pending but not yet due.
        // Is there state to capture from the views (=render) before we can step further?
        if (!mRenderWithApply || DEFINED_TIME(mRenderPendingFor)) {
          // yes, we must await display apply first
          scheduleNextFor = mDispApplyPendingFor;
          break;
        }
      }
    }
    // we get here only when we may run rendering if it is pending, possibly/usually ahead of time
    // (checks above prevent getting here if we still waiting to flush the previous rendered data)
    if (DEFINED_TIME(mRenderPendingFor)) {
      if (!mRenderWithApply || realNow>=mRenderPendingFor) {
        MLMicroSeconds renderTime = realNow;
        renderViewState();
        realNow = MainLoop::now();
        renderTime = realNow-renderTime; // time spent in rendering
        #if LED_UPDATE_STATS
        mNumRenders++;
        if (renderTime<mMinRenderTime) mMinRenderTime = renderTime;
        if (renderTime>mMaxRenderTime) mMaxRenderTime = renderTime;
        #endif
        // now applying this gets pending
        mDispApplyPendingFor = mRenderPendingFor;
        mRenderPendingFor = Infinite; // done rendering
        // make sure we do not apply too soon
        if (mDispApplyPendingFor<mEarliestNextDispApply) mDispApplyPendingFor = mEarliestNextDispApply;
        // Optimization: apply right now without any delay if we have reached the time for apply by now
        if (realNow>=mDispApplyPendingFor) continue;
      }
      else {
        // late renderWithApply pending but not yet due
        // schedule due time in case no further calculation step schedules something
        scheduleNextFor = mRenderPendingFor;
      }
    }
    // we get here only if we can run further display steps, that is previous state
    // already rendered (or mRenderWithApply demands late rendering).
    mCurrentStepShowTime = mNextStepShowTime; // we can advance to the next step
    // calculate next step possibly and hopefully ahead of time
    // Are we late?
    if (realNow>mCurrentStepShowTime) {
      // we are certainly late here as we should APPLY the result of this step now, and haven't
      // even started calculating it.
      // Note: even if we get here before the show time, we still MIGHT be late due to calculation
      // needing more time than left until showtime, but not certainly. Only correct for certain lateness!
      // We are definitely late -> push calculation time into the future
      mCurrentStepShowTime = realNow+mBufferTime;
      #if LED_UPDATE_STATS
      mNumBufferTimesInserted++;
      #endif
    }
    mNextStepShowTime = Infinite; // none known yet
    // Run a step
    if (mRootView) {
      // do a real view hierarchy step
      MLMicroSeconds stepTime = realNow;
      mNextStepShowTime = mRootView->step(mCurrentStepShowTime, mCurrentStepShowTime+mMaxPriorityInterval, realNow);
      if (mNextStepShowTime==mCurrentStepShowTime) {
        // means we want next step as soon as this one's result is out, which is at earliest next display time (if we are lucky)
        mNextStepShowTime = mEarliestNextDispApply;
      }
      realNow = MainLoop::now();
      stepTime = realNow-stepTime; // time spent in step calculation
      #if LED_UPDATE_STATS
      mNumViewSteps++;
      if (stepTime<mMinStepTime) mMinStepTime = stepTime;
      if (stepTime>mMaxStepTime) mMaxStepTime = stepTime;
      #endif
      // is this step's result state something we should bring to the display?
      if (mRootView->isDirty() || mCurrentStepShowTime>mEarliestNextDispApply+MAX_UPDATE_INTERVAL) {
        // yes, we should bring that state to display at mCurrentStepShowTime
        // But do not advance planned rendering to the future, just possibly draw it closer
        if (!DEFINED_TIME(mRenderPendingFor) || mRenderPendingFor>mCurrentStepShowTime) mRenderPendingFor = mCurrentStepShowTime;
        continue; // try right now (might be possible unless display apply is pending)
      }
      // no change in view hierarchy that needs rendering.
    }
    // we get here when there is no render pending (or done mRenderWithApply), but maybe a display update
    if (!DEFINED_TIME(mNextStepShowTime)) {
      // no next step requested, just run one occasionally
      if (!DEFINED_TIME(scheduleNextFor)) scheduleNextFor = realNow+MAX_UPDATE_INTERVAL;
    }
    else {
      // next step requested, calculating a (hopefully) future display state
      MLMicroSeconds startBeforeShow = 2*mBufferTime; // start calculating step that much before it's planned show time
      if (mNextStepShowTime-realNow > startBeforeShow) {
        // no point in calculating too much into the future.
        // Schedule calculation in real time for two buffer times before result is needed
        MLMicroSeconds sch = mNextStepShowTime-startBeforeShow;
        if (!DEFINED_TIME(scheduleNextFor) || scheduleNextFor>sch) scheduleNextFor = sch;
      }
      else {
        // we're close to show time, calculate it
        scheduleNextFor = realNow; // schedule immediately in case we fall out of the loop...
        continue; // ..but try to loop
      }
    }
  } // for looping limit
  #if LED_UPDATE_STATS>1
  mStepLog[mStepLogIdx].schedulenext = scheduleNextFor;
  mStepLog[mStepLogIdx].left = realNow;
  mLogged++;
  mStepLogIdx++;
  if (mStepLogIdx>=cStepLogSize) mStepLogIdx = 0;
  #endif // LED_UPDATE_STATS>1
  return scheduleNextFor;
}


void LEDChainArrangement::autoStep(MLTimer &aTimer)
{
  DBGFOCUSOLOG("\r\n\n######## autostep() called");
  mNextAutoStep = step();
  MainLoop::currentMainLoop().retriggerTimer(aTimer, mNextAutoStep, 0, MainLoop::absolute);
}


void LEDChainArrangement::render()
{
  DBGFOCUSOLOG("\r######## render() called");
  // run this next step right now, but calculate it for the same time as the last (current) step calculated (to prevent step show time going backwardss!)
  mNextStepShowTime = !DEFINED_TIME(mCurrentStepShowTime) ? MainLoop::now() : mCurrentStepShowTime;
  mNextAutoStep = step();
  mAutoStepTicket.executeOnceAt(boost::bind(&LEDChainArrangement::autoStep, this, _1), mNextAutoStep);
}


void LEDChainArrangement::externalUpdateRequest()
{
  DBGFOCUSOLOG("\r######## externalUpdateRequest()");
  if (mRootView) {
    MLMicroSeconds realNow = MainLoop::now();
    // prevent extra steps when next scheduled step is near enough
    if (mNextAutoStep > realNow+2*mMinUpdateInterval) {
      // run this next step right now, but calculate it for the same time as the last (current) step calculated (to prevent step show time going backwardss!)
      mNextStepShowTime = !DEFINED_TIME(mCurrentStepShowTime) ? realNow : mCurrentStepShowTime;
      if (mAutoStepTicket) {
        // interrupt autostepping timer
        DBGFOCUSOLOG("- externalUpdateRequest: interrupts scheduled autostep and inserts step right now");
        // start new with immediate step call
        mNextAutoStep = realNow;
        mAutoStepTicket.executeOnce(boost::bind(&LEDChainArrangement::autoStep, this, _1));
      }
      else {
        // just step
        mNextAutoStep = step();
      }
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
FUNC_ARG_DEFS(addledchain, { text } );
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
  f->finish(new IntegerValue(l->ledChainArrangement().getNeededPower()));
}


// currentledpower()
static void currentledpower_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  f->finish(new IntegerValue(l->ledChainArrangement().getCurrentPower()));
}


// setmaxledpower()
FUNC_ARG_DEFS(setmaxledpower, { numeric } );
static void setmaxledpower_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  l->ledChainArrangement().setPowerLimit(f->arg(0)->intValue());
  f->finish();
}


// setledrefresh(minUpdateInterval [, maxpriorityinterval [, bufferTime, [, renderwithapply]])
FUNC_ARG_DEFS(setledrefresh, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } );
static void setledrefresh_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  if (f->arg(0)->defined()) {
    MLMicroSeconds intvl = f->arg(0)->doubleValue()*Second;
    l->ledChainArrangement().setMinUpdateInterval(intvl);
    l->ledChainArrangement().setMaxPriorityInterval(intvl*3); // auto-adjust to sensible value
  }
  if (f->arg(1)->defined()) {
    l->ledChainArrangement().setMaxPriorityInterval(f->arg(1)->doubleValue()*Second);
  }
  if (f->arg(2)->defined()) {
    l->ledChainArrangement().setBufferTime(f->arg(2)->doubleValue()*Second);
  }
  if (f->arg(3)->defined()) {
    l->ledChainArrangement().setRenderWithApply(f->arg(3)->boolValue());
  }
  f->finish();
}


// setrootview(view)
FUNC_ARG_DEFS(setrootview, { objectvalue } );
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
  ObjectValue* cover = new ObjectValue();
  cover->setMemberByName("x", new IntegerValue(crect.x));
  cover->setMemberByName("y", new IntegerValue(crect.y));
  cover->setMemberByName("dx", new IntegerValue(crect.dx));
  cover->setMemberByName("dy", new IntegerValue(crect.dy));
  f->finish(cover);
}


// ledmaindim()
// ledmaindim(newdim)
FUNC_ARG_DEFS(ledmaindim, { numeric|optionalarg } );
static void ledmaindim_func(BuiltinFunctionContextPtr f)
{
  LEDChainLookup* l = dynamic_cast<LEDChainLookup*>(f->funcObj()->getMemberLookup());
  if (f->numArgs()==0) {
    f->finish(new NumericValue(l->ledChainArrangement().getMainDimFactor()*100));
  }
  else {
    l->ledChainArrangement().setMainDimFactor(f->arg(0)->doubleValue()/100);
    f->finish();
  }
}


static const BuiltinMemberDescriptor ledChainArrangementGlobals[] = {
  FUNC_DEF_W_ARG(addledchain, executable),
  FUNC_DEF_NOARG(removeledchains, executable),
  FUNC_DEF_NOARG(ledchaincover, executable|objectvalue),
  FUNC_DEF_NOARG(neededledpower, executable|numeric),
  FUNC_DEF_NOARG(currentledpower, executable|numeric),
  FUNC_DEF_W_ARG(setmaxledpower, executable),
  FUNC_DEF_W_ARG(setrootview, executable),
  FUNC_DEF_W_ARG(setledrefresh, executable),
  FUNC_DEF_W_ARG(ledmaindim, executable),
  BUILTINS_TERMINATOR
};


LEDChainLookup::LEDChainLookup(LEDChainArrangement& aLedChainArrangement) :
  inherited(ledChainArrangementGlobals),
  mLedChainArrangement(aLedChainArrangement)
{
}

#endif // ENABLE_P44SCRIPT

#endif // ENABLE_P44LRGRAPHICS


#if 0

class PWMTableVerifier
{
public:
  PWMTableVerifier() {
    //roundingoptimizer();
    tabledump();
    //testcalc();
    exit(1);
  }

  void tabledump() {
    // verification table
    double exponent = 3;
    LEDChannelPower minpower = 0;

    printf("=== Table with exponent=%f, minpower=%d\n", exponent, minpower);
    LEDPowerConverter stdtable(exponent, minpower);
    for (int bright=0; bright<=PIXELMAX; bright++) {
      // generating PWMs
      LEDChannelPower red, green, blue, white;
      stdtable.powersForComponents(0, bright, bright, bright, bright, red, green, blue, white);
      printf(
        "Brightness=%3d | red16=%6d, green16=%6d, blue16=%6d, white16=%6d  |  red8=%3d, green8=%3d, blue8=%3d, white8=%3d\n",
        bright, red, green, blue, white,
        pwmTo8Bits(red), pwmTo8Bits(green), pwmTo8Bits(blue), pwmTo8Bits(white)
      );
    }
    printf("--- done ---\n\n");
  }
};

static PWMTableVerifier gV;

#endif // 0
