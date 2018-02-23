//
//  Copyright (c) 2014-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "analogio.hpp"

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
#if !DISABLE_PWM
#include "pwm.hpp"
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

AnalogIo::AnalogIo(const char* aPinSpec, bool aOutput, double aInitialValue)
{
  // save params
  output = aOutput;
  // check for inverting and pullup prefixes
  bool inverted = false; // not all analog outputs support this at all
  while (aPinSpec && *aPinSpec) {
    if (*aPinSpec=='/') inverted = true;
    else break; // none of the allowed prefixes -> done
    ++aPinSpec; // processed prefix -> check next
  }
  // rest is pin specification
  pinspec = aPinSpec;
  // check for missing pin (no pin, just silently keeping value)
  if (pinspec=="missing") {
    ioPin = AnalogIOPinPtr(new AnalogMissingPin(aInitialValue));
    return;
  }
  // dissect name into bus, device, pin
  string busName;
  string deviceName;
  string pinName;
  size_t i = pinspec.find(".");
  if (i==string::npos) {
    // no structured name, NOP
    return;
  }
  else {
    busName = pinspec.substr(0,i);
    // rest is device + pinname or just pinname
    pinName = pinspec.substr(i+1,string::npos);
    i = pinName.find(".");
    if (i!=string::npos) {
      // separate device and pin names
      // - extract device name
      deviceName = pinName.substr(0,i);
      // - remove device name from pin name string
      pinName.erase(0,i+1);
    }
  }
  // now create appropriate pin
  DBGLOG(LOG_DEBUG, "AnalogIo: bus name = '%s'", busName.c_str());
  #if !DISABLE_I2C
  if (busName.substr(0,3)=="i2c") {
    // i2c<busnum>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    ioPin = AnalogIOPinPtr(new AnalogI2CPin(busNumber, deviceName.c_str(), pinNumber, output, aInitialValue));
  }
  else
  #endif
  #if !DISABLE_SPI
  if (busName.substr(0,3)=="spi") {
    // spi<interfaceno*10+chipselno>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    ioPin = AnalogIOPinPtr(new AnalogSPIPin(busNumber, deviceName.c_str(), pinNumber, output, aInitialValue));
  }
  else
  #endif
  #if !DISABLE_SYSCMDIO
  if (busName=="syscmd") {
    // analog I/O calling system command to set value
    ioPin = AnalogIOPinPtr(new AnalogSysCommandPin(pinName.c_str(), output, aInitialValue));
  }
  else
  #endif
  if (busName.substr(0,7)=="pwmchip") {
    // Linux generic PWM output
    // pwmchip<chipno>.<channelno>[.<period>]
    int chipNumber = atoi(busName.c_str()+7);
    int channelNumber;
    uint32_t periodNs = 40000; // default to 25kHz = 40000nS
    if (deviceName.empty()) {
      channelNumber = atoi(pinName.c_str());
    }
    else {
      channelNumber = atoi(deviceName.c_str());
      periodNs = atoi(pinName.c_str());
    }
    ioPin = AnalogIOPinPtr(new PWMPin(chipNumber, channelNumber, inverted, aInitialValue, periodNs));
  }
  else {
    // all other/unknown bus names default to simulated pin
    ioPin = AnalogIOPinPtr(new AnalogSimPin(pinspec.c_str(), output, aInitialValue));
  }
}


AnalogIo::~AnalogIo()
{
}


double AnalogIo::value()
{
  return ioPin->getValue();
}


void AnalogIo::setValue(double aValue)
{
  ioPin->setValue(aValue);
}


bool AnalogIo::getRange(double &aMin, double &aMax, double &aResolution)
{
  return ioPin->getRange(aMin, aMax, aResolution);
}
