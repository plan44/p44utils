//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2017-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44utils.
//
//  p44ayabd is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  p44ayabd is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with p44ayabd. If not, see <http://www.gnu.org/licenses/>.
//

#include "dcmotor.hpp"

#include "consolekey.hpp"
#include "application.hpp"

#include <math.h>

using namespace p44;


// MARK: - DCMotorDriver

DcMotorDriver::DcMotorDriver(AnalogIoPtr aPWMOutput, DigitalIoPtr aCWDirectionOutput, DigitalIoPtr aCCWDirectionOutput) :
  mCurrentPower(0),
  mCurrentDirection(0)
{
  mPwmOutput = aPWMOutput;
  // - direction control
  mCWdirectionOutput = aCWDirectionOutput;
  mCCWdirectionOutput = aCCWDirectionOutput;
  mPowerOffset = 0;
  mPowerScaling = 1;
  setPower(0, 0);
}



DcMotorDriver::~DcMotorDriver()
{
  // stop power to motor
  setPower(0, 0);
}


void DcMotorDriver::setStopCallback(DCMotorStatusCB aStoppedCB)
{
  mStoppedCB = aStoppedCB;
}



void DcMotorDriver::setEndSwitches(DigitalIoPtr aPositiveEnd, DigitalIoPtr aNegativeEnd, MLMicroSeconds aDebounceTime, MLMicroSeconds aPollInterval)
{
  mPositiveEndInput = aPositiveEnd;
  mNegativeEndInput = aNegativeEnd;
  if (mPositiveEndInput) {
    #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
    mPositiveEndInput->setChangeDetection(aDebounceTime, aPollInterval);
    mPositiveEndInput->registerForEvents(mEndSwitchHandler);
    #else
    mPositiveEndInput->setInputChangedHandler(boost::bind(&DcMotorDriver::endSwitch, this, true, _1), aDebounceTime, aPollInterval);
    #endif
  }
  if (mNegativeEndInput) {
    #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
    mNegativeEndInput->setChangeDetection(aDebounceTime, aPollInterval);
    mNegativeEndInput->registerForEvents(mEndSwitchHandler);
    #else
    mNegativeEndInput->setInputChangedHandler(boost::bind(&DcMotorDriver::endSwitch, this, false, _1), aDebounceTime, aPollInterval);
    #endif
  }
  #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
  mEndSwitchHandler.setHandler(boost::bind(&DcMotorDriver::endSwitchEvent, this, _1, _2));
  #endif
}


#if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
void DcMotorDriver::endSwitchEvent(P44Script::ScriptObjPtr aEvent, P44Script::EventSource &aSource)
{
  endSwitch(&aSource == static_cast<EventSource*>(mPositiveEndInput.get()), aEvent->boolValue());
}
#endif

void DcMotorDriver::endSwitch(bool aPositiveEnd, bool aNewState)
{
  // save direction and power as known BEFORE stopping
  if (aNewState) {
    double pwr = mCurrentPower;
    int dir = mCurrentDirection;
    stop();
    LOG(LOG_INFO, "stopped with power=%.2f, direction=%d because %s end switch reached", pwr, dir, aPositiveEnd ? "positive" : "negative");
    autoStopped(pwr, dir, new DcMotorDriverError(DcMotorDriverError::endswitchStop));
  }
}


void DcMotorDriver::autoStopped(double aPower, int aDirection, ErrorPtr aError)
{
  if (mStoppedCB) {
    // stop callback does not reset
    mStoppedCB(aPower, aDirection, aError);
  }
  motorStatusUpdate(aError);
}


void DcMotorDriver::motorStatusUpdate(ErrorPtr aStopCause)
{
  mStopCause = aStopCause;
  if (mRampDoneCB) {
    // ramp done CB must be set for every ramp separately
    DCMotorStatusCB cb = mRampDoneCB;
    mRampDoneCB = NoOP;
    cb(mCurrentPower, mCurrentDirection, mStopCause);
  }
  #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
  if (hasSinks()) {
    sendEvent(getStatusObj());
  }
  #endif
}

#if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
P44Script::ScriptObjPtr DcMotorDriver::getStatusObj()
{
  return new P44Script::DcMotorStatusObj(this);
}
#endif

