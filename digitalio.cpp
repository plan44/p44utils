//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#if (!DISABLE_SYSTEMCMDIO || ENABLE_DIGITALIO_SCRIPT_FUNCS) && !defined(ESP_PLATFORM)
  #if ENABLE_APPLICATION_SUPPORT
    #include "application.hpp" // we need it for user level, syscmd is only allowed with userlevel>=2
  #endif
  #ifndef ALWAYS_ALLOW_SYSCMDIO
    #define ALWAYS_ALLOW_SYSCMDIO 0
  #endif
#endif


using namespace p44;


DigitalIo::DigitalIo(const char* aPinSpec, bool aOutput, bool aInitialState) :
  mInverted(false),
  mPull(undefined)
{
  // save params
  mOutput = aOutput;
  // check for inverting and pullup prefixes
  while (aPinSpec && *aPinSpec) {
    if (*aPinSpec=='/') mInverted = true;
    else if (*aPinSpec=='+') mPull = yes; // pullup
    else if (*aPinSpec=='-') mPull = no; // pulldown
    else break; // none of the allowed prefixes -> done
    ++aPinSpec; // processed prefix -> check next
  }
  // rest is actual pin specification
  mPinSpec = nonNullCStr(aPinSpec);
  bool initialPinState = aInitialState!=mInverted;
  // check for missing pin (no pin, just silently keeping state)
  if (mPinSpec.size()==0 || mPinSpec=="missing") {
    mIoPin = IOPinPtr(new MissingPin(initialPinState));
    return;
  }
  // dissect name into bus, device, pin
  string busName;
  string deviceName;
  string pinName;
  size_t i = mPinSpec.find(".");
  if (i==string::npos) {
    // just a bus name, device and pin remain empty
    busName = mPinSpec;
  }
  else {
    busName = mPinSpec.substr(0,i);
    // rest is device + pinname or just pinname
    pinName = mPinSpec.substr(i+1,string::npos);
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
    // Linux or ESP32 generic GPIO
    // gpio.<gpionumber>
    int pinNumber = atoi(pinName.c_str());
    mIoPin = IOPinPtr(new GpioPin(pinNumber, mOutput, initialPinState, mPull));
  }
  #ifndef ESP_PLATFORM
  else if (busName=="led") {
    // Linux generic LED
    // led.<lednumber_or_name>
    mIoPin = IOPinPtr(new GpioLedPin(pinName.c_str(), initialPinState));
  }
  #endif // !ESP_PLATFORM
  else
  #endif
  #if P44_BUILD_DIGI && !DISABLE_GPIO
  if (busName=="gpioNS9XXXX") {
    // gpioNS9XXXX.<pinname>
    // NS9XXX driver based GPIO (Digi ME 9210 LX)
    mIoPin = IOPinPtr(new GpioNS9XXXPin(pinName.c_str(), mOutput, initialPinState));
  }
  else
  #endif
  #if !DISABLE_I2C
  if (busName.substr(0,3)=="i2c") {
    // i2c<busnum>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    mIoPin = IOPinPtr(new I2CPin(busNumber, deviceName.c_str(), pinNumber, mOutput, initialPinState, mPull));
  }
  else
  #endif
  #if !DISABLE_SPI
  if (busName.substr(0,3)=="spi") {
    // spi<interfaceno*10+chipselno>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    mIoPin = IOPinPtr(new SPIPin(busNumber, deviceName.c_str(), pinNumber, mOutput, initialPinState, mPull));
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
    // digital I/O calling system command to turn on/off
    mIoPin = IOPinPtr(new SysCommandPin(pinName.c_str(), mOutput, initialPinState));
  }
  else
  #endif
  {
    // all other/unknown bus names, including "sim", default to simulated pin operated from console
    mIoPin = IOPinPtr(new SimPin(mPinSpec.c_str(), mOutput, initialPinState));
  }
}


DigitalIo::~DigitalIo()
{
}


