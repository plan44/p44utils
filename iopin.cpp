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

#include "iopin.hpp"

using namespace p44;


// MARK: - IOPin

IOPin::IOPin() :
  mCurrentState(false),
  mInvertedReporting(false),
  mPollInterval(Never),
  mLastReportedChange(Never)
{
}


IOPin::~IOPin()
{
  clearChangeHandling();
}


void IOPin::clearChangeHandling()
{
  mInputChangedCB = NoOP;
  mPollInterval = Never;
  mPollTicket.cancel();
  mDebounceTicket.cancel();
}


#define IOPIN_DEFAULT_POLL_INTERVAL (25*MilliSecond)

bool IOPin::setInputChangedHandler(InputChangedCB aInputChangedCB, bool aInverted, bool aInitialState, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  mInputChangedCB = aInputChangedCB;
  mPollInterval = aPollInterval;
  mCurrentState = aInitialState;
  mInvertedReporting = aInverted;
  mDebounceTime = aDebounceTime;
  if (aInputChangedCB==NULL) {
    // disable polling
    clearChangeHandling();
  }
  else {
    if (mPollInterval<0)
      return false; // cannot install non-polling input change handler
    // install handler
    if (mPollInterval==0) {
      // use default interval
      mPollInterval = IOPIN_DEFAULT_POLL_INTERVAL;
    }
    // schedule first poll
    mPollTicket.executeOnce(boost::bind(&IOPin::timedpoll, this, _1));
  }
  return true; // successful
}


void IOPin::inputHasChangedTo(bool aNewState)
{
  if (aNewState!=mCurrentState) {
    mDebounceTicket.cancel();
    MLMicroSeconds now = MainLoop::now();
    // optional debouncing
    if (mDebounceTime>0 && mLastReportedChange!=Never) {
      // check for debounce time passed
      if (mLastReportedChange+mDebounceTime>now) {
        LOG(LOG_DEBUG, "- debouncing holdoff, will resample after debouncing time");
        // debounce time not yet over, schedule an extra re-sample later and suppress reporting for now
        mDebounceTicket.executeOnce(boost::bind(&IOPin::debounceSample, this), mDebounceTime);
        return;
      }
    }
    // report change now
    LOG(LOG_DEBUG, "- state changed >=debouncing time after last change: new state = %d", aNewState);
    mCurrentState = aNewState;
    mLastReportedChange = now;
    if (mInputChangedCB) mInputChangedCB(mCurrentState!=mInvertedReporting);
  }
}


void IOPin::debounceSample()
{
  bool newState = getState();
  LOG(LOG_DEBUG, "- debouncing time over, resampled state = %d", newState);
  if (newState!=mCurrentState) {
    mCurrentState = newState;
    mLastReportedChange = MainLoop::now();
    if (mInputChangedCB) mInputChangedCB(mCurrentState!=mInvertedReporting);
  }
}



void IOPin::timedpoll(MLTimer &aTimer)
{
  inputHasChangedTo(getState());
  // schedule next poll
  MainLoop::currentMainLoop().retriggerTimer(aTimer, mPollInterval, mPollInterval/2); // allow 50% jitter
}




// MARK: - digital I/O simulation

static char nextIoSimKey = 'a';

SimPin::SimPin(const char *aName, bool aOutput, bool aInitialState) :
  mName(aName),
  mOutput(aOutput),
  mPinState(aInitialState)
{
  LOG(LOG_ALERT, "Initialized SimPin \"%s\" as %s with initial state %s", mName.c_str(), aOutput ? "output" : "input", mPinState ? "HI" : "LO");
  #if !DISABLE_CONSOLEKEY
  size_t n = mName.find(":");
  char key = 0;
  if (n!=string::npos && mName.size()>n+1) {
    key = mName[n+1];
  }
  else {
    key = nextIoSimKey++;
  }
  if (!aOutput) {
    if (!mOutput) {
      mConsoleKey = ConsoleKeyManager::sharedKeyManager()->newConsoleKey(
        key,
        mName.c_str(),
        mPinState
      );
    }
  }
  #endif
}