void DcMotorDriver::setCurrentSensor(AnalogIoPtr aCurrentSensor, MLMicroSeconds aSampleInterval)
{
  // - current sensor
  #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
  if (mCurrentSensor) mCurrentSensor->unregisterFromEvents(mCurrentHandler); // make sure previous sensor no longer sends events
  #endif
  mCurrentSensor = aCurrentSensor;
  mSampleInterval = aSampleInterval;
  #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
  if (mCurrentSensor) mCurrentSensor->registerForEvents(mCurrentHandler); // we want to see events of the new sensor
  #endif
}


void DcMotorDriver::setCurrentLimits(double aStopCurrent, MLMicroSeconds aHoldOffTime, double aMaxStartCurrent)
{
  // - current sensor
  mStopCurrent = aStopCurrent;
  mCurrentLimiterHoldoffTime = aHoldOffTime;
  mMaxStartCurrent = aMaxStartCurrent;
}


void DcMotorDriver::setOutputParams(double aPowerScaling, double aPowerOffset)
{
  mPowerScaling = aPowerScaling;
  mPowerOffset = aPowerOffset;
}




void DcMotorDriver::setDirection(int aDirection)
{
  if (mCWdirectionOutput) {
    mCWdirectionOutput->set(aDirection>0);
    if (mCCWdirectionOutput) {
      mCCWdirectionOutput->set(aDirection<0);
    }
  }
  if (aDirection!=mCurrentDirection) {
    OLOG(LOG_INFO, "Direction changed to %d", aDirection);
    mCurrentDirection = aDirection;
  }
}



void DcMotorDriver::setPower(double aPower, int aDirection)
{
  if (aPower<=0) {
    // no power
    // - disable power completely (0, even when there is a mPowerOffset)
    mPwmOutput->setValue(0);
    // - off (= hold/brake with no power)
    setDirection(0);
    // disable current sampling
    if (mCurrentSensor) mCurrentSensor->setAutopoll(0);
  }
  else {
    // determine current direction
    if (mCurrentDirection!=0 && aDirection!=0 && aDirection!=mCurrentDirection) {
      // avoid reversing direction with power on
      mPwmOutput->setValue(0);
      setDirection(0);
    }
    // check end switch, do not allow setting power towards already active end switch
    if (
      (aDirection<0 && mNegativeEndInput && mNegativeEndInput->isSet()) ||
      (aDirection>0 && mPositiveEndInput && mPositiveEndInput->isSet())
    ) {
      OLOG(LOG_INFO, "Cannot start in direction %d, endswitch is active", aDirection);
      // count this as running (again) into an end switch and cause callback to fire
      endSwitch(aDirection>0, true);
      return;
    }
    // start current sampling when starting to apply power
    if (mCurrentSensor && mStopCurrent>0 && aDirection!=0 && mCurrentPower==0) {
      mStartMonitoring = MainLoop::now()+mCurrentLimiterHoldoffTime;
      #if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
      mCurrentSensor->setAutopoll(mSampleInterval, mSampleInterval/4);
      mCurrentHandler.setHandler(boost::bind(&DcMotorDriver::checkCurrent, this));
      #else
      mCurrentSensor->setAutopoll(mSampleInterval, mSampleInterval/4, boost::bind(&DcMotorDriver::checkCurrent, this));
      #endif
    }
    // now set desired direction and power
    setDirection(aDirection);
    mPwmOutput->setValue(mPowerOffset+aPower*mPowerScaling);
  }
  if (aPower!=mCurrentPower) {
    OLOG(LOG_DEBUG, "Power changed to %.2f%%", aPower);
    mCurrentPower = aPower;
  }
}


void DcMotorDriver::checkCurrent()
{
  if (mStopCurrent>0) {
    // event from current sensor, process value
    double v = fabs(mCurrentSensor->processedValue()); // takes abs, in case we're not using processing that already takes abs values
    OLOG(LOG_DEBUG, "checkCurrent: processed: %.3f, last raw value: %.3f", v, mCurrentSensor->lastValue());
    if (
      (v>=mStopCurrent && MainLoop::now()>=mStartMonitoring) || // normal limit
      (mMaxStartCurrent>0 && v>=mMaxStartCurrent) // limit during startup
    ) {
      if (mCurrentPower>0) {
        double pwr = mCurrentPower;
        int dir = mCurrentDirection;
        stop();
        OLOG(LOG_INFO, "stopped because processed current (%.3f) exceeds max (%.3f) - last raw sample = %.3f", v, mStopCurrent, mCurrentSensor->lastValue());
        autoStopped(pwr, dir, new DcMotorDriverError(DcMotorDriverError::overcurrentStop));
      }
    }
  }
}


