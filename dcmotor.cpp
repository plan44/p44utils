//
//  Copyright (c) 2017-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
  currentPower(0),
  currentDirection(0)
{
  pwmOutput = aPWMOutput;
  // - direction control
  cwDirectionOutput = aCWDirectionOutput;
  ccwDirectionOutput = aCCWDirectionOutput;
  setPower(0, 0);
}



DcMotorDriver::~DcMotorDriver()
{
  // stop power to motor
  setPower(0, 0);
}


void DcMotorDriver::setStopCallback(DCMotorStatusCB aStoppedCB)
{
  stoppedCB = aStoppedCB;
}



void DcMotorDriver::setEndSwitches(DigitalIoPtr aPositiveEnd, DigitalIoPtr aNegativeEnd, MLMicroSeconds aPollInterval)
{
  mPositiveEndInput = aPositiveEnd;
  mNegativeEndInput = aNegativeEnd;
  if (mPositiveEndInput) mPositiveEndInput->setInputChangedHandler(boost::bind(&DcMotorDriver::endSwitch, this, true), 0, aPollInterval);
  if (mNegativeEndInput) mNegativeEndInput->setInputChangedHandler(boost::bind(&DcMotorDriver::endSwitch, this, false), 0, aPollInterval);
}


void DcMotorDriver::endSwitch(bool aPositiveEnd)
{
  // save direction and power as known BEFORE stopping
  double pwr = currentPower;
  int dir = currentDirection;
  stop();
  LOG(LOG_INFO, "stopped with power=%.2f, direction=%d because %s end switch reached", pwr, dir, aPositiveEnd ? "positive" : "negative");
  autoStopped(pwr, dir);
  return;
}


void DcMotorDriver::autoStopped(double aPower, int aDirection)
{
  if (stoppedCB) {
    // stop callback does not reset
    stoppedCB(aPower, aDirection, new DcMotorDriverError(DcMotorDriverError::endswitchStop));
  }
  if (mRampDoneCB) {
    // ramp done CB must be set for every ramp separately
    DCMotorStatusCB cb = mRampDoneCB;
    mRampDoneCB = NULL;
    cb(aPower, aDirection, new DcMotorDriverError(DcMotorDriverError::endswitchStop));
  }
}

void DcMotorDriver::setCurrentLimiter(AnalogIoPtr aCurrentSensor, double aStopCurrent, MLMicroSeconds aSampleInterval)
{
  // - current sensor
  currentSensor = aCurrentSensor;
  stopCurrent = aStopCurrent;
  sampleInterval = aSampleInterval;
}


void DcMotorDriver::setDirection(int aDirection)
{
  if (cwDirectionOutput) {
    cwDirectionOutput->set(aDirection>0);
    if (ccwDirectionOutput) {
      ccwDirectionOutput->set(aDirection<0);
    }
  }
  if (aDirection!=currentDirection) {
    OLOG(LOG_INFO, "Direction changed to %d", aDirection);
    currentDirection = aDirection;
  }
}



void DcMotorDriver::setPower(double aPower, int aDirection)
{
  if (aPower<=0) {
    // no power
    // - disable PWM
    pwmOutput->setValue(0);
    // - off (= hold/brake with no power)
    setDirection(0);
    // disable current sampling
    if (currentSensor) currentSensor->setAutopoll(0);
  }
  else {
    // determine current direction
    if (currentDirection!=0 && aDirection!=0 && aDirection!=currentDirection) {
      // avoid reversing direction with power on
      pwmOutput->setValue(0);
      setDirection(0);
    }
    // check end switch, do not allow setting power towards already active end switch
    if (
      (aDirection<0 && mNegativeEndInput && mNegativeEndInput->isSet()) ||
      (aDirection>0 && mPositiveEndInput && mPositiveEndInput->isSet())
    ) {
      OLOG(LOG_INFO, "Cannot start in direction %d, endswitch is active", aDirection);
      // count this as reaching end and cause callback to fire
      endSwitch(aDirection>0);
      return;
    }
    // now set desired direction and power
    setDirection(aDirection);
    pwmOutput->setValue(aPower);
    // start current sampling
    if (currentSensor) {
      currentSensor->setAutopoll(sampleInterval, sampleInterval/4, boost::bind(&DcMotorDriver::checkCurrent, this));
    }
  }
  if (aPower!=currentPower) {
    OLOG(LOG_DEBUG, "Power changed to %.2f%%", aPower);
    currentPower = aPower;
  }
}


