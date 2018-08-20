//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#if !DISABLE_SPI
#include "spi.hpp"
#endif

#include "logger.hpp"
#include "mainloop.hpp"

using namespace p44;


DigitalIo::DigitalIo(const char* aPinSpec, bool aOutput, bool aInitialState) :
  inverted(false),
  pullUp(false)
{
  // save params
  output = aOutput;
  // check for inverting and pullup prefixes
  while (aPinSpec && *aPinSpec) {
    if (*aPinSpec=='/') inverted = true;
    else if (*aPinSpec=='+') pullUp = true;
    else break; // none of the allowed prefixes -> done
    ++aPinSpec; // processed prefix -> check next
  }
  // rest is actual pin specification
  pinSpec = nonNullCStr(aPinSpec);
  if (pinSpec.size()==0) pinSpec="missing";
  bool initialPinState = aInitialState!=inverted;
  // check for missing pin (no pin, just silently keeping state)
  if (pinSpec=="missing") {
    ioPin = IOPinPtr(new MissingPin(initialPinState));
    return;
  }
  // dissect name into bus, device, pin
  string busName;
  string deviceName;
  string pinName;
  size_t i = pinSpec.find(".");
  if (i==string::npos) {
    // no structured name, assume GPIO
    busName = "gpio";
  }
  else {
    busName = pinSpec.substr(0,i);
    // rest is device + pinname or just pinname
    pinName = pinSpec.substr(i+1,string::npos);
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
    // led.<lednumber_or_name>
    ioPin = IOPinPtr(new GpioLedPin(pinName.c_str(), initialPinState));
  }
  else
  #endif
  #if P44_BUILD_DIGI && !DISABLE_GPIO
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
    ioPin = IOPinPtr(new I2CPin(busNumber, deviceName.c_str(), pinNumber, output, initialPinState, pullUp));
  }
  else
  #endif
  #if !DISABLE_SPI
  if (busName.substr(0,3)=="spi") {
    // spi<interfaceno*10+chipselno>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    ioPin = IOPinPtr(new SPIPin(busNumber, deviceName.c_str(), pinNumber, output, initialPinState, pullUp));
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
    // all other/unknown bus names, including "sim", default to simulated pin operated from console
    ioPin = IOPinPtr(new SimPin(pinSpec.c_str(), output, initialPinState));
  }
}


DigitalIo::~DigitalIo()
{
}


string DigitalIo::getName()
{
  return string_format("%s%s%s", pullUp ? "+" : "", inverted ? "/" : "", pinSpec.c_str());
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


// MARK: ===== Button input

#define BUTTON_DEBOUNCE_TIME (80*MilliSecond)

ButtonInput::ButtonInput(const char* aPinSpec) :
  DigitalIo(aPinSpec, false, false),
  repeatActiveReport(Never)
{
  lastChangeTime = MainLoop::now();
}


ButtonInput::~ButtonInput()
{
  activeReportTicket.cancel();
}


void ButtonInput::setButtonHandler(ButtonHandlerCB aButtonHandler, bool aPressAndRelease, MLMicroSeconds aRepeatActiveReport)
{
  reportPressAndRelease = aPressAndRelease;
  repeatActiveReport = aRepeatActiveReport;
  buttonHandler = aButtonHandler;
  if (buttonHandler) {
    // mainloop idle polling if input does not support edge detection
    setInputChangedHandler(boost::bind(&ButtonInput::inputChanged, this, _1), BUTTON_DEBOUNCE_TIME, 0);
    // if active already when handler is installed and active report repeating requested -> start reporting now
    if (isSet() && repeatActiveReport!=Never) {
      // report for the first time and keep reporting
      repeatStateReport();
    }
  }
  else {
    // unregister
    setInputChangedHandler(NULL, 0, 0);
    activeReportTicket.cancel();
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
    activeReportTicket.executeOnce(boost::bind(&ButtonInput::repeatStateReport, this), repeatActiveReport);
  }
  else {
    // no longer active, cancel repeating active state if any
    activeReportTicket.cancel();
  }
}


void ButtonInput::repeatStateReport()
{
  if (buttonHandler) buttonHandler(true, false, MainLoop::now()-lastChangeTime);
  activeReportTicket.executeOnce(boost::bind(&ButtonInput::repeatStateReport, this), repeatActiveReport);
}



// MARK: ===== Indicator output

IndicatorOutput::IndicatorOutput(const char* aPinSpec, bool aInitiallyOn) :
  DigitalIo(aPinSpec, true, aInitiallyOn),
  blinkOnTime(Never),
  blinkOffTime(Never),
  blinkUntilTime(Never),
  nextTimedState(false)
{
}


IndicatorOutput::~IndicatorOutput()
{
  stop();
}


void IndicatorOutput::stop()
{
  blinkOnTime = Never;
  blinkOffTime = Never;
  blinkUntilTime = Never;
  timedOpTicket.cancel();
}


void IndicatorOutput::onFor(MLMicroSeconds aOnTime)
{
  stop();
  set(true);
  if (aOnTime>0) {
    nextTimedState = false; // ..turn off
    timedOpTicket.executeOnce(boost::bind(&IndicatorOutput::timer, this, _1), aOnTime); // ..after given time
  }
}


void IndicatorOutput::blinkFor(MLMicroSeconds aOnTime, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent)
{
  stop();
  blinkOnTime =  (aBlinkPeriod*aOnRatioPercent*10)/1000;
  blinkOffTime = aBlinkPeriod - blinkOnTime;
  blinkUntilTime = aOnTime>0 ? MainLoop::now()+aOnTime : Never;
  set(true); // ..start with on
  nextTimedState = false; // ..then turn off..
  timedOpTicket.executeOnce(boost::bind(&IndicatorOutput::timer, this, _1), blinkOnTime); // ..after blinkOn time
}


void IndicatorOutput::steady(bool aState)
{
  stop();
  set(aState);
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



void IndicatorOutput::timer(MLTimer &aTimer)
{
  // apply scheduled next state
  set(nextTimedState);
  // if we are blinking, check continuation
  if (blinkUntilTime!=Never && blinkUntilTime<MainLoop::now()) {
    // end of blinking, stop
    stop();
  }
  else if (blinkOnTime!=Never) {
    // blinking should continue
    nextTimedState = !nextTimedState;
    MainLoop::currentMainLoop().retriggerTimer(aTimer, nextTimedState ? blinkOffTime : blinkOnTime);
  }
}
