//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "gpio.hpp"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#ifdef ESP_PLATFORM
  #include "driver/gpio.h"
#else
  #include <fcntl.h>
  #include <stdio.h>
  #include <sys/ioctl.h>
  #include <unistd.h>
  #include "gpio.h" // NS9XXX GPIO header, included in project
#endif

#include "logger.hpp"
#include "mainloop.hpp"

using namespace p44;

#ifdef ESP_PLATFORM

// MARK: - GPIO via ESP32 gpio

GpioPin::GpioPin(int aGpioNo, bool aOutput, bool aInitialState, Tristate aPull) :
  mGpioNo((gpio_num_t)aGpioNo),
  mOutput(aOutput),
  mPinState(aInitialState)
  #ifndef ESP_PLATFORM
  ,mGpioFD(-1)
  #endif
{
  esp_err_t ret;
  // make sure pin is set to GPIO
  ret = gpio_reset_pin(mGpioNo);
  if (ret==ESP_OK) {
    // set pullup/down
    ret = gpio_set_pull_mode(mGpioNo, aPull==yes ? GPIO_PULLUP_ONLY : (aPull==no ? GPIO_PULLUP_PULLDOWN : GPIO_FLOATING));
  }
  if (ret==ESP_OK) {
    // set direction
    ret = gpio_set_direction(mGpioNo, mOutput ? GPIO_MODE_OUTPUT : GPIO_MODE_INPUT);
    if (mOutput && ret==ESP_OK) {
      // set initial state
      ret = gpio_set_level(mGpioNo, mPinState ? 1 : 0);
    }
  }
  if (ret!=ESP_OK) {
    LOG(LOG_ERR,"GPIO init error: %s", esp_err_to_name(ret));
    gpio_reset_pin(mGpioNo);
    mGpioNo = GPIO_NUM_NC; // signal "not connected"
  }
}


GpioPin::~GpioPin()
{
  // reset to default (disabled) state
  gpio_reset_pin(mGpioNo);
}


bool GpioPin::getState()
{
  if (mOutput) {
    return mPinState; // just return last set state
  }
  else {
    // is input
    if (mGpioNo!=GPIO_NUM_NC) {
      return gpio_get_level(mGpioNo);
    }
  }
  return false; // non-working pins always return false
}


bool GpioPin::setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  // TODO: implement interrupts
  // for now use poll-based input change detection
  return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  /*
  if (aInputChangedCB==NULL) {
    // release handler
    MainLoop::currentMainLoop().unregisterPollHandler(mGpioFD);
    return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  }
  // anyway, save parameters for base class
  inputChangedCB = aInputChangedCB;
  invertedReporting = aInverted;
  currentState = aInitialState;
  debounceTime = aDebounceTime;
  // try to open "edge" to configure interrupt
  string edgePath = string_format("%s/gpio%d/edge", GPIO_SYS_CLASS_PATH, mGpioNo);
  int edgeFd = open(edgePath.c_str(), O_RDWR);
  if (edgeFd<0) {
    LOG(LOG_DEBUG, "GPIO edge file does not exist -> GPIO %d has no edge interrupt capability", mGpioNo);
    // use poll-based input change detection
    return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  }
  // enable triggering on both edges
  string s = "both";
  ssize_t ret = write(edgeFd, s.c_str(), s.length());
  if (ret<0) { LOG(LOG_ERR, "Cannot write to GPIO edge file %s", strerror(errno)); return false; }
  close(edgeFd);
  // establish a IO poll
  MainLoop::currentMainLoop().registerPollHandler(mGpioFD, POLLPRI, boost::bind(&GpioPin::stateChanged, this, _2));
  return true;
  */
}


bool GpioPin::stateChanged(int aPollFlags)
{
  bool newState = getState();
  //LOG(LOG_DEBUG, "GPIO %d edge detected (poll() returned POLLPRI for value file) : new state = %d", mGpioNo, newState);
  inputHasChangedTo(newState);
  return true; // handled
}


void GpioPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mGpioNo==GPIO_NUM_NC) return; // non-existing pins cannot be set
  mPinState = aState;
  // - set value
  gpio_set_level(mGpioNo, mPinState ? 1 : 0);
}


