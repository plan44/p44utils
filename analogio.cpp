//
//  Copyright (c) 2014-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#if ENABLE_APPLICATION_SUPPORT && !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)
#include "application.hpp" // we need it for user level, syscmd is only allowed with userlevel>=2
#endif


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
  #if ENABLE_APPLICATION_SUPPORT && !DISABLE_SYSCMDIO && !defined(ESP_PLATFORM)
  if (busName=="syscmd" && Application::sharedApplication()->userLevel()>=2) {
    // analog I/O calling system command to set value
    ioPin = AnalogIOPinPtr(new AnalogSysCommandPin(pinName.c_str(), output, aInitialValue));
  }
  else
  #endif
  #if !DISABLE_PWM
  if (busName.substr(0,7)=="pwmchip") {
    // Linux generic PWM output
    //   pwmchip<chipno>.<channelno>[.<period>]
    // or ESP32 ledc PWM output
    //   pwmchip<gpiono>.<channelno>[.<period>]
    int chipNumber = atoi(busName.c_str()+7);
    int channelNumber;
    uint32_t periodNs = 0; // default
    if (deviceName.empty()) {
      channelNumber = atoi(pinName.c_str());
    }
    else {
      channelNumber = atoi(deviceName.c_str());
      periodNs = atoi(pinName.c_str());
    }
    ioPin = AnalogIOPinPtr(new PWMPin(chipNumber, channelNumber, inverted, aInitialValue, periodNs));
  }
  else
  #endif
  if (busName=="fdsim") {
    // analog I/O from file descriptor (should be non-blocking or at least minimal-delay files such
    // as quickly served pipes or /sys/class/* files)
    ioPin = AnalogIOPinPtr(new AnalogSimPinFd(pinName.c_str(), output, aInitialValue));
  }
  else {
    // all other/unknown bus names, including "sim", default to simulated pin operated from console
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


/// get value setter for animations
ValueSetterCB AnalogIo::getValueSetter(double& aCurrentValue)
{
  aCurrentValue = value();
  return boost::bind(&AnalogIo::setValue, this, _1);
}


// MARK: - AnalogColorOutput

AnalogColorOutput::AnalogColorOutput(AnalogIoPtr aRedOutput, AnalogIoPtr aGreenOutput, AnalogIoPtr aBlueOutput, AnalogIoPtr aWhiteOutput, AnalogIoPtr aAmberOutput) :
  mMaxMilliWatts(0), // no power limit
  mRequestedMilliWatts(0)
{
  mRGBWAOutputs[0] = aRedOutput;
  mRGBWAOutputs[1] = aGreenOutput;
  mRGBWAOutputs[2] = aBlueOutput;
  mRGBWAOutputs[3] = aWhiteOutput;
  mRGBWAOutputs[4] = aAmberOutput;
  mHSV[0] = 0;
  mHSV[1] = 0;
  mHSV[2] = 0;
  // default white assumed to contribute equally to R,G,B with 35% each
  whiteRGB[0] = 0.35; whiteRGB[1] = 0.35; whiteRGB[2] = 0.35;
  // default amber assumed to be AMBER web color #FFBE00 = 100%, 75%, 0% contributing 50% intensity
  amberRGB[0] = 0.5; amberRGB[1] = 0.375; amberRGB[2] = 0;
  // assume same consumption on all channels, one Watt each
  for (int i=0; i<5; i++) mOutputMilliWatts[i] = 1;
}



void AnalogColorOutput::setHSV(const Row3 &aHSV)
{
  mHSV[0] = aHSV[0];
  mHSV[1] = aHSV[1];
  mHSV[2] = aHSV[2];
  outputHSV();
}


void AnalogColorOutput::setColor(double aHue, double aSaturation)
{
  mHSV[0] = aHue;
  mHSV[1] = aSaturation;
  outputHSV();
}


void AnalogColorOutput::setBrightness(double aBrightness)
{
  mHSV[2] = aBrightness;
  outputHSV();
}


void AnalogColorOutput::setPowerLimit(int aMilliWatts)
{
  if (aMilliWatts!=mMaxMilliWatts) {
    mMaxMilliWatts = aMilliWatts;
    outputRGB(); // re-output with new limit applied
  }
}


int AnalogColorOutput::getPowerLimit()
{
  return mMaxMilliWatts;
}


int AnalogColorOutput::getNeededPower()
{
  return mRequestedMilliWatts;
}


int AnalogColorOutput::getCurrentPower()
{
  if (mMaxMilliWatts<=0 || mRequestedMilliWatts<mMaxMilliWatts)
    return mRequestedMilliWatts;
  return mMaxMilliWatts; // at the limit
}



inline static void setOutputIntensity(AnalogIoPtr &aOutput, double aIntensity)
{
  if (!aOutput) return;
  double min,max,res;
  if (!aOutput->getRange(min, max, res)) max = 100; // assume 0..100 when output does not provide a range
  aOutput->setValue(max*aIntensity);
}


void AnalogColorOutput::setRGB(const Row3 &aRGB)
{
  mRGB[0] = aRGB[0];
  mRGB[1] = aRGB[1];
  mRGB[2] = aRGB[2];
  outputRGB();
}



void AnalogColorOutput::outputHSV()
{
  HSVtoRGB(mHSV, mRGB);
  outputRGB();
}


void AnalogColorOutput::outputRGB()
{
  double r = mRGB[0];
  double g = mRGB[1];
  double b = mRGB[2];
  double w = 0;
  double a = 0;
  mRequestedMilliWatts = 0;
  if (mRGBWAOutputs[3]) {
    // there is a white channel
    double w = transferToColor(whiteRGB, r, g, b);
    if (w<0) w=0; else if (w>1) w=1;
    mRequestedMilliWatts += w*mOutputMilliWatts[3];
    if (mRGBWAOutputs[4]) {
      // there is a amber channel
      double a = transferToColor(amberRGB, r, g, b);
      if (a<0) a=0; else if (a>1) a=1;
      mRequestedMilliWatts += a*mOutputMilliWatts[3];
    }
  }
  if (r<0) r=0; else if (r>1) r=1;
  if (g<0) g=0; else if (g>1) g=1;
  if (b<0) b=0; else if (b>1) b=1;
  mRequestedMilliWatts += r*mOutputMilliWatts[0];
  mRequestedMilliWatts += g*mOutputMilliWatts[1];
  mRequestedMilliWatts += b*mOutputMilliWatts[2];
  double factor = 1;
  if (mMaxMilliWatts>0 && mRequestedMilliWatts>mMaxMilliWatts) {
    factor = (double)mMaxMilliWatts/mRequestedMilliWatts; // reduce
  }
  // apply to channels
  setOutputIntensity(mRGBWAOutputs[0], factor*r);
  setOutputIntensity(mRGBWAOutputs[1], factor*g);
  setOutputIntensity(mRGBWAOutputs[2], factor*b);
  setOutputIntensity(mRGBWAOutputs[3], factor*w);
  setOutputIntensity(mRGBWAOutputs[4], factor*a);
}


ValueSetterCB AnalogColorOutput::getColorComponentSetter(const string aComponent, double &aCurrentValue)
{
  if (aComponent=="hue") {
    return getHsvComponentSetter(mHSV[0], aCurrentValue);
  }
  else if (aComponent=="saturation") {
    return getHsvComponentSetter(mHSV[1], aCurrentValue);
  }
  else if (aComponent=="brightness") {
    return getHsvComponentSetter(mHSV[2], aCurrentValue);
  }
  else if (aComponent=="r") {
    return getRgbComponentSetter(mRGB[0], aCurrentValue);
  }
  else if (aComponent=="g") {
    return getRgbComponentSetter(mRGB[1], aCurrentValue);
  }
  else if (aComponent=="b") {
    return getRgbComponentSetter(mRGB[2], aCurrentValue);
  }
  return NULL;
}


ValueSetterCB AnalogColorOutput::getHsvComponentSetter(double &aColorComponent, double &aCurrentValue)
{
  aCurrentValue = aColorComponent;
  return boost::bind(&AnalogColorOutput::hsvComponentSetter, this, &aColorComponent, _1);
}

void AnalogColorOutput::hsvComponentSetter(double* aColorComponentP, double aNewValue)
{
  *aColorComponentP = aNewValue;
  outputHSV();
}


ValueSetterCB AnalogColorOutput::getRgbComponentSetter(double &aColorComponent, double &aCurrentValue)
{
  aCurrentValue = aColorComponent;
  return boost::bind(&AnalogColorOutput::rgbComponentSetter, this, &aColorComponent, _1);
}

void AnalogColorOutput::rgbComponentSetter(double* aColorComponentP, double aNewValue)
{
  *aColorComponentP = aNewValue;
  outputRGB();
}
