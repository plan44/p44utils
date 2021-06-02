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
#if !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)
  #if ENABLE_APPLICATION_SUPPORT
    #include "application.hpp" // we need it for user level, syscmd is only allowed with userlevel>=2
  #endif
  #ifndef ALWAYS_ALLOW_SYSCMDIO
    #define ALWAYS_ALLOW_SYSCMDIO 0
  #endif
#endif



using namespace p44;

AnalogIo::AnalogIo(const char* aPinSpec, bool aOutput, double aInitialValue)
{
  mLastValue = aInitialValue;
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
  #if !DISABLE_SYSCMDIO && !defined(ESP_PLATFORM) && (ENABLE_APPLICATION_SUPPORT || ALWAYS_ALLOW_SYSCMDIO)
  if (
    busName=="syscmd"
    #if !ALWAYS_ALLOW_SYSCMDIO
    && Application::sharedApplication()->userLevel()>=2
    #endif
  ) {
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
  mLastValue = ioPin->getValue();
  if (mWindowEvaluator) {
    mWindowEvaluator->addValue(mLastValue);
  }
  return mLastValue;
}


double AnalogIo::lastValue()
{
  return mLastValue;
}


double AnalogIo::processedValue()
{
  if (!mAutoPollTicket) value(); // not autopolling: update value (and add it to processor if enabled)
  if (mWindowEvaluator) return mWindowEvaluator->evaluate(); // processed value
  return mLastValue; // just last raw value
}


void AnalogIo::setFilter(WinEvalMode aEvalType, MLMicroSeconds aWindowTime, MLMicroSeconds aDataPointCollTime)
{
  mWindowEvaluator.reset();
  if (aEvalType==eval_none) return;
  mWindowEvaluator = WindowEvaluatorPtr(new WindowEvaluator(aWindowTime, aDataPointCollTime, aEvalType));
  value(); // cause initialisation
}


void AnalogIo::setAutopoll(MLMicroSeconds aPollInterval, MLMicroSeconds aTolerance, SimpleCB aPollCB)
{
  mAutoPollTicket.cancel();
  if (aPollInterval<=0) return; // disable polling
  mAutoPollTicket.executeOnce(boost::bind(&AnalogIo::pollhandler, this, aPollInterval, aTolerance, _1));
}


void AnalogIo::pollhandler(MLMicroSeconds aPollInterval, MLMicroSeconds aTolerance, MLTimer &aTimer)
{
  value(); // get (and possibly process) new value
  if (mPollCB) mPollCB();
  MainLoop::currentMainLoop().retriggerTimer(aTimer, aPollInterval, aTolerance);
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


ValueAnimatorPtr AnalogIo::animator()
{
  double startValue;
  ValueSetterCB valueSetter = getValueSetter(startValue);
  ValueAnimatorPtr animator = ValueAnimatorPtr(new ValueAnimator(valueSetter, true)); // self-timed
  return animator->from(startValue);
}



#if ENABLE_ANALOGIO_COLOR_SUPPORT

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


ValueAnimatorPtr AnalogColorOutput::animatorFor(const string aComponent)
{
  double startValue;
  ValueSetterCB valueSetter = getColorComponentSetter(aComponent, startValue);
  ValueAnimatorPtr animator = ValueAnimatorPtr(new ValueAnimator(valueSetter, true)); // self-timed
  return animator->from(startValue);
}


#endif // ENABLE_ANALOGIO_COLOR_SUPPORT

// MARK: - script support

#if ENABLE_ANALOGIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

using namespace P44Script;

AnalogInputEventObj::AnalogInputEventObj(AnalogIoObj* aAnalogIoObj) :
  inherited(0),
  mAnalogIoObj(aAnalogIoObj)
{
}


string AnalogInputEventObj::getAnnotation() const
{
  return "analog input value";
}


TypeInfo AnalogInputEventObj::getTypeInfo() const
{
  return inherited::getTypeInfo()|oneshot|keeporiginal; // returns the request only once, must keep the original
}


EventSource* AnalogInputEventObj::eventSource() const
{
  return static_cast<EventSource*>(mAnalogIoObj);
}


double AnalogInputEventObj::doubleValue() const
{
  return mAnalogIoObj && mAnalogIoObj->analogIo()->processedValue();
}




// range()
static void range_func(BuiltinFunctionContextPtr f)
{
  AnalogIoObj* a = dynamic_cast<AnalogIoObj*>(f->thisObj().get());
  assert(a);
  // return range
  double min;
  double max;
  double res;
  a->analogIo()->getRange(min, max, res);
  JsonObjectPtr j = JsonObject::newObj();
  j->add("min", JsonObject::newDouble(min));
  j->add("max", JsonObject::newDouble(max));
  j->add("resolution", JsonObject::newDouble(res));
  f->finish(new JsonValue(j));
}


// animator()
static void animator_func(BuiltinFunctionContextPtr f)
{
  AnalogIoObj* a = dynamic_cast<AnalogIoObj*>(f->thisObj().get());
  assert(a);
  f->finish(new ValueAnimatorObj(a->analogIo()->animator()));
}



// value() // get value
// value(val) // set value
static const BuiltInArgDesc value_args[] = { { numeric|optionalarg } };
static const size_t value_numargs = sizeof(value_args)/sizeof(BuiltInArgDesc);
static void value_func(BuiltinFunctionContextPtr f)
{
  AnalogIoObj* a = dynamic_cast<AnalogIoObj*>(f->thisObj().get());
  assert(a);
  if (f->numArgs()>0) {
    // set new analog value
    a->analogIo()->setValue(f->arg(0)->doubleValue());
    f->finish();
  }
  else {
    // return current value as triggerable event
    f->finish(new AnalogInputEventObj(a));
  }
}


// poll(interval [, tolerance])
// poll()
static const BuiltInArgDesc poll_args[] = { { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t poll_numargs = sizeof(poll_args)/sizeof(BuiltInArgDesc);
static void poll_func(BuiltinFunctionContextPtr f)
{
  AnalogIoObj* a = dynamic_cast<AnalogIoObj*>(f->thisObj().get());
  assert(a);
  if (f->arg(0)->doubleValue()<=0) {
    // null, undefined, <=0 cancels polling
    a->analogIo()->setAutopoll(0);
  }
  else {
    MLMicroSeconds interval = f->arg(0)->doubleValue()*Second;
    MLMicroSeconds tolerance = 0;
    if (f->numArgs()>=1) tolerance = f->arg(0)->doubleValue()*Second;
    a->analogIo()->setAutopoll(interval, tolerance, boost::bind(&AnalogIoObj::valueUpdated, a));
  }
  f->finish();
}

void AnalogIoObj::valueUpdated()
{
  if (hasSinks()) {
    // Note: we do not actually read the value here, this will happen only when value() is called
    //   as a consequence of this event being sent
    sendEvent(new AnalogInputEventObj(this));
  }
}


// filter(type, [interval [, colltime]])
static const BuiltInArgDesc filter_args[] = { { text }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t filter_numargs = sizeof(filter_args)/sizeof(BuiltInArgDesc);
static void filter_func(BuiltinFunctionContextPtr f)
{
  AnalogIoObj* a = dynamic_cast<AnalogIoObj*>(f->thisObj().get());
  assert(a);
  string ty = f->arg(0)->stringValue();
  WinEvalMode ety = eval_none;
  if (strucmp(ty.c_str(), "abs-", 4)==0) {
    ety |= eval_option_abs;
  }
  if (uequals(ty,"average")) ety |= eval_timeweighted_average;
  else if (uequals(ty,"simpleaverage")) ety |= eval_average;
  else if (uequals(ty,"min")) ety |= eval_min;
  else if (uequals(ty,"max")) ety |= eval_max;
  MLMicroSeconds windowtime = 10*Second; // default to 10 second processing window
  if (f->arg(1)->defined()) windowtime = f->arg(1)->doubleValue()*Second;
  MLMicroSeconds colltime = windowtime/20; // default to 1/20 of the processing window
  if (f->arg(2)->defined()) colltime = f->arg(2)->doubleValue()*Second;
  a->analogIo()->setFilter(ety, windowtime, colltime);
  f->finish();
}


static const BuiltinMemberDescriptor analogioFunctions[] = {
  { "value", executable|numeric, value_numargs, value_args, &value_func },
  { "range", executable|object, 0, NULL, &range_func },
  { "animator", executable|object, 0, NULL, &animator_func },
  { "poll", executable|null, poll_numargs, poll_args, &poll_func },
  { "filter", executable|null, filter_numargs, filter_args, &filter_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedAnalogIoFunctionLookupP = NULL;

AnalogIoObj::AnalogIoObj(AnalogIoPtr aAnalogIo) :
  mAnalogIo(aAnalogIo)
{
  registerSharedLookup(sharedAnalogIoFunctionLookupP, analogioFunctions);
}


AnalogIoPtr AnalogIoObj::analogIoFromArg(ScriptObjPtr aArg, bool aOutput, double aInitialValue)
{
  AnalogIoPtr aio;
  AnalogIoObj* a = dynamic_cast<AnalogIoObj*>(aArg.get());
  if (a) {
    aio = a->analogIo();
  }
  else if (aArg->hasType(text)) {
    aio = AnalogIoPtr(new AnalogIo(aArg->stringValue().c_str(), aOutput, aInitialValue));
  }
  return aio;
}



// analogio(pinspec, isOutput [, initialValue])
static const BuiltInArgDesc analogio_args[] = { { text }, { numeric }, { numeric|optionalarg } };
static const size_t analogio_numargs = sizeof(analogio_args)/sizeof(BuiltInArgDesc);
static void analogio_func(BuiltinFunctionContextPtr f)
{
  bool out = f->arg(1)->boolValue();
  double v = 0;
  if (f->arg(2)->defined()) v = f->arg(2)->doubleValue();
  AnalogIoPtr analogio = new AnalogIo(f->arg(0)->stringValue().c_str(), out, v);
  f->finish(new AnalogIoObj(analogio));
}


#if ENABLE_ANALOGIO_COLOR_SUPPORT

// animator(property)
static const BuiltInArgDesc animatorfor_args[] = { { text } };
static const size_t animatorfor_numargs = sizeof(animatorfor_args)/sizeof(BuiltInArgDesc);
static void animatorfor_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  f->finish(new ValueAnimatorObj(c->colorOutput()->animatorFor(f->arg(0)->stringValue())));
}


// setcolor(hue, saturation)
// setcolor(webcolor)
static const BuiltInArgDesc setcolor_args[] = { { text|numeric }, { numeric|optionalarg } };
static const size_t setcolor_numargs = sizeof(setcolor_args)/sizeof(BuiltInArgDesc);
static void setcolor_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  if (f->numArgs()<2) {
    // set color via web color
    PixelColor col = webColorToPixel(f->arg(0)->stringValue());
    Row3 rgb;
    pixelToRGB(col, rgb);
    c->colorOutput()->setRGB(rgb);
  }
  else {
    c->colorOutput()->setColor(f->arg(0)->doubleValue(), f->arg(1)->doubleValue());
  }
  f->finish();
}


// setbrightness(brightness)
static const BuiltInArgDesc setbrightness_args[] = { { numeric } };
static const size_t setbrightness_numargs = sizeof(setbrightness_args)/sizeof(BuiltInArgDesc);
static void setbrightness_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  c->colorOutput()->setBrightness(f->arg(0)->doubleValue());
  f->finish();
}


// powerlimit(brightness)
static const BuiltInArgDesc powerlimit_args[] = { { numeric|optionalarg } };
static const size_t powerlimit_numargs = sizeof(powerlimit_args)/sizeof(BuiltInArgDesc);
static void powerlimit_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  if (f->numArgs()==0) {
    f->finish(new NumericValue(c->colorOutput()->getPowerLimit()));
  }
  else {
    c->colorOutput()->setPowerLimit(f->arg(0)->intValue());
    f->finish();
  }
}


// neededpower()
static void neededpower_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  f->finish(new NumericValue(c->colorOutput()->getNeededPower()));
}


// currentpower()
static void currentpower_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  f->finish(new NumericValue(c->colorOutput()->getCurrentPower()));
}


// whitecolor(pixelcolor)
// ambercolor(pixelcolor)
static const BuiltInArgDesc chcolor_args[] = { { text } };
static const size_t chcolor_numargs = sizeof(chcolor_args)/sizeof(BuiltInArgDesc);
static void whitecolor_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  PixelColor col = webColorToPixel(f->arg(0)->stringValue());
  pixelToRGB(col, c->colorOutput()->whiteRGB);
  f->finish();
}
static void ambercolor_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  PixelColor col = webColorToPixel(f->arg(0)->stringValue());
  pixelToRGB(col, c->colorOutput()->amberRGB);
  f->finish();
}


