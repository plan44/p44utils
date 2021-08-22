//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#if !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)
  #if ENABLE_APPLICATION_SUPPORT
    #include "application.hpp" // we need it for user level, syscmd is only allowed with userlevel>=2
  #endif
  #ifndef ALWAYS_ALLOW_SYSCMDIO
    #define ALWAYS_ALLOW_SYSCMDIO 0
  #endif
#endif


using namespace p44;


DigitalIo::DigitalIo(const char* aPinSpec, bool aOutput, bool aInitialState) :
  inverted(false),
  pull(undefined)
{
  // save params
  output = aOutput;
  // check for inverting and pullup prefixes
  while (aPinSpec && *aPinSpec) {
    if (*aPinSpec=='/') inverted = true;
    else if (*aPinSpec=='+') pull = yes; // pullup
    else if (*aPinSpec=='-') pull = no; // pulldown
    else break; // none of the allowed prefixes -> done
    ++aPinSpec; // processed prefix -> check next
  }
  // rest is actual pin specification
  pinSpec = nonNullCStr(aPinSpec);
  bool initialPinState = aInitialState!=inverted;
  // check for missing pin (no pin, just silently keeping state)
  if (pinSpec.size()==0 || pinSpec=="missing") {
    ioPin = IOPinPtr(new MissingPin(initialPinState));
    return;
  }
  // dissect name into bus, device, pin
  string busName;
  string deviceName;
  string pinName;
  size_t i = pinSpec.find(".");
  if (i==string::npos) {
    // just a bus name, device and pin remain empty
    busName = pinSpec;
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
    // Linux or ESP32 generic GPIO
    // gpio.<gpionumber>
    int pinNumber = atoi(pinName.c_str());
    ioPin = IOPinPtr(new GpioPin(pinNumber, output, initialPinState, pull));
  }
  #ifndef ESP_PLATFORM
  else if (busName=="led") {
    // Linux generic LED
    // led.<lednumber_or_name>
    ioPin = IOPinPtr(new GpioLedPin(pinName.c_str(), initialPinState));
  }
  #endif // !ESP_PLATFORM
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
    ioPin = IOPinPtr(new I2CPin(busNumber, deviceName.c_str(), pinNumber, output, initialPinState, pull));
  }
  else
  #endif
  #if !DISABLE_SPI
  if (busName.substr(0,3)=="spi") {
    // spi<interfaceno*10+chipselno>.<devicespec>.<pinnum>
    int busNumber = atoi(busName.c_str()+3);
    int pinNumber = atoi(pinName.c_str());
    ioPin = IOPinPtr(new SPIPin(busNumber, deviceName.c_str(), pinNumber, output, initialPinState, pull));
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
  return string_format("%s%s%s", pull==yes ? "+" : (pull==no ? "-" : ""), inverted ? "/" : "", pinSpec.c_str());
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


#if ENABLE_DIGITALIO_SCRIPT_FUNCS && ENABLE_P44SCRIPT
/// get a analog input value object. This is also what is sent to event sinks
P44Script::ScriptObjPtr DigitalIo::getStateObj()
{
  return new P44Script::DigitalInputEventObj(this);
}
#endif



bool DigitalIo::setInputChangedHandler(InputChangedCB aInputChangedCB, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  // enable or disable reporting changes via callback
  return ioPin->setInputChangedHandler(aInputChangedCB, inverted, ioPin->getState(), aDebounceTime, aPollInterval);
}


bool DigitalIo::setChangeDetection(MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  if (aDebounceTime<0) {
    // disable
    return ioPin->setInputChangedHandler(NULL, inverted, false, 0, 0);
  }
  else {
    // enable
    return ioPin->setInputChangedHandler(boost::bind(&DigitalIo::processChange, this, _1), inverted, ioPin->getState(), aDebounceTime, aPollInterval);
  }
}


void DigitalIo::processChange(bool aNewState)
{
  if (hasSinks()) {
    sendEvent(getStateObj());
  }
}



// MARK: - Button input

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



// MARK: - Indicator output

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


// MARK: - script support

#if ENABLE_DIGITALIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

using namespace P44Script;

DigitalInputEventObj::DigitalInputEventObj(DigitalIoPtr aDigitalIo) :
  inherited(false),
  mDigitalIo(aDigitalIo)
{
  if (mDigitalIo) num = mDigitalIo->isSet() ? 1 : 0;
}


void DigitalInputEventObj::deactivate()
{
  mDigitalIo.reset();
  inherited::deactivate();
}


string DigitalInputEventObj::getAnnotation() const
{
  return "input event";
}


EventSource* DigitalInputEventObj::eventSource() const
{
  return static_cast<EventSource*>(mDigitalIo.get());
}


// detectchanges([debouncetime [, pollinterval]])
// detectchanges(null)
static const BuiltInArgDesc detectchanges_args[] = { { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t detectchanges_numargs = sizeof(detectchanges_args)/sizeof(BuiltInArgDesc);
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
    f->finish(new NumericValue(works));
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
static const BuiltInArgDesc state_args[] = { { numeric|optionalarg } };
static const size_t state_numargs = sizeof(state_args)/sizeof(BuiltInArgDesc);
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
  { "state", executable|numeric, state_numargs, state_args, &state_func },
  { "toggle", executable|numeric, 0, NULL, &toggle_func },
  { "detectchanges", executable|numeric, detectchanges_numargs, detectchanges_args, &detectchanges_func },
  { NULL } // terminator
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
    if (Application::sharedApplication()->userLevel()>=1) { // user level >=1 is needed for IO access
      dio = DigitalIoPtr(new DigitalIo(aArg->stringValue().c_str(), aOutput, aInitialState));
    }
  }
  return dio;
}


// blink([period [, onpercent [, timeout]]])
static const BuiltInArgDesc blink_args[] = { { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t blink_numargs = sizeof(blink_args)/sizeof(BuiltInArgDesc);
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
static const BuiltInArgDesc on_args[] = { { numeric|optionalarg } };
static const size_t on_numargs = sizeof(on_args)/sizeof(BuiltInArgDesc);
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
  { "blink", executable|numeric, blink_numargs, blink_args, &blink_func },
  { "on", executable|numeric, on_numargs, on_args, &on_func },
  { "off", executable|numeric, 0, NULL, &off_func },
  { "stop", executable|numeric, 0, NULL, &stop_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedIndicatorFunctionLookupP = NULL;

IndicatorObj::IndicatorObj(IndicatorOutputPtr aIndicator) :
  mIndicator(aIndicator)
{
  registerSharedLookup(sharedIndicatorFunctionLookupP, indicatorFunctions);
}


// digitalio(pinspec, isOutput [, initialValue])
static const BuiltInArgDesc digitalio_args[] = { { text }, { numeric }, { numeric|optionalarg } };
static const size_t digitalio_numargs = sizeof(digitalio_args)/sizeof(BuiltInArgDesc);
static void digitalio_func(BuiltinFunctionContextPtr f)
{
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  bool out = f->arg(1)->boolValue();
  bool v = false;
  if (f->arg(2)->defined()) v = f->arg(2)->boolValue();
  DigitalIoPtr digitalio = new DigitalIo(f->arg(0)->stringValue().c_str(), out, v);
  f->finish(new DigitalIoObj(digitalio));
}


// indicator(pinspec [, initialValue])
static const BuiltInArgDesc indicator_args[] = { { text }, { numeric|optionalarg } };
static const size_t indicator_numargs = sizeof(indicator_args)/sizeof(BuiltInArgDesc);
static void indicator_func(BuiltinFunctionContextPtr f)
{
  if (Application::sharedApplication()->userLevel()<1) { // user level >=1 is needed for IO access
    f->finish(new ErrorValue(ScriptError::NoPrivilege, "no IO privileges"));
  }
  bool v = false;
  if (f->arg(1)->defined()) v = f->arg(2)->boolValue();
  IndicatorOutputPtr indicator = new IndicatorOutput(f->arg(0)->stringValue().c_str(), v);
  f->finish(new IndicatorObj(indicator));
}


static const BuiltinMemberDescriptor digitalioGlobals[] = {
  { "digitalio", executable|null, digitalio_numargs, digitalio_args, &digitalio_func },
  { "indicator", executable|null, indicator_numargs, indicator_args, &indicator_func },
  { NULL } // terminator
};

DigitalIoLookup::DigitalIoLookup() :
  inherited(digitalioGlobals)
{
}


#endif // ENABLE_DIGITALIO_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