#else

// MARK: - LEDs via modern kernel support

#define GPIO_LED_CLASS_PATH "/sys/class/leds"

GpioLedPin::GpioLedPin(const char* aLedName, bool aInitialState) :
  mLedState(aInitialState),
  mLedFD(-1)
{
  string name;
  if (isdigit(*aLedName)) {
    // old style number-only -> prefix with "led"
    name = string_format("%s/led%s/brightness", GPIO_LED_CLASS_PATH, aLedName);
  }
  else {
    // modern alphanumeric LED name -> use as-is
    name = string_format("%s/%s/brightness", GPIO_LED_CLASS_PATH, aLedName);
  }
  mLedFD = open(name.c_str(), O_RDWR);
  if (mLedFD<0) { LOG(LOG_ERR, "Cannot open LED brightness file %s: %s", name.c_str(), strerror(errno)); return; }
  // set initial state
  setState(mLedState);
}


GpioLedPin::~GpioLedPin()
{
  if (mLedFD>0) {
    close(mLedFD);
  }
}


bool GpioLedPin::getState()
{
  return mLedState; // just return last set state
}


void GpioLedPin::setState(bool aState)
{
  if (mLedFD<0) return; // non-existing pins cannot be set
  mLedState = aState;
  // - set value
  char buf[2];
  buf[0] = mLedState ? '1' : '0';
  buf[1] = 0;
  write(mLedFD, buf, 1);
}


#if ENABLE_GPIOSYSFS

// MARK: - GPIO via classic kernel support

#define GPIO_SYS_CLASS_PATH "/sys/class/gpio"

GpioPin::GpioPin(int aGpioNo, bool aOutput, bool aInitialState, Tristate aPull) :
  mGpioNo(aGpioNo),
  mOutput(aOutput),
  mPinState(aInitialState),
  mGpioFD(-1)
{
  // TODO: implement pull up/down.
  int tempFd;
  ssize_t ret;
  string name;
  string s = string_format("%d", mGpioNo);
  // have the kernel export the pin
  name = string_format("%s/export",GPIO_SYS_CLASS_PATH);
  tempFd = open(name.c_str(), O_WRONLY);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open GPIO export file %s: %s", name.c_str(), strerror(errno)); return; }
  ret = write(tempFd, s.c_str(), s.length());
  if (ret<0) { LOG(LOG_WARNING, "Cannot write '%s' to GPIO export file %s: %s, probably already exported", s.c_str(), name.c_str(), strerror(errno)); }
  close(tempFd);
  // save base path
  string basePath = string_format("%s/gpio%d", GPIO_SYS_CLASS_PATH, mGpioNo);
  // configure
  name = basePath + "/direction";
  tempFd = open(name.c_str(), O_RDWR);
  if (tempFd<0) { LOG(LOG_ERR, "Cannot open GPIO direction file %s: %s", name.c_str(), strerror(errno)); return; }
  if (mOutput) {
    // output
    // - set output with initial value
    s = mPinState ? "high" : "low";
    ret = write(tempFd, s.c_str(), s.length());
  }
  else {
    // input
    // - set input
    s = "in";
    ret = write(tempFd, s.c_str(), s.length());
  }
  if (ret<0) { LOG(LOG_WARNING, "Cannot write '%s' to GPIO direction file %s: %s", s.c_str(), name.c_str(), strerror(errno)); }
  close(tempFd);
  // now keep the value FD open
  name = basePath + "/value";
  mGpioFD = open(name.c_str(), O_RDWR);
  if (mGpioFD<0) { LOG(LOG_ERR, "Cannot open GPIO value file %s: %s", name.c_str(), strerror(errno)); return; }
}


GpioPin::~GpioPin()
{
  if (mGpioFD>0) {
    MainLoop::currentMainLoop().unregisterPollHandler(mGpioFD);
    close(mGpioFD);
  }
}


bool GpioPin::getState()
{
  if (mOutput)
    return mPinState; // just return last set state
  else {
    // is input
    if (mGpioFD<0)
      return false; // non-working pins always return false
    else {
      // read from input
      char buf[2];
      lseek(mGpioFD, 0, SEEK_SET);
      if (read(mGpioFD, buf, 1)>0) {
        return buf[0]!='0';
      }
    }
  }
  return false;
}