string DigitalIo::getName() const
{
  return string_format("%s%s%s", mPull==yes ? "+" : (mPull==no ? "-" : ""), mInverted ? "/" : "", mPinSpec.c_str());
}



bool DigitalIo::isSet()
{
  return mIoPin->getState() != mInverted;
}


void DigitalIo::set(bool aState)
{
  mIoPin->setState(aState!=mInverted);
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
  if (mOutput) {
    state = !state;
    set(state);
  }
  return state;
}


bool DigitalIo::setInputChangedHandler(InputChangedCB aInputChangedCB, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  // enable or disable reporting changes via callback
  return mIoPin->setInputChangedHandler(aInputChangedCB, mInverted, mIoPin->getState(), aDebounceTime, aPollInterval);
}


#if ENABLE_DIGITALIO_SCRIPT_FUNCS && ENABLE_P44SCRIPT

/// get a analog input value object. This is also what is sent to event sinks
P44Script::ScriptObjPtr DigitalIo::getStateObj()
{
  return new P44Script::DigitalInputEventObj(this);
}


bool DigitalIo::setChangeDetection(MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  if (aDebounceTime<0) {
    // disable
    return mIoPin->setInputChangedHandler(NoOP, mInverted, false, 0, 0);
  }
  else {
    // enable
    return mIoPin->setInputChangedHandler(boost::bind(&DigitalIo::processChange, this, _1), mInverted, mIoPin->getState(), aDebounceTime, aPollInterval);
  }
}


void DigitalIo::processChange(bool aNewState)
{
  if (hasSinks()) {
    sendEvent(getStateObj());
  }
}

#endif // ENABLE_DIGITALIO_SCRIPT_FUNCS && ENABLE_P44SCRIPT


#if !REDUCED_FOOTPRINT
// MARK: - DigitalIoBus

DigitalIoBus::DigitalIoBus(const char* aBusPinSpecs, int aNumBits, bool aOutputs, bool aInitialStates) :
  mOutputs(aOutputs)
{
  string prefix;
  string spec;
  while (nextPart(aBusPinSpecs, spec, ',') && mBusPins.size()<aNumBits) {
    if (spec.empty()) {
      spec="missing";
    }
    else if (spec[0]=='(') {
      // (prefix) can be used to define multiple similar pins, such as "(gpio.)1,2,3,4,(i2c0.MCP23017@24.)6,7"
      size_t e = spec.find(")", 1);
      if (e!=string::npos) {
        prefix = spec.substr(1,e-1);
        spec.erase(0,e+1);
      }
    }
    string pinspec = prefix+spec;
    DigitalIoPtr newIO = DigitalIoPtr(new DigitalIo(pinspec.c_str(), mOutputs, aInitialStates));
    mBusPins.insert(mBusPins.begin(), newIO);
  }
  // calculate initial value
  mCurrentValue = 0;
  for (int i=0; i<mBusPins.size(); i++) {
    mCurrentValue = (mCurrentValue<<1)|(aInitialStates ? 1 : 0);
  }
}


DigitalIoBus::~DigitalIoBus()
{
}


uint32_t DigitalIoBus::getBusValue()
{
  if (!mOutputs) {
    // actually read
    mCurrentValue = 0;
    for (BusPinVector::iterator pos=mBusPins.begin(); pos<mBusPins.end(); ++pos) {
      mCurrentValue = (mCurrentValue<<1)|((*pos)->isSet() ? 1 : 0);
    }
  }
  return mCurrentValue;
}


uint32_t DigitalIoBus::getMaxBusValue()
{
  return (1<<mBusPins.size())-1;
}


uint8_t DigitalIoBus::getBusWidth()
{
  return (uint8_t)mBusPins.size();
}


void DigitalIoBus::setBusValue(uint32_t aBusValue)
{
  if (mOutputs && aBusValue!=mCurrentValue) {
    uint32_t m = 0x01;
    for (BusPinVector::iterator pos=mBusPins.begin(); pos<mBusPins.end(); ++pos) {
      bool sta = (aBusValue & m)!=0;
      if (sta != ((mCurrentValue & m)!=0)) {
        // bit has changed, apply
        (*pos)->set(sta);
      }
      m <<= 1;
    }
    mCurrentValue = aBusValue;
  }
}

