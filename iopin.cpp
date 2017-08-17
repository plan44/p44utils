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

#include "iopin.hpp"

using namespace p44;


// MARK: ===== IOPin

IOPin::IOPin() :
  currentState(false),
  invertedReporting(false),
  pollTicket(0),
  debounceTicket(0),
  pollInterval(Never),
  lastReportedChange(Never)
{
}


IOPin::~IOPin()
{
  clearChangeHandling();
}


void IOPin::clearChangeHandling()
{
  inputChangedCB = NULL;
  pollInterval = Never;
  MainLoop::currentMainLoop().cancelExecutionTicket(pollTicket);
  MainLoop::currentMainLoop().cancelExecutionTicket(debounceTicket);
}


#define IOPIN_DEFAULT_POLL_INTERVAL (25*MilliSecond)

bool IOPin::setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  inputChangedCB = aInputChangedCB;
  pollInterval = aPollInterval;
  currentState = aInitialState;
  invertedReporting = aInverted;
  debounceTime = aDebounceTime;
  if (aInputChangedCB==NULL) {
    // disable polling
    clearChangeHandling();
  }
  else {
    if (pollInterval<0)
      return false; // cannot install non-polling input change handler
    // install handler
    if (pollInterval==0) {
      // use default interval
      pollInterval = IOPIN_DEFAULT_POLL_INTERVAL;
    }
    // schedule first poll
    MainLoop::currentMainLoop().executeTicketOnce(pollTicket, boost::bind(&IOPin::timedpoll, this, _1));
  }
  return true; // successful
}


void IOPin::inputHasChangedTo(bool aNewState)
{
  if (aNewState!=currentState) {
    MainLoop::currentMainLoop().cancelExecutionTicket(debounceTicket);
    MLMicroSeconds now = MainLoop::now();
    // optional debouncing
    if (debounceTime>0 && lastReportedChange!=Never) {
      // check for debounce time passed
      if (lastReportedChange+debounceTime>now) {
        LOG(LOG_DEBUG, "- debouncing holdoff, will resample after debouncing time");
        // debounce time not yet over, schedule an extra re-sample later and suppress reporting for now
        debounceTicket = MainLoop::currentMainLoop().executeOnce(boost::bind(&IOPin::debounceSample, this), debounceTime);
        return;
      }
    }
    // report change now
    LOG(LOG_DEBUG, "- state changed >=debouncing time after last change: new state = %d", aNewState);
    currentState = aNewState;
    lastReportedChange = now;
    if (inputChangedCB) inputChangedCB(currentState!=invertedReporting);
  }
}


void IOPin::debounceSample()
{
  bool newState = getState();
  LOG(LOG_DEBUG, "- debouncing time over, resampled state = %d", newState);
  if (newState!=currentState) {
    currentState = newState;
    lastReportedChange = MainLoop::now();
    if (inputChangedCB) inputChangedCB(currentState!=invertedReporting);
  }
}



void IOPin::timedpoll(MLTimer &aTimer)
{
  inputHasChangedTo(getState());
  // schedule next poll
  MainLoop::currentMainLoop().retriggerTimer(aTimer, pollInterval, pollInterval/2); // allow 50% jitter
}




// MARK: ===== digital I/O simulation

static char nextIoSimKey = 'a';

SimPin::SimPin(const char *aName, bool aOutput, bool aInitialState) :
  name(aName),
  output(aOutput),
  pinState(aInitialState)
{
  LOG(LOG_ALERT, "Initialized SimPin \"%s\" as %s with initial state %s", name.c_str(), aOutput ? "output" : "input", pinState ? "HI" : "LO");
  #if !DISABLE_CONSOLEKEY
  size_t n = name.find(":");
  char key = 0;
  if (n!=string::npos && name.size()>n+1) {
    key = name[n+1];
  }
  else {
    key = nextIoSimKey++;
  }
  if (!aOutput) {
    if (!output) {
      consoleKey = ConsoleKeyManager::sharedKeyManager()->newConsoleKey(
        key,
        name.c_str(),
        pinState
      );
    }
  }
  #endif
}


bool SimPin::getState()
{
  if (output) {
    return pinState; // just return last set state
  }
  else {
    #if DISABLE_CONSOLEKEY
    return false; // no input at all
    #else
    return (bool)consoleKey->isSet();
    #endif
  }
}


void SimPin::setState(bool aState)
{
  if (!output) return; // non-outputs cannot be set
  if (pinState!=aState) {
    pinState = aState;
    LOG(LOG_ALERT, ">>> SimPin \"%s\" set to %s", name.c_str(), pinState ? "HI" : "LO");
  }
}


// MARK: ===== digital output via system command

#if !DISABLE_SYSTEMCMDIO

SysCommandPin::SysCommandPin(const char *aConfig, bool aOutput, bool aInitialState) :
  pinState(aInitialState),
  output(aOutput),
  changePending(false),
  changing(false)
{
  // separate commands for switching on and off
  //  oncommand|offcommand
  string s = aConfig;
  size_t i = s.find("|", 0);
  if (i!=string::npos) {
    onCommand = s.substr(0,i);
    offCommand = s.substr(i+1);
  }
  // force setting initial state
  pinState = !aInitialState;
  setState(aInitialState);
}