bool GpioPin::setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  if (!aInputChangedCB) {
    // release handler
    MainLoop::currentMainLoop().unregisterPollHandler(mGpioFD);
    return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  }
  // anyway, save parameters for base class
  mInputChangedCB = aInputChangedCB;
  mInvertedReporting = aInverted;
  mCurrentState = aInitialState;
  mDebounceTime = aDebounceTime;
  // try to open "edge" to configure interrupt
  string edgePath = string_format("%s/gpio%d/edge", GPIO_SYS_CLASS_PATH, mGpioNo);
  int edgeFd = open(edgePath.c_str(), O_RDWR);
  if (edgeFd<0) {
    LOG(LOG_DEBUG, "GPIO edge file does not exist -> GPIO %d has no edge interrupt capability", mGpioNo);
    // use poll-based input change detection
    return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  }
  // enable triggering on both edges
  string s = "both";
  ssize_t ret = write(edgeFd, s.c_str(), s.length());
  if (ret<0) { LOG(LOG_ERR, "Cannot write to GPIO edge file %s", strerror(errno)); return false; }
  close(edgeFd);
  // establish a IO poll
  MainLoop::currentMainLoop().registerPollHandler(mGpioFD, POLLPRI, boost::bind(&GpioPin::stateChanged, this, _2));
  return true;
}


bool GpioPin::stateChanged(int aPollFlags)
{
  bool newState = getState();
  //LOG(LOG_DEBUG, "GPIO %d edge detected (poll() returned POLLPRI for value file) : new state = %d", gpioNo, newState);
  inputHasChangedTo(newState);
  return true; // handled
}


void GpioPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mGpioFD<0) return; // non-existing pins cannot be set
  mPinState = aState;
  // - set value
  char buf[2];
  buf[0] = mPinState ? '1' : '0';
  buf[1] = 0;
  write(mGpioFD, buf, 1);
}

#endif // ENABLE_GPIOSYSFS


#if GPIOD_VERS==1

// MARK: - GPIO via libgpiod v1 interface

int GpiodPin::requestLineType(int aRequestType)
{
  gpiod_line_request_config cfg;
  cfg.consumer = "p44utils";
  cfg.flags = mLineFlags;
  // - input/output
  cfg.request_type = aRequestType;
  // request the line
  return gpiod_line_request(mLine, &cfg, mPinState);
}



GpiodPin::GpiodPin(int aChipNo, const char* aPinName, bool aOutput, bool aInitialState, Tristate aPull) :
  mChip(nullptr),
  mLine(nullptr),
  mLineFlags(0),
  mOutput(aOutput),
  mPinState(aInitialState)
{
  string chipname = string_format("gpiochip%d", aChipNo);
  mChip = gpiod_chip_open_by_name(chipname.c_str());
  if (!mChip) { LOG(LOG_ERR, "Cannot access %s: %s", chipname.c_str(), strerror(errno)); return; }
  mLine = gpiod_chip_find_line(mChip, aPinName); // try by name first
  if (!mLine) {
    int lineIndex = atoi(aPinName);
    mLine = gpiod_chip_get_line(mChip, lineIndex);
  }
  if (!mLine) {
    gpiod_chip_close(mChip); mChip = nullptr;
    LOG(LOG_ERR, "Cannot find gpio line %s.%s: %s", chipname.c_str(), aPinName, strerror(errno));
    return;
  }
  // found the line
  // - configure pull up/down
  if (aPull==yes) mLineFlags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_UP;
  else if (aPull==no) mLineFlags |= GPIOD_LINE_REQUEST_FLAG_BIAS_PULL_DOWN;
  else mLineFlags |= GPIOD_LINE_REQUEST_FLAG_BIAS_DISABLE;
  // - request the line
  int ret = requestLineType(mOutput ? GPIOD_LINE_REQUEST_DIRECTION_OUTPUT : GPIOD_LINE_REQUEST_DIRECTION_INPUT);
  if (ret<0) {
    LOG(LOG_ERR, "Failed requesting gpio line %s.%s: %s", chipname.c_str(), aPinName, strerror(errno));
    gpiod_line_release(mLine); mLine = nullptr;
    gpiod_chip_close(mChip); mChip = nullptr;
  }
}