bool SimPin::getState()
{
  if (mOutput) {
    return mPinState; // just return last set state
  }
  else {
    #if DISABLE_CONSOLEKEY
    return false; // no input at all
    #else
    return (bool)mConsoleKey->isSet();
    #endif
  }
}


void SimPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mPinState!=aState) {
    mPinState = aState;
    LOG(LOG_ALERT, ">>> SimPin \"%s\" set to %s", mName.c_str(), mPinState ? "HI" : "LO");
  }
}


// MARK: - digital output via system command

#if !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)

SysCommandPin::SysCommandPin(const char *aConfig, bool aOutput, bool aInitialState) :
  mPinState(aInitialState),
  mOutput(aOutput),
  mChangePending(false),
  mChanging(false)
{
  // separate commands for switching on and off
  //  oncommand|offcommand
  string s = aConfig;
  size_t i = s.find("|", 0);
  if (i!=string::npos) {
    mOnCommand = s.substr(0,i);
    mOffCommand = s.substr(i+1);
  }
  // force setting initial state
  mPinState = !aInitialState;
  setState(aInitialState);
}


string SysCommandPin::stateSetCommand(bool aState)
{
  return aState ? mOnCommand : mOffCommand;
}


void SysCommandPin::setState(bool aState)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mPinState!=aState) {
    mPinState = aState;
    // schedule change
    applyState(aState);
  }
}


void SysCommandPin::applyState(bool aState)
{
  if (mChanging) {
    // already in process of applying a change
    mChangePending = true;
  }
  else {
    // trigger change
    mChanging = true;
    MainLoop::currentMainLoop().fork_and_system(boost::bind(&SysCommandPin::stateUpdated, this, _1, _2), stateSetCommand(aState).c_str());
  }
}


void SysCommandPin::stateUpdated(ErrorPtr aError, const string &aOutputString)
{
  if (Error::notOK(aError)) {
    LOG(LOG_WARNING, "SysCommandPin set state=%d: command (%s) execution failed: %s", mPinState, stateSetCommand(mPinState).c_str(), aError->text());
  }
  else {
    LOG(LOG_INFO, "SysCommandPin set state=%d: command (%s) executed successfully", mPinState, stateSetCommand(mPinState).c_str());
  }
  mChanging = false;
  if (mChangePending) {
    mChangePending = false;
    // apply latest value
    applyState(mPinState);
  }
}

#endif // !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)


// MARK: - analog I/O simulation


AnalogSimPin::AnalogSimPin(const char *aName, bool aOutput, double aInitialValue) :
  mName(aName),
  mOutput(aOutput),
  mPinValue(aInitialValue)
{
  LOG(LOG_ALERT, "Initialized AnalogSimPin \"%s\" as %s with initial value %.2f", mName.c_str(), aOutput ? "output" : "input", mPinValue);
  #if !DISABLE_CONSOLEKEY
  if (!aOutput) {
    if (!mOutput) {
      mConsoleKeyUp = ConsoleKeyManager::sharedKeyManager()->newConsoleKey(
        nextIoSimKey++,
        "increase",
        false
      );
      mConsoleKeyUp->setConsoleKeyHandler(boost::bind(&AnalogSimPin::simKeyPress, this, 1, _1));
      mConsoleKeyDown = ConsoleKeyManager::sharedKeyManager()->newConsoleKey(
        nextIoSimKey++,
        "decrease",
        false
      );
      mConsoleKeyDown->setConsoleKeyHandler(boost::bind(&AnalogSimPin::simKeyPress, this, -1, _1));
    }
  }
  #endif
}


#if !DISABLE_CONSOLEKEY
void AnalogSimPin::simKeyPress(int aDir, bool aNewState)
{
  if (aNewState) {
    mPinValue += 0.1*aDir;
    LOG(LOG_ALERT, ">>> AnalogSimPin \"%s\" manually changed to %.2f", mName.c_str(), mPinValue);
  }
}
#endif


double AnalogSimPin::getValue()
{
  return mPinValue; // just return last set value (as set by setValue or modified by key presses)
}


void AnalogSimPin::setValue(double aValue)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mPinValue!=aValue) {
    mPinValue = aValue;
    LOG(LOG_ALERT, ">>> AnalogSimPin \"%s\" set to %.2f", mName.c_str(), mPinValue);
  }
}


// MARK: - analog output via system command


#if !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)

AnalogSysCommandPin::AnalogSysCommandPin(const char *aConfig, bool aOutput, double aInitialValue) :
  mPinValue(aInitialValue),
  mOutput(aOutput),
  mRange(100),
  mChangePending(false),
  mChanging(false)
{
  // Save set command
  //  [range|]offcommand
  mSetCommand = aConfig;
  size_t i = mSetCommand.find("|", 0);
  // check for range
  if (i!=string::npos) {
    sscanf(mSetCommand.substr(0,i).c_str(), "%d", &mRange);
    mSetCommand.erase(0,i+1);
  }
  // force setting initial state
  mPinValue = aInitialValue+1;
  setValue(aInitialValue);
}


string AnalogSysCommandPin::valueSetCommand(double aValue)
{
  size_t vpos = mSetCommand.find("${VALUE}");
  string cmd;
  if (vpos!=string::npos) {
    cmd = mSetCommand;
    cmd.replace(vpos, vpos+8, string_format("%d", (int)(aValue/100*mRange))); // aValue assumed to be 0..100
  }
  return cmd;
}


void AnalogSysCommandPin::setValue(double aValue)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (aValue!=mPinValue) {
    mPinValue = aValue;
    // schedule change
    applyValue(aValue);
  }
}


void AnalogSysCommandPin::applyValue(double aValue)
{
  if (mChanging) {
    // already in process of applying a change
    mChangePending = true;
  }
  else {
    // trigger change
    mChanging = true;
    MainLoop::currentMainLoop().fork_and_system(boost::bind(&AnalogSysCommandPin::valueUpdated, this, _1, _2), valueSetCommand(aValue).c_str());
  }
}


void AnalogSysCommandPin::valueUpdated(ErrorPtr aError, const string &aOutputString)
{
  if (Error::notOK(aError)) {
    LOG(LOG_WARNING, "AnalogSysCommandPin set value=%.2f: command (%s) execution failed: %s", mPinValue, valueSetCommand(mPinValue).c_str(), aError->text());
  }
  else {
    LOG(LOG_INFO, "AnalogSysCommandPin set value=%.2f: command (%s) executed successfully", mPinValue, valueSetCommand(mPinValue).c_str());
  }
  mChanging = false;
  if (mChangePending) {
    mChangePending = false;
    // apply latest value
    applyValue(mPinValue);
  }
}

#endif // !DISABLE_SYSTEMCMDIO && !defined(ESP_PLATFORM)

// MARK: - analog I/O simulation from fd


AnalogSimPinFd::AnalogSimPinFd(const char *aName, bool aOutput, double aInitialValue) :
  mName(aName),
  mOutput(aOutput),
  mPinValue(aInitialValue)
{
  LOG(LOG_ALERT, "Initialized AnalogSimPinFd \"%s\" as %s with initial value %.2f", mName.c_str(), aOutput ? "output" : "input", mPinValue);
  mFd = open(aName, O_RDWR);
}


double AnalogSimPinFd::getValue()
{
  char buf[20];
  lseek(mFd, 0, SEEK_SET);
  if (read(mFd, buf, 20) > 0) {
    mPinValue = atof(buf);
  }
  return mPinValue;
}


void AnalogSimPinFd::setValue(double aValue)
{
  if (!mOutput) return; // non-outputs cannot be set
  if (mPinValue!=aValue) {
    mPinValue = aValue;
    char buf[20];
    snprintf(buf, 20, "%f\n", mPinValue);
    lseek(mFd, 0, SEEK_SET);
    write(mFd, buf, strlen(buf));
  }
}
