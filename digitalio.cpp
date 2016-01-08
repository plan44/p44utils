//
//  Copyright (c) 2013-2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


#include "digitalio.hpp"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "iopin.hpp"

#if !DISABLE_GPIO
#include "gpio.hpp"
#endif
#if !DISABLE_I2C
#include "i2c.hpp"
#endif

#include "logger.hpp"
#include "mainloop.hpp"

using namespace p44;


DigitalIo::DigitalIo(const char* aName, bool aOutput, bool aInverted, bool aInitialState) 
{
  // save params
  output = aOutput;
  // allow inverting via prefixing name with slash
  if (aName && *aName=='/') {
    inverted = !aInverted;
    ++aName; // skip first char of name for further processing
  }
  else
    inverted = aInverted;
  name = aName;
  bool initialPinState = aInitialState!=inverted;
  // check for missing pin (no pin, just silently keeping state)
  if (name=="missing") {
    ioPin = IOPinPtr(new MissingPin(initialPinState));
    return;
  }
  // dissect name into bus, device, pin
  string busName;
  string deviceName;
  string pinName;
  size_t i = name.find(".");
  if (i==string::npos) {
    // no structured name, assume GPIO
    busName = "gpio";
  }
  else {
    busName = name.substr(0,i);
    // rest is device + pinname or just pinname
    pinName = name.substr(i+1,string::npos);
    if (busName!="syscmd") {
      i = pinName.find(".");
      if (i!=string::npos) {
        // separate device and pin names
        // - extract device name
        deviceName = pinName.substr(0,i);
        // - remove device name from pin name string
        pinName.erase(0,i+1);
      }
    }
  }
  // now create appropriate pin
  DBGLOG(LOG_DEBUG, "DigitalIo: bus name = '%s'", busName.c_str());
  #if !defined(__APPLE__) && !DISABLE_GPIO
  if (busName=="gpio") {
    // Linux generic GPIO
    // gpio.<gpionumber>
    int pinNumber = atoi(pinName.c_str());
    ioPin = IOPinPtr(new GpioPin(pinNumber, output, initialPinState));
  }
  else if (busName=="led") {
    // Linux generic LED
    // led.<lednumber>
    int pinNumber = atoi(pinName.c_str());
    ioPin = IOPinPtr(new GpioLedPin(pinNumber, initialPinState));
  }
  else
  #endif
  #if defined(DIGI_ESP) && !DISABLE_GPIO
  if (busName=="gpioNS9XXXX") {
    // gpioNS9XXXX.<pinname>
    // NS9XXX driver based GPIO (Digi ME 9210 LX)
    ioPin = IOPinPtr(new GpioNS9XXXPin(pinName.c_str(), output, initialPinState));
  }
  else
  #endif
  #if !DISABLE_I2C
  if (busName.substr(0,3)=="i2c") {
    // i2c<busnum>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    ioPin = IOPinPtr(new I2CPin(busNumber, deviceName.c_str(), pinNumber, output, initialPinState));
  }
  else
  #endif
  #if !DISABLE_SYSTEMCMDIO
  if (busName=="syscmd") {
    // digital I/O calling system command to turn on/off
    ioPin = IOPinPtr(new SysCommandPin(pinName.c_str(), output, initialPinState));
  }
  else
  #endif
  {
    // all other/unknown bus names default to simulated pin
    ioPin = IOPinPtr(new SimPin(name.c_str(), output, initialPinState)); // set even for inputs
  }
}


DigitalIo::~DigitalIo()
{
}


bool DigitalIo::isSet()
{
  return ioPin->getState() != inverted;
}


void DigitalIo::set(bool aState)
{
  ioPin->setState(aState!=inverted);
}


void DigitalIo::on()
{
  set(true);
}


void DigitalIo::off()
{
  set(false);
}


bool DigitalIo::toggle()
{
  bool state = isSet();
  if (output) {
    state = !state;
    set(state);
  }
  return state;
}


bool DigitalIo::setInputChangedHandler(InputChangedCB aInputChangedCB, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  return ioPin->setInputChangedHandler(aInputChangedCB, inverted, ioPin->getState(), aDebounceTime, aPollInterval);
}


#pragma mark - Button input

#define BUTTON_DEBOUNCE_TIME (5*MilliSecond)

ButtonInput::ButtonInput(const char* aName, bool aInverted) :
  DigitalIo(aName, false, aInverted, false),
  repeatActiveReport(Never),
  activeReportTicket(0)
{
  lastChangeTime = MainLoop::now();
}


ButtonInput::~ButtonInput()
{
  MainLoop::currentMainLoop().unregisterIdleHandlers(this);
}


void ButtonInput::setButtonHandler(ButtonHandlerCB aButtonHandler, bool aPressAndRelease, MLMicroSeconds aRepeatActiveReport)
{
  reportPressAndRelease = aPressAndRelease;
  repeatActiveReport = aRepeatActiveReport;
  buttonHandler = aButtonHandler;
  if (buttonHandler) {
    // mainloop idle polling if input does not support edge detection
    setInputChangedHandler(boost::bind(&ButtonInput::inputChanged, this, _1), BUTTON_DEBOUNCE_TIME, 0);
  }
  else {
    // unregister
    setInputChangedHandler(NULL, 0, 0);
    MainLoop::currentMainLoop().cancelExecutionTicket(activeReportTicket);
  }
}



void ButtonInput::inputChanged(bool aNewState)
{
  MLMicroSeconds now = MainLoop::now();
  if (!aNewState || reportPressAndRelease) {
    buttonHandler(aNewState, true, now-lastChangeTime);
  }
  // consider this a state change
  lastChangeTime = now;
  // active state reported now
  if (aNewState && repeatActiveReport!=Never) {
    activeReportTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&ButtonInput::repeatStateReport, this), repeatActiveReport);
  }
  else {
    // no longer active, cancel repeating active state if any
    MainLoop::currentMainLoop().cancelExecutionTicket(activeReportTicket);
  }
}


void ButtonInput::repeatStateReport()
{
  if (buttonHandler) buttonHandler(true, false, MainLoop::now()-lastChangeTime);
  activeReportTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&ButtonInput::repeatStateReport, this), repeatActiveReport);
}



#pragma mark - Indicator output

IndicatorOutput::IndicatorOutput(const char* aName, bool aInverted, bool aInitiallyOn) :
  DigitalIo(aName, true, aInverted, aInitiallyOn),
  switchOffAt(Never),
  blinkOnTime(Never),
  blinkOffTime(Never)
{
  MainLoop::currentMainLoop().registerIdleHandler(this, boost::bind(&IndicatorOutput::timer, this, _1));
}


IndicatorOutput::~IndicatorOutput()
{
  MainLoop::currentMainLoop().unregisterIdleHandlers(this);
}


void IndicatorOutput::onFor(MLMicroSeconds aOnTime)
{
  blinkOnTime = Never;
  blinkOffTime = Never;
  set(true);
  if (aOnTime>0)
    switchOffAt = MainLoop::now()+aOnTime;
  else
    switchOffAt = Never;
}


void IndicatorOutput::blinkFor(MLMicroSeconds aOnTime, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent)
{
  onFor(aOnTime);
  blinkOnTime =  (aBlinkPeriod*aOnRatioPercent*10)/1000;
  blinkOffTime = aBlinkPeriod - blinkOnTime;
  blinkToggleAt = MainLoop::now()+blinkOnTime;
}


void IndicatorOutput::stop()
{
  blinkOnTime = Never;
  blinkOffTime = Never;
  switchOffAt = Never;
}


void IndicatorOutput::steadyOff()
{
  stop();
  off();
}


void IndicatorOutput::steadyOn()
{
  stop();
  on();
}





bool IndicatorOutput::timer(MLMicroSeconds aTimestamp)
{
  // check off time first
  if (switchOffAt!=Never && aTimestamp>=switchOffAt) {
    stop();
  }
  else if (blinkOnTime!=Never) {
    // blinking enabled
    if (aTimestamp>=blinkToggleAt) {
      if (toggle()) {
        // turned on, blinkOnTime starts
        blinkToggleAt = aTimestamp + blinkOnTime;
      }
      else {
        // turned off, blinkOffTime starts
        blinkToggleAt = aTimestamp + blinkOffTime;
      }
    }
  }
  return true;
}



BlindsOutput::BlindsOutput(const char* aGpioNameUp, const char* aGpioNameDown, bool aInverted)
{
  upPin = DigitalIoPtr(new DigitalIo(aGpioNameUp, true, aInverted, false));
  downPin = DigitalIoPtr(new DigitalIo(aGpioNameDown, true, aInverted, false));
}


void BlindsOutput::changeMovement(SimpleCB aDoneCB, int aNewDirection)
{
  if (aNewDirection == 0) {
    // stop
    upPin->set(false);
    downPin->set(false);
  } else if (aNewDirection > 0) {
    downPin->set(false);
    upPin->set(true);
  } else {
    upPin->set(false);
    downPin->set(true);
  }
  if (aDoneCB) {
    aDoneCB();
  }
}