GpiodPin::~GpiodPin()
{
  if (mLine) {
    int fd = gpiod_line_event_get_fd(mLine);
    if (fd>=0) MainLoop::currentMainLoop().unregisterPollHandler(fd);
    gpiod_line_release(mLine); mLine = nullptr;
  }
  if (mChip) {
    gpiod_chip_close(mChip); mChip = nullptr;
  }
}


bool GpiodPin::getState()
{
  if (mOutput)
    return mPinState; // just return last set state
  else {
    // is input
    if (!mLine) {
      return false; // non-working pins always return false
    }
    else {
      // read from input
      int ret = gpiod_line_get_value(mLine);
      if (ret<0) {
        LOG(LOG_ERR, "Failed reading gpio line: %s", strerror(errno));
      }
      return ret==1;
    }
  }
  return false;
}


bool GpiodPin::setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  if (!mLine) return false;
  if (!aInputChangedCB) {
    // release handler
    int fd = gpiod_line_event_get_fd(mLine);
    if (fd>=0) MainLoop::currentMainLoop().unregisterPollHandler(fd);
    gpiod_line_release(mLine);
    if (requestLineType(GPIOD_LINE_REQUEST_DIRECTION_INPUT)<0) {
      LOG(LOG_ERR, "Error restoring normal input after turning off edge detection: %s", strerror(errno));
    }
    return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  }
  // anyway, save parameters for base class
  mInputChangedCB = aInputChangedCB;
  mInvertedReporting = aInverted;
  mCurrentState = aInitialState;
  mDebounceTime = aDebounceTime;
  // try to reconfigure the input as edge detecting
  if (!mOutput && mLine) {
    gpiod_line_release(mLine);
    if (requestLineType(GPIOD_LINE_REQUEST_EVENT_BOTH_EDGES)<0) {
      LOG(LOG_DEBUG, "Cannot switch input to edge detection: %s", strerror(errno));
      // fall back to plain input
      if (requestLineType(GPIOD_LINE_REQUEST_DIRECTION_INPUT)<0) {
        LOG(LOG_ERR, "Error restoring normal input after failed edge detection request: %s", strerror(errno));
        return false; // failure to revert to normal input means edge detection does not work at all
      }
      // input ok, but does not support edge detectuon: use poll-based input change detection instead
      return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
    }
    // edge detection active now -> establish a IO poll
    int fd = gpiod_line_event_get_fd(mLine);
    if (fd>=0) {
      MainLoop::currentMainLoop().registerPollHandler(fd, POLLIN, boost::bind(&GpiodPin::stateChanged, this, _2));
      return true;
    }
    LOG(LOG_ERR, "no event fd available for gpio line: %s", strerror(errno));
  }
  return false;
}


bool GpiodPin::stateChanged(int aPollFlags)
{
  struct gpiod_line_event lev;
  bool newState;
  if (gpiod_line_event_read(mLine, &lev)>=0) {
    if (lev.event_type==GPIOD_LINE_EVENT_RISING_EDGE) newState = true;
    else if (lev.event_type==GPIOD_LINE_EVENT_FALLING_EDGE) newState = false;
    else return true; // no know event, still handled
  }
  inputHasChangedTo(newState);
  return true; // handled
}


void GpiodPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (!mLine) return; // non-existing pins cannot be set
  mPinState = aState;
  // - set value
  int ret = gpiod_line_set_value(mLine, aState);
  if (ret<0) {
    LOG(LOG_ERR, "Failed reading gpio line: %s", strerror(errno));
  }
}

#endif // GPIOD_VERS==1


#if GPIOD_VERS==2

// MARK: - GPIO via libgpiod v2 interface


void GpiodPin::releaseLine()
{
  if (mLineRequest) {
    if (mEventBuffer) {
      int fd = gpiod_line_request_get_fd(mLineRequest);
      if (fd>=0) MainLoop::currentMainLoop().unregisterPollHandler(fd);
      gpiod_edge_event_buffer_free(mEventBuffer);
      mEventBuffer = nullptr;
    }
    gpiod_line_request_release(mLineRequest);
    mLineRequest = nullptr;
  }
}