#endif // !REDUCED_FOOTPRINT

// MARK: - Button input

#define BUTTON_DEBOUNCE_TIME (80*MilliSecond)

ButtonInput::ButtonInput(const char* aPinSpec) :
  DigitalIo(aPinSpec, false, false),
  mRepeatActiveReport(Never)
{
  mLastChangeTime = MainLoop::now();
}


ButtonInput::~ButtonInput()
{
  mActiveReportTicket.cancel();
}


void ButtonInput::setButtonHandler(ButtonHandlerCB aButtonHandler, bool aPressAndRelease, MLMicroSeconds aRepeatActiveReport)
{
  mReportPressAndRelease = aPressAndRelease;
  mRepeatActiveReport = aRepeatActiveReport;
  mButtonHandler = aButtonHandler;
  if (mButtonHandler) {
    // mainloop idle polling if input does not support edge detection
    setInputChangedHandler(boost::bind(&ButtonInput::inputChanged, this, _1), BUTTON_DEBOUNCE_TIME, 0);
    // if active already when handler is installed and active report repeating requested -> start reporting now
    if (isSet() && mRepeatActiveReport!=Never) {
      // report for the first time and keep reporting
      repeatStateReport();
    }
  }
  else {
    // unregister
    setInputChangedHandler(NoOP, 0, 0);
    mActiveReportTicket.cancel();
  }
}



void ButtonInput::inputChanged(bool aNewState)
{
  MLMicroSeconds now = MainLoop::now();
  if (!aNewState || mReportPressAndRelease) {
    mButtonHandler(aNewState, true, now-mLastChangeTime);
  }
  // consider this a state change
  mLastChangeTime = now;
  // active state reported now
  if (aNewState && mRepeatActiveReport!=Never) {
    mActiveReportTicket.executeOnce(boost::bind(&ButtonInput::repeatStateReport, this), mRepeatActiveReport);
  }
  else {
    // no longer active, cancel repeating active state if any
    mActiveReportTicket.cancel();
  }
}


void ButtonInput::repeatStateReport()
{
  if (mButtonHandler) mButtonHandler(true, false, MainLoop::now()-mLastChangeTime);
  mActiveReportTicket.executeOnce(boost::bind(&ButtonInput::repeatStateReport, this), mRepeatActiveReport);
}



// MARK: - Indicator output

IndicatorOutput::IndicatorOutput(const char* aPinSpec, bool aInitiallyOn) :
  DigitalIo(aPinSpec, true, aInitiallyOn),
  mBlinkOnTime(Never),
  mBlinkOffTime(Never),
  mBlinkUntilTime(Never),
  mNextTimedState(false)
{
}


IndicatorOutput::~IndicatorOutput()
{
  stop();
}


void IndicatorOutput::stop()
{
  mBlinkOnTime = Never;
  mBlinkOffTime = Never;
  mBlinkUntilTime = Never;
  mTimedOpTicket.cancel();
}


void IndicatorOutput::onFor(MLMicroSeconds aOnTime)
{
  stop();
  set(true);
  if (aOnTime>0) {
    mNextTimedState = false; // ..turn off
    mTimedOpTicket.executeOnce(boost::bind(&IndicatorOutput::timer, this, _1), aOnTime); // ..after given time
  }
}


