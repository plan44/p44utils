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



// MARK: - GPIO via modern kernel support

#define GPIO_SYS_CLASS_PATH "/sys/class/gpio"

GpioPin::GpioPin(int aGpioNo, bool aOutput, bool aInitialState, Tristate aPull) :
  mGpioNo(aGpioNo),
  mOutput(aOutput),
  mPinState(aInitialState),
  mGpioFD(-1)
{
  // TODO: convert to modern character device based gpio, implement pull up/down.
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
  if (aInputChangedCB==NULL) {
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

#endif


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