int GpiodPin::requestLine(bool aEdgeDetection)
{
  if (!mChip) return -1;
  // free any previous request
  releaseLine();
  // prepare settings
  struct gpiod_line_settings *settings = gpiod_line_settings_new();
  if (!settings) {
    LOG(LOG_ERR, "Failed to create gpiod line settings: %s", strerror(errno));
    return -1;
  }
  // - direction / edge / initial value
  int ret = 0;
  if (mOutput) {
    // output
    ret |= gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_OUTPUT);
    ret |= gpiod_line_settings_set_output_value(settings, mPinState ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE);
  }
  else {
    // input
    ret |= gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    if (aEdgeDetection) ret |= gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
    else ret |= gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_NONE);
  }
  if (ret<0) {
    LOG(LOG_ERR, "Failed configuring line settings: %s", strerror(errno));
    gpiod_line_settings_free(settings);
    return -1;
  }
  // - bias (pullup/down)
  if (mLineBias!=0) {
    if (gpiod_line_settings_set_bias(settings, mLineBias)<0) {
      LOG(LOG_WARNING, "Failed setting line bias: %s", strerror(errno));
    }
  }
  // prepare line config
  struct gpiod_line_config *line_cfg = gpiod_line_config_new();
  if (!line_cfg) {
    LOG(LOG_ERR, "Failed to create gpiod line config: %s", strerror(errno));
    gpiod_line_settings_free(settings);
    return -1;
  }
  // - add single line with these settings
  unsigned int offset = mOffset;
  if (gpiod_line_config_add_line_settings(line_cfg, &offset, 1, settings)<0) {
    LOG(LOG_ERR, "Failed adding line settings to line config: %s", strerror(errno));
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    return -1;
  }
  // prepare request config
  struct gpiod_request_config *req_cfg = gpiod_request_config_new();
  if (!req_cfg) {
    LOG(LOG_ERR, "Failed to create gpiod request config: %s", strerror(errno));
    gpiod_line_config_free(line_cfg);
    gpiod_line_settings_free(settings);
    return -1;
  }
  // now request the line
  gpiod_request_config_set_consumer(req_cfg, "p44utils");
  mLineRequest = gpiod_chip_request_lines(mChip, req_cfg, line_cfg);
  // release the configs
  gpiod_request_config_free(req_cfg);
  gpiod_line_config_free(line_cfg);
  gpiod_line_settings_free(settings);
  if (!mLineRequest) {
    LOG(LOG_ERR, "Failed requesting gpio line (offset %u): %s", mOffset, strerror(errno));
    return -1;
  }
  return 0;
}


GpiodPin::GpiodPin(int aChipNo, const char* aPinName, bool aOutput, bool aInitialState, Tristate aPull) :
  mChip(nullptr),
  mLineRequest(nullptr),
  mLineBias(GPIOD_LINE_BIAS_DISABLED),
  mOffset(0),
  mEventBuffer(nullptr),
  mPinState(aInitialState),
  mOutput(aOutput)
{
  // open chip as /dev/gpiochipN
  string chipdev = string_format("/dev/gpiochip%d", aChipNo);
  mChip = gpiod_chip_open(chipdev.c_str());
  if (!mChip) {
    LOG(LOG_ERR, "Cannot access %s: %s", chipdev.c_str(), strerror(errno));
    return;
  }
  // resolve line offset from name or numeric string
  if (aPinName && *aPinName) {
    char *endp = nullptr;
    long val = strtol(aPinName, &endp, 10);
    if (endp && *endp == 0) {
      // pure number -> offset
      mOffset = (unsigned int)val;
    }
    else {
      // by name
      int offset = gpiod_chip_get_line_offset_from_name(mChip, aPinName);
      if (offset<0) {
        LOG(LOG_ERR, "Cannot find gpio line %s (on %s): %s", aPinName, chipdev.c_str(), strerror(errno));
        gpiod_chip_close(mChip); mChip = nullptr;
        return;
      }
      mOffset = (unsigned int)offset;
    }
  }
  else {
    LOG(LOG_ERR, "Invalid gpio line name");
    gpiod_chip_close(mChip); mChip = nullptr;
    return;
  }
  // configure bias equivalent to v1 flags
  if (aPull==yes) mLineBias = GPIOD_LINE_BIAS_PULL_UP;
  else if (aPull==no) mLineBias = GPIOD_LINE_BIAS_PULL_DOWN;
  else mLineBias = GPIOD_LINE_BIAS_DISABLED;
  // request the line as input or output (but no edge detection yet)
  int ret = requestLine(false);
  if (ret<0) {
    LOG(LOG_ERR, "Failed requesting gpio line %s.%s (offset %u): %s", chipdev.c_str(), aPinName, mOffset, strerror(errno));
    if (mLineRequest) {
      gpiod_line_request_release(mLineRequest); mLineRequest = nullptr;
    }
    gpiod_chip_close(mChip); mChip = nullptr;
  }
}