void IndicatorOutput::blinkFor(MLMicroSeconds aOnTime, MLMicroSeconds aBlinkPeriod, int aOnRatioPercent)
{
  stop();
  mBlinkOnTime =  (aBlinkPeriod*aOnRatioPercent*10)/1000;
  mBlinkOffTime = aBlinkPeriod - mBlinkOnTime;
  mBlinkUntilTime = aOnTime>0 ? MainLoop::now()+aOnTime : Never;
  set(true); // ..start with on
  mNextTimedState = false; // ..then turn off..
  mTimedOpTicket.executeOnce(boost::bind(&IndicatorOutput::timer, this, _1), mBlinkOnTime); // ..after blinkOn time
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
  set(mNextTimedState);
  // if we are blinking, check continuation
  if (mBlinkUntilTime!=Never && mBlinkUntilTime<MainLoop::now()) {
    // end of blinking, stop
    stop();
  }
  else if (mBlinkOnTime!=Never) {
    // blinking should continue
    mNextTimedState = !mNextTimedState;
    MainLoop::currentMainLoop().retriggerTimer(aTimer, mNextTimedState ? mBlinkOffTime : mBlinkOnTime);
  }
}


// MARK: - script support

#if ENABLE_DIGITALIO_SCRIPT_FUNCS && ENABLE_P44SCRIPT

#if !ENABLE_APPLICATION_SUPPORT
  #warning "Unconditionally allowing I/O creation (no userlevel check)"
#endif


using namespace P44Script;

DigitalInputEventObj::DigitalInputEventObj(DigitalIoPtr aDigitalIo) :
  inherited(false),
  mDigitalIo(aDigitalIo)
{
  if (mDigitalIo) mNum = mDigitalIo->isSet() ? 1 : 0;
}


void DigitalInputEventObj::deactivate()
{
  mDigitalIo.reset();
  inherited::deactivate();
}


TypeInfo DigitalInputEventObj::getTypeInfo() const
{
  return inherited::getTypeInfo()|freezable; // can be frozen
}


string DigitalInputEventObj::getAnnotation() const
{
  return "input event";
}


bool DigitalInputEventObj::isEventSource() const
{
  return mDigitalIo.get(); // yes if it exists
}


void DigitalInputEventObj::registerForFilteredEvents(EventSink* aEventSink, intptr_t aRegId)
{
  if (mDigitalIo) mDigitalIo->registerForEvents(aEventSink, aRegId); // no filtering
}


// detectchanges([debouncetime [, pollinterval]])
// detectchanges(null)
FUNC_ARG_DEFS(detectchanges, { numeric|optionalarg }, { numeric|optionalarg } );
static void detectchanges_func(BuiltinFunctionContextPtr f)
{
  DigitalIoObj* d = dynamic_cast<DigitalIoObj*>(f->thisObj().get());
  assert(d);
  MLMicroSeconds debounce = 0; // no debouncing by default
  MLMicroSeconds pollinterval = 0; // default polling interval (or no polling if pin has edge detection)
  if (f->numArgs()==1 && f->arg(0)->undefined()) {
    // stop change detection/polling
    d->digitalIo()->setChangeDetection();
    f->finish();
  }
  else {
    // start polling and debouncing
    if (f->arg(0)->defined()) debounce = f->arg(0)->doubleValue()*Second;
    if (f->arg(1)->defined()) pollinterval = f->arg(0)->doubleValue()*Second;
    bool works = d->digitalIo()->setChangeDetection(debounce, pollinterval);
    f->finish(new BoolValue(works));
  }
}


// toggle()
static void toggle_func(BuiltinFunctionContextPtr f)
{
  DigitalIoObj* d = dynamic_cast<DigitalIoObj*>(f->thisObj().get());
  assert(d);
  d->digitalIo()->toggle();
  f->finish();
}


// state() // get state (has event source)
// state(newstate) // set state
FUNC_ARG_DEFS(state, { numeric|optionalarg } );
static void state_func(BuiltinFunctionContextPtr f)
{
  DigitalIoObj* d = dynamic_cast<DigitalIoObj*>(f->thisObj().get());
  assert(d);
  if (f->numArgs()>0) {
    // set new state
    d->digitalIo()->set(f->arg(0)->boolValue());
    f->finish();
  }
  else {
    // return current state as triggerable event
    f->finish(new DigitalInputEventObj(d->digitalIo()));
  }
}


