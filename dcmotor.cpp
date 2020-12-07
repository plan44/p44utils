//
//  Copyright (c) 2017-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Lukas Zeller <luz@plan44.ch>
//
//  This file is part of p44wiperd.
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


#pragma mark - DCMotorDriver

DcMotorDriver::DcMotorDriver(const char *aPWMOutput, const char *aCWDirectionOutput, const char *aCCWDirectionOutput) :
  currentPower(0),
  currentDirection(0)
{
  pwmOutput = AnalogIoPtr(new AnalogIo(aPWMOutput, true, 0)); // off to begin with
  // - direction control
  if (aCWDirectionOutput) {
    cwDirectionOutput = DigitalIoPtr(new DigitalIo(aCWDirectionOutput, true, false));
    if (aCCWDirectionOutput) {
      ccwDirectionOutput = DigitalIoPtr(new DigitalIo(aCCWDirectionOutput, true, false));
    }
  }
  setPower(0, 0);
}


DcMotorDriver::~DcMotorDriver()
{
  // stop power to motor
  setPower(0, 0);
}


void DcMotorDriver::setCurrentLimiter(const char *aCurrentSensor, double aStopCurrent, MLMicroSeconds aSampleInterval, SimpleCB aStoppedCB)
{
  // - current sensor
  if (aCurrentSensor) {
    currentSensor = AnalogIoPtr(new AnalogIo(aCurrentSensor, false, 0));
  }
  stopCurrent = aStopCurrent;
  sampleInterval = aSampleInterval;
  stoppedCB = aStoppedCB;
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
    LOG(LOG_DEBUG, "Direction changed to %d", aDirection);
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
    sampleTicket.cancel();
  }
  else {
    // determine current direction
    if (currentDirection!=0 && aDirection!=0 && aDirection!=currentDirection) {
      // avoid reversing direction with power on
      pwmOutput->setValue(0);
      setDirection(0);
    }
    // now set desired direction and power
    setDirection(aDirection);
    pwmOutput->setValue(aPower);
    // start current sampling
    if (currentSensor) {
      lastCurrent = 0;
      sampleTicket.executeOnce(boost::bind(&DcMotorDriver::checkCurrent, this, _1), sampleInterval);
    }
  }
  if (aPower!=currentPower) {
    LOG(LOG_DEBUG, "Power changed to %.2f%%", aPower);
    currentPower = aPower;
  }
}


void DcMotorDriver::checkCurrent(MLTimer &aTimer)
{
  double v = fabs(currentSensor->value()); // driving or braking!
  double av = (v+lastCurrent)/2; // average
  lastCurrent = v;
  LOG(LOG_DEBUG, "checkCurrent: sampled: %.3f, average over 2: %.3f", v, av);
  if (av>=stopCurrent) {
    stop();
    LOG(LOG_INFO, "stopped because averaged current (%.3f) exceeds max (%.3f) - last sample = %.3f", av, stopCurrent, v);
    if (stoppedCB) stoppedCB();
    return;
  }
  // continue sampling
  MainLoop::currentMainLoop().retriggerTimer(aTimer, sampleInterval);
}



#define RAMP_STEP_TIME (20*MilliSecond)


void DcMotorDriver::stop()
{
  stopSequences();
  setPower(0, 0);
}


void DcMotorDriver::stopSequences()
{
  MainLoop::currentMainLoop().cancelExecutionTicket(sequenceTicket);
}



void DcMotorDriver::rampToPower(double aPower, int aDirection, double aRampTime, double aRampExp, DCMotorStatusCB aRampDoneCB)
{
  LOG(LOG_DEBUG, "+++ new ramp: power: %.2f%%..%.2f%%, direction:%d..%d with ramp time %.3f Seconds, exp=%.2f", currentPower, aPower, currentDirection, aDirection, aRampTime, aRampExp);
  MainLoop::currentMainLoop().cancelExecutionTicket(sequenceTicket);
  if (aDirection!=currentDirection) {
    if (currentPower!=0) {
      // ramp to zero first, then ramp up to new direction
      LOG(LOG_DEBUG, "Ramp trough different direction modes -> first ramp power down, then up again");
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
  LOG(LOG_DEBUG, "Ramp power from %.2f%% to %.2f%% in %lld uS (%d steps)", currentPower, aPower, totalRampTime, numSteps);
  // now execute the ramp
  rampStep(currentPower, aPower, numSteps, 0, aRampExp, aRampDoneCB);
}



void DcMotorDriver::rampStep(double aStartPower, double aTargetPower, int aNumSteps, int aStepNo , double aRampExp, DCMotorStatusCB aRampDoneCB)
{
  LOG(LOG_DEBUG, "ramp step #%d/%d, %d%% of ramp", aStepNo, aNumSteps, aStepNo*100/aNumSteps);
  if (aStepNo++>=aNumSteps) {
    // finalize
    setPower(aTargetPower, currentDirection);
    LOG(LOG_DEBUG, "--- end of ramp");
    // call back
    if (aRampDoneCB) aRampDoneCB(currentPower, currentDirection, ErrorPtr());
  }
  else {
    // set power for this step
    double f = (double)aStepNo/aNumSteps;
    if (aRampExp!=0) {
      f = (exp(f*aRampExp)-1)/(exp(aRampExp)-1);
    }
    // - scale the power
    double pwr = aStartPower + (aTargetPower-aStartPower)*f;
    LOG(LOG_DEBUG, "- f=%.3f, pwr=%.2f", f, pwr);
    setPower(pwr, currentDirection);
    // schedule next step
    sequenceTicket.executeOnce(boost::bind(
      &DcMotorDriver::rampStep, this, aStartPower, aTargetPower, aNumSteps, aStepNo, aRampExp, aRampDoneCB),
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