GpiodPin::~GpiodPin()
{
  releaseLine();
  if (mChip) {
    gpiod_chip_close(mChip);
    mChip = nullptr;
  }
}


bool GpiodPin::getState()
{
  if (mOutput) {
    return mPinState; // just return last set state
  }
  if (!mLineRequest) {
    return false; // non-working pins always return false
  }
  int val = gpiod_line_request_get_value(mLineRequest, mOffset);
  if (val<0) {
    LOG(LOG_ERR, "Failed reading gpio line value: %s", strerror(errno));
    return false;
  }
  return val == GPIOD_LINE_VALUE_ACTIVE;
}


bool GpiodPin::setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  if (!mLineRequest || mOutput) return false; // no line or output -> cannot detect edges
  if (!aInputChangedCB) {
    // release handler and switch back to plain input
    if (requestLine(false)<0) {
      LOG(LOG_ERR, "Error restoring normal input after turning off edge detection: %s", strerror(errno));
    }
    return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
  }
  // anyway, save parameters for base class
  mInputChangedCB     = aInputChangedCB;
  mInvertedReporting  = aInverted;
  mCurrentState       = aInitialState;
  mDebounceTime       = aDebounceTime;
  // try to reconfigure the input as edge detecting
  if (mLineRequest) {
    if (requestLine(true)<0) {
      LOG(LOG_DEBUG, "Cannot switch input to edge detection: %s", strerror(errno));
      // fall back to plain input
      if (requestLine(false)<0) {
        LOG(LOG_ERR, "Error restoring normal input after failed edge detection request: %s", strerror(errno));
        return false; // failure to revert to normal input means edge detection does not work at all
      }
      // input ok, but does not support edge detection: use poll-based input change detection instead
      return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
    }
    // edge detection active now -> establish an IO poll
    if (!mEventBuffer) {
      mEventBuffer = gpiod_edge_event_buffer_new(1);
      if (!mEventBuffer) {
        LOG(LOG_ERR, "Cannot create gpiod edge event buffer: %s", strerror(errno));
        // fall back: revert to plain input and polling
        if (requestLine(false)<0) {
          LOG(LOG_ERR, "Error restoring normal input after event buffer failure: %s", strerror(errno));
          return false;
        }
        return inherited::setInputChangedHandler(aInputChangedCB, aInverted, aInitialState, aDebounceTime, aPollInterval);
      }
    }
    int fd = gpiod_line_request_get_fd(mLineRequest);
    if (fd>=0) {
      MainLoop::currentMainLoop().registerPollHandler(fd, POLLIN, boost::bind(&GpiodPin::stateChanged, this, _2));
      return true;
    }
    LOG(LOG_ERR, "no event fd available for gpio line request: %s", strerror(errno));
  }
  return false;
}