static const BuiltinMemberDescriptor digitalioFunctions[] = {
  FUNC_DEF_W_ARG(state, executable|numeric),
  FUNC_DEF_NOARG(toggle, executable|numeric),
  FUNC_DEF_W_ARG(detectchanges, executable|numeric),
  BUILTINS_TERMINATOR
};

static BuiltInMemberLookup* sharedDigitalIoFunctionLookupP = NULL;

DigitalIoObj::DigitalIoObj(DigitalIoPtr aDigitalIo) :
  mDigitalIo(aDigitalIo)
{
  registerSharedLookup(sharedDigitalIoFunctionLookupP, digitalioFunctions);
}


DigitalIoPtr DigitalIoObj::digitalIoFromArg(ScriptObjPtr aArg, bool aOutput, bool aInitialState)
{
  DigitalIoPtr dio;
  DigitalIoObj* d = dynamic_cast<DigitalIoObj*>(aArg.get());
  if (d) {
    dio = d->digitalIo();
  }
  else if (aArg->hasType(text)) {
    #if ENABLE_APPLICATION_SUPPORT
    if (Application::sharedApplication()->userLevel()>=1)
    #endif
    { // user level >=1 is needed for IO access
      dio = DigitalIoPtr(new DigitalIo(aArg->stringValue().c_str(), aOutput, aInitialState));
    }
  }
  return dio;
}


// value() // get value
// value(val) // set value
FUNC_ARG_DEFS(value, { numeric|optionalarg } );
static void value_func(BuiltinFunctionContextPtr f)
{
  DigitalIoBusObj* b = dynamic_cast<DigitalIoBusObj *>(f->thisObj().get());
  assert(b);
  if (f->numArgs()>0) {
    // set new bus value
    b->digitalIoBus()->setBusValue(f->arg(0)->intValue());
    f->finish();
  }
  else {
    // return current value
    f->finish(new IntegerValue((int64_t)b->digitalIoBus()->getBusValue()));
  }
}


// maxvalue() // get max value this bus can represent
static void maxvalue_func(BuiltinFunctionContextPtr f)
{
  DigitalIoBusObj* b = dynamic_cast<DigitalIoBusObj *>(f->thisObj().get());
  assert(b);
  f->finish(new IntegerValue((int64_t)b->digitalIoBus()->getMaxBusValue()));
}


// buswidth() // bus width in number of bits
static void buswidth_func(BuiltinFunctionContextPtr f)
{
  DigitalIoBusObj* b = dynamic_cast<DigitalIoBusObj *>(f->thisObj().get());
  assert(b);
  f->finish(new IntegerValue(b->digitalIoBus()->getBusWidth()));
}


static const BuiltinMemberDescriptor digitalIoBusFunctions[] = {
  FUNC_DEF_W_ARG(value, executable|numeric),
  FUNC_DEF_NOARG(buswidth, executable|numeric),
  FUNC_DEF_NOARG(maxvalue, executable|numeric),
  BUILTINS_TERMINATOR
};

static BuiltInMemberLookup* sharedDigitalIoBusFunctionLookupP = NULL;

DigitalIoBusObj::DigitalIoBusObj(DigitalIoBusPtr aDigitalIoBus) :
  mDigitalIoBus(aDigitalIoBus)
{
  registerSharedLookup(sharedDigitalIoBusFunctionLookupP, digitalIoBusFunctions);
}




// blink([period [, onpercent [, timeout]]])
FUNC_ARG_DEFS(blink, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } );
static void blink_func(BuiltinFunctionContextPtr f)
{
  IndicatorObj* i = dynamic_cast<IndicatorObj*>(f->thisObj().get());
  assert(i);
  MLMicroSeconds howlong = p44::Infinite;
  MLMicroSeconds period = 600*MilliSecond;
  int onpercent = 50;
  if (f->arg(0)->defined()) period = f->arg(0)->doubleValue()*Second;
  if (f->arg(1)->defined()) onpercent = f->arg(1)->intValue();
  if (f->arg(2)->defined()) howlong = f->arg(2)->doubleValue()*Second;
  i->indicator()->blinkFor(howlong, period, onpercent);
  f->finish();
}


// on() // just turn on
// on(timeout) // on for a certain time
FUNC_ARG_DEFS(on, { numeric|optionalarg } );
static void on_func(BuiltinFunctionContextPtr f)
{
  IndicatorObj* i = dynamic_cast<IndicatorObj*>(f->thisObj().get());
  assert(i);
  if (f->numArgs()>0) {
    // timed on
    i->indicator()->onFor(f->arg(0)->doubleValue()*Second);
  }
  else {
    i->indicator()->steadyOn();
  }
  f->finish();
}


// off()
static void off_func(BuiltinFunctionContextPtr f)
{
  IndicatorObj* i = dynamic_cast<IndicatorObj*>(f->thisObj().get());
  assert(i);
  i->indicator()->steadyOff();
  f->finish();
}


// stop()
static void stop_func(BuiltinFunctionContextPtr f)
{
  IndicatorObj* i = dynamic_cast<IndicatorObj*>(f->thisObj().get());
  assert(i);
  i->indicator()->stop();
  f->finish();
}


static const BuiltinMemberDescriptor indicatorFunctions[] = {
  FUNC_DEF_W_ARG(blink, executable|numeric),
  FUNC_DEF_W_ARG(on, executable|numeric),
  FUNC_DEF_NOARG(off, executable|numeric),
  FUNC_DEF_NOARG(stop, executable|numeric),
  BUILTINS_TERMINATOR
};

static BuiltInMemberLookup* sharedIndicatorFunctionLookupP = NULL;

IndicatorObj::IndicatorObj(IndicatorOutputPtr aIndicator) :
  mIndicator(aIndicator)
{
  registerSharedLookup(sharedIndicatorFunctionLookupP, indicatorFunctions);
}


// digitalio(pinspec, isOutput [, initialValue])
FUNC_ARG_DEFS(digitalio, { text }, { numeric }, { numeric|optionalarg } );
static void digitalio_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  bool out = f->arg(1)->boolValue();
  bool v = false;
  if (f->arg(2)->defined()) v = f->arg(2)->boolValue();
  DigitalIoPtr digitalio = new DigitalIo(f->arg(0)->stringValue().c_str(), out, v);
  f->finish(new DigitalIoObj(digitalio));
}


// digitalbus(pinspecs, areOutputs [, initialPinValues])
FUNC_ARG_DEFS(digitalbus, { text }, { numeric }, { numeric|optionalarg } );
static void digitalbus_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  bool out = f->arg(1)->boolValue();
  bool v = false;
  if (f->arg(2)->defined()) v = f->arg(2)->boolValue();
  DigitalIoBusPtr digitalbus = new DigitalIoBus(f->arg(0)->stringValue().c_str(), 32, out, v);
  f->finish(new DigitalIoBusObj(digitalbus));
}



// indicator(pinspec [, initialValue])
FUNC_ARG_DEFS(indicator, { text }, { numeric|optionalarg } );
static void indicator_func(BuiltinFunctionContextPtr f)
{
  #if ENABLE_APPLICATION_SUPPORT
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  #endif
  bool v = false;
  if (f->arg(1)->defined()) v = f->arg(2)->boolValue();
  IndicatorOutputPtr indicator = new IndicatorOutput(f->arg(0)->stringValue().c_str(), v);
  f->finish(new IndicatorObj(indicator));
}


static const BuiltinMemberDescriptor digitalioGlobals[] = {
  FUNC_DEF_W_ARG(digitalio, executable|null),
  FUNC_DEF_W_ARG(digitalbus, executable|null),
  FUNC_DEF_W_ARG(indicator, executable|null),
  BUILTINS_TERMINATOR
};

DigitalIoLookup::DigitalIoLookup() :
  inherited(digitalioGlobals)
{
}


#endif // ENABLE_DIGITALIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