// setoutputchannelpower(milliwatt) // for all channels
// setoutputchannelpower(r,g,b [,w [,a]]) // separately
static const BuiltInArgDesc setoutputchannelpower_args[] = { { numeric }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg }};
static const size_t setoutputchannelpower_numargs = sizeof(setoutputchannelpower_args)/sizeof(BuiltInArgDesc);
static void setoutputchannelpower_func(BuiltinFunctionContextPtr f)
{
  AnalogColorOutputObj* c = dynamic_cast<AnalogColorOutputObj*>(f->thisObj().get());
  assert(c);
  if (f->numArgs()==1) {
    for (int i=0; i<5; i++) c->colorOutput()->mOutputMilliWatts[i] = f->arg(0)->intValue();
  }
  else {
    for (int i=0; i<f->numArgs(); i++) {
      c->colorOutput()->mOutputMilliWatts[i] = f->arg(i)->intValue();
    }
  }
  f->finish();
}


static const BuiltinMemberDescriptor coloroutputFunctions[] = {
  { "animator", executable|object, animatorfor_numargs, animatorfor_args, &animatorfor_func },
  { "setcolor", executable|null, setcolor_numargs, setcolor_args, &setcolor_func },
  { "setbrightness", executable|null, setbrightness_numargs, setbrightness_args, &setbrightness_func },
  { "powerlimit", executable|numeric|null, powerlimit_numargs, powerlimit_args, &powerlimit_func },
  { "neededpower", executable|numeric, NULL, 0, &neededpower_func },
  { "currentpower", executable|numeric, NULL, 0, &currentpower_func },
  { "whitecolor", executable|null, chcolor_numargs, chcolor_args, &whitecolor_func },
  { "ambercolor", executable|null, chcolor_numargs, chcolor_args, &ambercolor_func },
  { "setoutputchannelpower", executable|null, setoutputchannelpower_numargs, setoutputchannelpower_args, &setoutputchannelpower_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedColorOutputFunctionLookupP = NULL;

AnalogColorOutputObj::AnalogColorOutputObj(AnalogColorOutputPtr aColorOutput) :
  mColorOutput(aColorOutput)
{
  registerSharedLookup(sharedColorOutputFunctionLookupP, coloroutputFunctions);
}


// analogcoloroutput(red, green, blue [[, white [, amber]) // AnalogIOObjs or pin specs
static const BuiltInArgDesc coloroutput_args[] = { { text|object }, { text|object }, { text|object }, { text|optionalarg }, { text|optionalarg } };
static const size_t coloroutput_numargs = sizeof(coloroutput_args)/sizeof(BuiltInArgDesc);
static void coloroutput_func(BuiltinFunctionContextPtr f)
{
  AnalogIoPtr red = AnalogIoObj::analogIoFromArg(f->arg(0), true, 0);
  AnalogIoPtr green = AnalogIoObj::analogIoFromArg(f->arg(1), true, 0);
  AnalogIoPtr blue = AnalogIoObj::analogIoFromArg(f->arg(2), true, 0);
  AnalogIoPtr white;
  AnalogIoPtr amber;
  if (f->arg(3)->defined()) white = AnalogIoObj::analogIoFromArg(f->arg(3), true, 0);
  if (f->arg(4)->defined()) amber = AnalogIoObj::analogIoFromArg(f->arg(4), true, 0);
  AnalogColorOutputPtr colorOutput = new AnalogColorOutput(red, green, blue, white, amber);
  f->finish(new AnalogColorOutputObj(colorOutput));
}

#endif // ENABLE_ANALOGIO_COLOR_SUPPORT



static const BuiltinMemberDescriptor analogioGlobals[] = {
  { "analogio", executable|null, analogio_numargs, analogio_args, &analogio_func },
  #if ENABLE_ANALOGIO_COLOR_SUPPORT
  { "analogcoloroutput", executable|null, coloroutput_numargs, coloroutput_args, &coloroutput_func },
  #endif
  { NULL } // terminator
};

AnalogIoLookup::AnalogIoLookup() :
  inherited(analogioGlobals)
{
}

#endif // ENABLE_ANALOGIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