bool GpiodPin::stateChanged(int aPollFlags)
{
  if (!mLineRequest || !mEventBuffer) return true;
  int ret = gpiod_line_request_read_edge_events(mLineRequest, mEventBuffer, 1);
  if (ret<0) {
    LOG(LOG_ERR, "Failed reading gpio edge events: %s", strerror(errno));
    return true; // handled (error reported)
  }
  struct gpiod_edge_event *ev = gpiod_edge_event_buffer_get_event(mEventBuffer, 0);
  if (!ev) return true; // no event, ignored but handled
  bool newState;
  switch (gpiod_edge_event_get_event_type(ev)) {
    case GPIOD_EDGE_EVENT_RISING_EDGE: newState = true; break;
    case GPIOD_EDGE_EVENT_FALLING_EDGE: newState = false; break;
    default: return true; // unknown event, ignored but handled
  }
  inputHasChangedTo(newState);
  return true; // handled
}


void GpiodPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (!mLineRequest)   return; // non-existing pins cannot be set
  mPinState = aState;
  int ret = gpiod_line_request_set_value(
    mLineRequest,
    mOffset,
    mPinState ? GPIOD_LINE_VALUE_ACTIVE : GPIOD_LINE_VALUE_INACTIVE
  );
  if (ret<0) {
    LOG(LOG_ERR, "Failed setting gpio line value: %s", strerror(errno));
  }
}

#endif // GPIOD_VERS==2

#if P44_BUILD_DIGI

// MARK: - GPIO for NS9xxx (Digi ME 9210 LX)

GpioNS9XXXPin::GpioNS9XXXPin(const char* aGpioName, bool aOutput, bool aInitialState) :
  mGpioFD(-1),
  mPinState(false)
{
  // save params
  mOutput = aOutput;
  mName = aGpioName;
  mPinState = aInitialState; // set even for inputs

  int ret_val;
  // open device
  string gpiopath(GPION9XXX_DEVICES_BASEPATH);
  gpiopath.append(mName);
  mGpioFD = open(gpiopath.c_str(), O_RDWR);
  if (mGpioFD<0) {
    LOG(LOG_ERR, "Cannot open GPIO device %s: %s", mName.c_str(), strerror(errno));
    return;
  }
  // configure
  if (mOutput) {
    // output
    if ((ret_val = ioctl(mGpioFD, GPIO_CONFIG_AS_OUT)) < 0) {
      LOG(LOG_ERR, "GPIO_CONFIG_AS_OUT failed for %s: %s", mName.c_str(), strerror(errno));
      return;
    }
    // set state immediately
    setState(mPinState);
  }
  else {
    // input
    if ((ret_val = ioctl(mGpioFD, GPIO_CONFIG_AS_INP)) < 0) {
      LOG(LOG_ERR, "GPIO_CONFIG_AS_INP failed for %s: %s", mName.c_str(), strerror(errno));
      return;
    }
  }
}


GpioNS9XXXPin::~GpioNS9XXXPin()
{
  if (mGpioFD>0) {
    close(mGpioFD);
  }
}


bool GpioNS9XXXPin::getState()
{
  if (mOutput)
    return mPinState; // just return last set state
  if (mGpioFD<0)
    return false; // non-working pins always return false
  else {
    // read from input
    int inval;
    #ifndef __APPLE__
    int ret_val;
    if ((ret_val = ioctl(gpioFD, GPIO_READ_PIN_VAL, &inval)) < 0) {
      LOG(LOG_ERR, "GPIO_READ_PIN_VAL failed for %s: %s", name.c_str(), strerror(errno));
      return false;
    }
    #else
    DBGLOG(LOG_ERR, "ioctl(gpioFD, GPIO_READ_PIN_VAL, &dummy)");
    inval = 0;
    #endif
    return (bool)inval;
  }
}


void GpioNS9XXXPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mGpioFD<0) return; // non-existing pins cannot be set
  mPinState = aState;
  // - set value
  int setval = mPinState;
  #ifndef __APPLE__
  int ret_val;
  if ((ret_val = ioctl(gpioFD, GPIO_WRITE_PIN_VAL, &setval)) < 0) {
    LOG(LOG_ERR, "GPIO_WRITE_PIN_VAL failed for %s: %s", name.c_str(), strerror(errno));
    return;
  }
  #else
  DBGLOG(LOG_ERR, "ioctl(gpioFD, GPIO_WRITE_PIN_VAL, %d)", setval);
  #endif
}

#endif // P44_BUILD_DIGI

#endif // !ESP_PLATFORM