#define RAMP_STEP_TIME (20*MilliSecond)

void DcMotorDriver::stop()
{
  stopSequences();
  setPower(0, 0);
}


void DcMotorDriver::stopSequences()
{
  mSequenceTicket.cancel();
}



void DcMotorDriver::rampToPower(double aPower, int aDirection, double aRampTime, double aRampExp, DCMotorStatusCB aRampDoneCB)
{
  OLOG(LOG_INFO, "+++ new ramp: power: %.2f%%..%.2f%%, direction:%d..%d with ramp time %.3f Seconds, exp=%.2f", mCurrentPower, aPower, mCurrentDirection, aDirection, aRampTime, aRampExp);
  mStopCause.reset(); // clear status from previous run
  mRampDoneCB = aRampDoneCB;
  MainLoop::currentMainLoop().cancelExecutionTicket(mSequenceTicket);
  if (aDirection!=mCurrentDirection) {
    if (mCurrentPower!=0) {
      // ramp to zero first, then ramp up to new direction
      OLOG(LOG_INFO, "Ramp trough different direction modes -> first ramp power down, then up again");
      if (aRampTime>0) aRampTime /= 2; // for absolute ramp time specificiation, just use half of the time for ramp up or down, resp. 
      rampToPower(0, mCurrentDirection, aRampTime, aRampExp, boost::bind(&DcMotorDriver::rampToPower, this, aPower, aDirection, aRampTime, aRampExp, aRampDoneCB));
      return;
    }
    // set new direction
    setDirection(aDirection);
  }
  // limit
  if (aPower>100) aPower=100;
  else if (aPower<0) aPower=0;
  // ramp to new value
  double rampRange = aPower-mCurrentPower;
  MLMicroSeconds totalRampTime;
  if (aRampTime<0) {
    // specification is 0..100 ramp time, scale according to power difference
    totalRampTime = fabs(rampRange)/100*(-aRampTime)*Second;
  }
  else {
    // absolute specification
    totalRampTime = aRampTime*Second;
  }
  int numSteps = (int)(totalRampTime/RAMP_STEP_TIME)+1;
  OLOG(LOG_INFO, "Ramp power from %.2f%% to %.2f%% in %lld uS (%d steps)", mCurrentPower, aPower, totalRampTime, numSteps);
  // now execute the ramp
  rampStep(mCurrentPower, aPower, numSteps, 0, aRampExp);
}



void DcMotorDriver::rampStep(double aStartPower, double aTargetPower, int aNumSteps, int aStepNo , double aRampExp)
{
  OLOG(LOG_DEBUG, "ramp step #%d/%d, %d%% of ramp", aStepNo, aNumSteps, aStepNo*100/aNumSteps);
  if (aStepNo++>=aNumSteps) {
    // finalize
    setPower(aTargetPower, mCurrentDirection);
    OLOG(LOG_INFO, "--- end of ramp");
    // update status + run callback
    motorStatusUpdate(ErrorPtr());
  }
  else {
    // set power for this step
    double f = (double)aStepNo/aNumSteps;
    if (aRampExp!=0) {
      f = (exp(f*aRampExp)-1)/(exp(aRampExp)-1);
    }
    // - scale the power
    double pwr = aStartPower + (aTargetPower-aStartPower)*f;
    OLOG(LOG_DEBUG, "- f=%.3f, pwr=%.2f", f, pwr);
    setPower(pwr, mCurrentDirection);
    // schedule next step
    mSequenceTicket.executeOnce(boost::bind(
      &DcMotorDriver::rampStep, this, aStartPower, aTargetPower, aNumSteps, aStepNo, aRampExp),
      RAMP_STEP_TIME
    );
  }
}


void DcMotorDriver::runSequence(SequenceStepList aSteps, DCMotorStatusCB aSequenceDoneCB)
{
  stopSequences();
  if (aSteps.size()==0) {
    // done
    if (aSequenceDoneCB) aSequenceDoneCB(mCurrentPower, mCurrentDirection, ErrorPtr());
  }
  // next step
  SequenceStep step = aSteps.front();
  rampToPower(step.power, step.direction, step.rampTime, step.rampExp, boost::bind(&DcMotorDriver::sequenceStepDone, this, aSteps, aSequenceDoneCB, _3));
}