string SysCommandPin::stateSetCommand(bool aState)
{
  return aState ? onCommand : offCommand;
}


void SysCommandPin::setState(bool aState)
{
  if (!output) return; // non-outputs cannot be set
  if (pinState!=aState) {
    pinState = aState;
    // schedule change
    applyState(aState);
  }
}


void SysCommandPin::applyState(bool aState)
{
  if (changing) {
    // already in process of applying a change
    changePending = true;
  }
  else {
    // trigger change
    changing = true;
    MainLoop::currentMainLoop().fork_and_system(boost::bind(&SysCommandPin::stateUpdated, this, _1, _2), stateSetCommand(aState).c_str());
  }
}


void SysCommandPin::stateUpdated(ErrorPtr aError, const string &aOutputString)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_WARNING, "SysCommandPin set state=%d: command (%s) execution failed: %s", pinState, stateSetCommand(pinState).c_str(), aError->description().c_str());
  }
  else {
    LOG(LOG_INFO, "SysCommandPin set state=%d: command (%s) executed successfully", pinState, stateSetCommand(pinState).c_str());
  }
  changing = false;
  if (changePending) {
    changePending = false;
    // apply latest value
    applyState(pinState);
  }
}

#endif // !DISABLE_SYSTEMCMDIO


// MARK: ===== analog I/O simulation


AnalogSimPin::AnalogSimPin(const char *aName, bool aOutput, double aInitialValue) :
  name(aName),
  output(aOutput),
  pinValue(aInitialValue)
{
  LOG(LOG_ALERT, "Initialized AnalogSimPin \"%s\" as %s with initial value %.2f", name.c_str(), aOutput ? "output" : "input", pinValue);
  #if !DISABLE_CONSOLEKEY
  if (!aOutput) {
    if (!output) {
      consoleKey = ConsoleKeyManager::sharedKeyManager()->newConsoleKey(
        nextIoSimKey++,
        name.c_str(),
        false
      );
    }
  }
  #endif
}


double AnalogSimPin::getValue()
{
  return pinValue; // just return last set value (as set by setValue or modified by key presses)
}


void AnalogSimPin::setValue(double aValue)
{
  if (!output) return; // non-outputs cannot be set
  if (pinValue!=aValue) {
    pinValue = aValue;
    LOG(LOG_ALERT, ">>> AnalogSimPin \"%s\" set to %.2f", name.c_str(), pinValue);
  }
}


// MARK: ===== analog output via system command


#if !DISABLE_SYSTEMCMDIO

AnalogSysCommandPin::AnalogSysCommandPin(const char *aConfig, bool aOutput, double aInitialValue) :
  pinValue(aInitialValue),
  output(aOutput),
  range(100),
  changePending(false),
  changing(false)
{
  // Save set command
  //  [range|]offcommand
  setCommand = aConfig;
  size_t i = setCommand.find("|", 0);
  // check for range
  if (i!=string::npos) {
    sscanf(setCommand.substr(0,i).c_str(), "%d", &range);
    setCommand.erase(0,i+1);
  }
  // force setting initial state
  pinValue = aInitialValue+1;
  setValue(aInitialValue);
}


string AnalogSysCommandPin::valueSetCommand(double aValue)
{
  size_t vpos = setCommand.find("${VALUE}");
  string cmd;
  if (vpos!=string::npos) {
    cmd = setCommand;
    cmd.replace(vpos, vpos+8, string_format("%d", (int)(aValue/100*range))); // aValue assumed to be 0..100
  }
  return cmd;
}


void AnalogSysCommandPin::setValue(double aValue)
{
  if (!output) return; // non-outputs cannot be set
  if (aValue!=pinValue) {
    pinValue = aValue;
    // schedule change
    applyValue(aValue);
  }
}


void AnalogSysCommandPin::applyValue(double aValue)
{
  if (changing) {
    // already in process of applying a change
    changePending = true;
  }
  else {
    // trigger change
    changing = true;
    MainLoop::currentMainLoop().fork_and_system(boost::bind(&AnalogSysCommandPin::valueUpdated, this, _1, _2), valueSetCommand(aValue).c_str());
  }
}


void AnalogSysCommandPin::valueUpdated(ErrorPtr aError, const string &aOutputString)
{
  if (!Error::isOK(aError)) {
    LOG(LOG_WARNING, "AnalogSysCommandPin set value=%.2f: command (%s) execution failed: %s", pinValue, valueSetCommand(pinValue).c_str(), aError->description().c_str());
  }
  else {
    LOG(LOG_INFO, "AnalogSysCommandPin set value=%.2f: command (%s) executed successfully", pinValue, valueSetCommand(pinValue).c_str());
  }
  changing = false;
  if (changePending) {
    changePending = false;
    // apply latest value
    applyValue(pinValue);
  }
}

#endif // !DISABLE_SYSTEMCMDIO