void DcMotorDriver::checkCurrent()
{
  double v = fabs(currentSensor->processedValue()); // takes abs, in case we're not using processing that already takes abs values
  OLOG(LOG_DEBUG, "checkCurrent: processed: %.3f, last raw value: %.3f", v, currentSensor->lastValue());
  if (v>=stopCurrent) {
    double pwr = currentPower;
    int dir = currentDirection;
    stop();
    OLOG(LOG_INFO, "stopped because processed current (%.3f) exceeds max (%.3f) - last raw sample = %.3f", v, stopCurrent, currentSensor->lastValue());
    autoStopped(pwr, dir);
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
  sequenceTicket.cancel();
}



void DcMotorDriver::rampToPower(double aPower, int aDirection, double aRampTime, double aRampExp, DCMotorStatusCB aRampDoneCB)
{
  OLOG(LOG_INFO, "+++ new ramp: power: %.2f%%..%.2f%%, direction:%d..%d with ramp time %.3f Seconds, exp=%.2f", currentPower, aPower, currentDirection, aDirection, aRampTime, aRampExp);
  mRampDoneCB = aRampDoneCB;
  MainLoop::currentMainLoop().cancelExecutionTicket(sequenceTicket);
  if (aDirection!=currentDirection) {
    if (currentPower!=0) {
      // ramp to zero first, then ramp up to new direction
      OLOG(LOG_INFO, "Ramp trough different direction modes -> first ramp power down, then up again");
      if (aRampTime>0) aRampTime /= 2; // for absolute ramp time specificiation, just use half of the time for ramp up or down, resp. 
      rampToPower(0, currentDirection, aRampTime, aRampExp, boost::bind(&DcMotorDriver::rampToPower, this, aPower, aDirection, aRampTime, aRampExp, aRampDoneCB));
      return;
    }
    // set new direction
    setDirection(aDirection);
  }
  // limit
  if (aPower>100) aPower=100;
  else if (aPower<0) aPower=0;
  // ramp to new value
  double rampRange = aPower-currentPower;
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
  OLOG(LOG_INFO, "Ramp power from %.2f%% to %.2f%% in %lld uS (%d steps)", currentPower, aPower, totalRampTime, numSteps);
  // now execute the ramp
  rampStep(currentPower, aPower, numSteps, 0, aRampExp);
}



void DcMotorDriver::rampStep(double aStartPower, double aTargetPower, int aNumSteps, int aStepNo , double aRampExp)
{
  OLOG(LOG_DEBUG, "ramp step #%d/%d, %d%% of ramp", aStepNo, aNumSteps, aStepNo*100/aNumSteps);
  if (aStepNo++>=aNumSteps) {
    // finalize
    setPower(aTargetPower, currentDirection);
    OLOG(LOG_INFO, "--- end of ramp");
    // call back
    if (mRampDoneCB) mRampDoneCB(currentPower, currentDirection, ErrorPtr());
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
    setPower(pwr, currentDirection);
    // schedule next step
    sequenceTicket.executeOnce(boost::bind(
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
    if (aSequenceDoneCB) aSequenceDoneCB(currentPower, currentDirection, ErrorPtr());
  }
  // next step
  SequenceStep step = aSteps.front();
  rampToPower(step.power, step.direction, step.rampTime, step.rampExp, boost::bind(&DcMotorDriver::sequenceStepDone, this, aSteps, aSequenceDoneCB, _3));
}


void DcMotorDriver::sequenceStepDone(SequenceStepList aSteps, DCMotorStatusCB aSequenceDoneCB, ErrorPtr aError)
{
  if (!Error::isOK(aError)) {
    // error, abort sequence
    if (aSequenceDoneCB) aSequenceDoneCB(currentPower, currentDirection, aError);
    return;
  }
  // launch next step after given run time
  SequenceStep step = aSteps.front();
  aSteps.pop_front();
  MainLoop::currentMainLoop().executeTicketOnce(sequenceTicket, boost::bind(&DcMotorDriver::runSequence, this, aSteps, aSequenceDoneCB), step.runTime*Second);
}





// MARK: - script support

#if ENABLE_DCMOTOR_SCRIPT_FUNCS  && ENABLE_P44SCRIPT

using namespace P44Script;

DcMotorEventObj::DcMotorEventObj(DcMotorObj* aDcMotorObj) :
  inherited(false),
  mDcMotorObj(aDcMotorObj)
{
}


string DcMotorEventObj::getAnnotation() const
{
  return "DC motor event";
}


TypeInfo DcMotorEventObj::getTypeInfo() const
{
  return inherited::getTypeInfo()|oneshot|keeporiginal; // returns the request only once, must keep the original
}


EventSource* DcMotorEventObj::eventSource() const
{
  return static_cast<EventSource*>(mDcMotorObj);
}


//double DcMotorEventObj::doubleValue() const
//{
//  return mDcMotorObj && mDcMotorObj->digitalIo()->isSet() ? 1 : 0;
//}



// %%% toggle()
static void toggle_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  f->finish();
}


// %%% state() // get state (has event source)
// state(newstate) // set state
static const BuiltInArgDesc state_args[] = { { numeric|optionalarg } };
static const size_t state_numargs = sizeof(state_args)/sizeof(BuiltInArgDesc);
static void state_func(BuiltinFunctionContextPtr f)
{
  DcMotorObj* dc = dynamic_cast<DcMotorObj*>(f->thisObj().get());
  assert(dc);
  f->finish();
}


static const BuiltinMemberDescriptor dcmotorFunctions[] = {
  { "state", executable|numeric, state_numargs, state_args, &state_func },
  { "toggle", executable|numeric, 0, NULL, &toggle_func },
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