void DcMotorDriver::sequenceStepDone(SequenceStepList aSteps, DCMotorStatusCB aSequenceDoneCB, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    // error, abort sequence
    if (aSequenceDoneCB) aSequenceDoneCB(mCurrentPower, mCurrentDirection, aError);
    return;
  }
  // launch next step after given run time
  SequenceStep step = aSteps.front();
  aSteps.pop_front();
  MainLoop::currentMainLoop().executeTicketOnce(mSequenceTicket, boost::bind(&DcMotorDriver::runSequence, this, aSteps, aSequenceDoneCB), step.runTime*Second);
}



// MARK: - script support

#if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

using namespace P44Script;

DcMotorStatusObj::DcMotorStatusObj(DcMotorDriverPtr aDcMotorDriver) :
  mDcMotorDriver(aDcMotorDriver)
{
  // create snapshot of status right now
  setMemberByName("power", new NumericValue(mDcMotorDriver->mCurrentPower));
  setMemberByName("direction", new IntegerValue(mDcMotorDriver->mCurrentDirection));
  if (mDcMotorDriver->mStopCause) {
    string cause;
    if (mDcMotorDriver->mStopCause->isError(DcMotorDriverError::domain(), DcMotorDriverError::overcurrentStop)) cause = "overcurrent";
    else if (mDcMotorDriver->mStopCause->isError(DcMotorDriverError::domain(), DcMotorDriverError::endswitchStop)) cause = "endswitch";
    else cause = mDcMotorDriver->mStopCause->text();
    setMemberByName("stoppedby", new StringValue(cause));
  }
  if (mDcMotorDriver->mCurrentSensor) {
    setMemberByName("current", new NumericValue(mDcMotorDriver->mCurrentSensor->lastValue()));
  }
}


void DcMotorStatusObj::deactivate()
{
  mDcMotorDriver.reset();
  inherited::deactivate();
}


string DcMotorStatusObj::getAnnotation() const
{
  return "DC motor event";
}


TypeInfo DcMotorStatusObj::getTypeInfo() const
{
  return inherited::getTypeInfo()|freezable; // can be frozen
}


EventSource* DcMotorStatusObj::eventSource() const
{
  return static_cast<EventSource*>(mDcMotorDriver.get());
}


// status()
static void status_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  f->finish(new DcMotorStatusObj(dc->dcMotor()));
}


// stop()
static void stop_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  dc->dcMotor()->stop();
  f->finish();
}


#define DEFAULT_CURRENT_POLL_INTERVAL (333*MilliSecond)

// currentsensor(sensor [, sampleinterval])
static const BuiltInArgDesc currentsensor_args[] = { { text|object }, { numeric|optionalarg } };
static const size_t currentsensor_numargs = sizeof(currentsensor_args)/sizeof(BuiltInArgDesc);
static void currentsensor_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  AnalogIoPtr sens = AnalogIoObj::analogIoFromArg(f->arg(0), false, 0);
  MLMicroSeconds interval = DEFAULT_CURRENT_POLL_INTERVAL; // sensible default
  if (f->arg(1)->defined()) interval = f->arg(1)->doubleValue()*Second;
  if (!sens) {
    interval = 0; // no polling if we have no sensor
  }
  dc->dcMotor()->setCurrentSensor(sens, interval);
  f->finish();
}

// currentlimit(limit [, startuptime [, maxstartcurrent]])
static const BuiltInArgDesc currentlimit_args[] = { { numeric }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t currentlimit_numargs = sizeof(currentlimit_args)/sizeof(BuiltInArgDesc);
static void currentlimit_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  double limit = f->arg(0)->doubleValue();
  MLMicroSeconds holdoff = 0;
  if (f->arg(1)->defined()) holdoff = f->arg(1)->doubleValue()*Second;
  double maxlimit = limit*2; // default to twice the normal limit
  if (f->arg(2)->defined()) maxlimit = f->arg(2)->doubleValue();
  dc->dcMotor()->setCurrentLimits(limit, holdoff, maxlimit);
  f->finish();
}

#define DEFAULT_ENDSWITCH_DEBOUNCE_TIME (80*MilliSecond)

// endswitches(positiveend, negativeend [, debouncetime [, pollinterval]])
static const BuiltInArgDesc endswitches_args[] = { { text|object|null }, { text|object|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t endswitches_numargs = sizeof(endswitches_args)/sizeof(BuiltInArgDesc);
static void endswitches_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  DigitalIoPtr pos = DigitalIoObj::digitalIoFromArg(f->arg(0), false, false);
  DigitalIoPtr neg = DigitalIoObj::digitalIoFromArg(f->arg(1), false, false);
  MLMicroSeconds interval = 0; // automatic
  MLMicroSeconds debouncetime = DEFAULT_ENDSWITCH_DEBOUNCE_TIME; // usually, we need some debounce to avoid stopping while driving out of end switch
  if (f->arg(2)->defined()) debouncetime = f->arg(2)->doubleValue()*Second;
  if (f->arg(3)->defined()) interval = f->arg(2)->doubleValue()*Second;
  dc->dcMotor()->setEndSwitches(pos, neg, debouncetime, interval);
  f->finish();
}


// outputparams(scaling [, offset])
static const BuiltInArgDesc outputparams_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t outputparams_numargs = sizeof(outputparams_args)/sizeof(BuiltInArgDesc);
static void outputparams_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  dc->dcMotor()->setOutputParams(f->arg(0)->doubleValue(), f->arg(1)->doubleValue());
  f->finish();
}


// power(power [, direction [, ramptime [, rampexponent]]])
static const BuiltInArgDesc power_args[] = { { numeric }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t power_numargs = sizeof(power_args)/sizeof(BuiltInArgDesc);
static void power_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  double power = f->arg(0)->doubleValue();
  int direction = 1; // default to forward (simplest case, just unidirectional DC motor)
  double ramptime = -1; // default to 1 second full scale ramp. NOTE: THESE ARE SECONDS, not MLMicroSeconds!
  double rampexponent = 1; // default to linear ramp
  if (f->arg(1)->defined()) direction = f->arg(1)->intValue();
  if (f->arg(2)->defined()) ramptime = f->arg(2)->doubleValue(); // NOTE: THESE ARE SECONDS, not MLMicroSeconds!
  if (f->arg(3)->defined()) rampexponent = f->arg(3)->doubleValue();
  dc->dcMotor()->rampToPower(power, direction, ramptime, rampexponent);
  f->finish();
}


static const BuiltinMemberDescriptor dcmotorFunctions[] = {
  { "outputparams", executable|null, outputparams_numargs, outputparams_args, &outputparams_func },
  { "endswitches", executable|null, endswitches_numargs, endswitches_args, &endswitches_func },
  { "currentsensor", executable|null, currentsensor_numargs, currentsensor_args, &currentsensor_func },
  { "currentlimit", executable|null, currentlimit_numargs, currentlimit_args, &currentlimit_func },
  { "power", executable|null, power_numargs, power_args, &power_func },
  { "status", executable|object, 0, NULL, &status_func },
  { "stop", executable|null, 0, NULL, &stop_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedDCMotorFunctionLookupP = NULL;

DcMotorObj::DcMotorObj(DcMotorDriverPtr aDCMotor) :
  mDCMotor(aDCMotor)
{
  registerSharedLookup(sharedDCMotorFunctionLookupP, dcmotorFunctions);
}




// dcmotor(output [, CWdirection [, CCWdirection]])
static const BuiltInArgDesc dcmotor_args[] = { { text|object }, { text|object|optionalarg }, { text|object|optionalarg } };
static const size_t dcmotor_numargs = sizeof(dcmotor_args)/sizeof(BuiltInArgDesc);
static void dcmotor_func(BuiltinFunctionContextPtr f)
{
  AnalogIoPtr power = AnalogIoObj::analogIoFromArg(f->arg(0), true, 0);
  if (!power) {
    f->finish(new ErrorValue(ScriptError::Invalid, "missing analog output"));
    return;
  }
  DigitalIoPtr cwd = DigitalIoObj::digitalIoFromArg(f->arg(1), true, false);
  DigitalIoPtr ccwd = DigitalIoObj::digitalIoFromArg(f->arg(2), true, false);
  DcMotorDriverPtr dcmotor = new DcMotorDriver(power, cwd, ccwd);
  f->finish(new DcMotorObj(dcmotor));
}


static const BuiltinMemberDescriptor dcmotorGlobals[] = {
  { "dcmotor", executable|null, dcmotor_numargs, dcmotor_args, &dcmotor_func },
  { NULL } // terminator
};

DcMotorLookup::DcMotorLookup() :
  inherited(dcmotorGlobals)
{
}


#endif // ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT
