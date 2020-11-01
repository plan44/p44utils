//
//  Copyright (c) 2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 0

#include "valueanimator.hpp"

#include <math.h>

using namespace p44;


// MARK: ValueAnimator

ValueAnimator::ValueAnimator(ValueSetterCB aValueSetter, bool aSelfTiming, MLMicroSeconds aDefaultMinStepTime) :
  valueSetter(aValueSetter),
  selfTiming(aSelfTiming),
  defaultMinStepTime(aDefaultMinStepTime>0 ? ANIMATION_MIN_STEP_TIME : aDefaultMinStepTime),
  animationFunction(NULL),
  animationParam(3),
  startValue(0),
  distance(0),
  currentValue(0),
  startedAt(Never),
  cycles(0),
  autoreverse(false),
  awaitingTrigger(false),
  startTimeOrDelay(0),
  absoluteStartTime(false)
{
}


ValueAnimator::~ValueAnimator()
{
  internalStop(false, true);
}


bool ValueAnimator::valid()
{
  return valueSetter;
}


bool ValueAnimator::inProgress()
{
  return valid() && (startedAt!=Never || startTimeOrDelay!=0 || awaitingTrigger);
}


void ValueAnimator::stop(bool aAndReport)
{
  internalStop(aAndReport, false);
}


void ValueAnimator::internalStop(bool aCallback, bool aCompleted)
{
  if (startedAt!=Never) {
    FOCUSLOG("=== Animation stops with value=%3.2f, completed=%d, callback=%d", currentValue, aCompleted, aCallback);
    startedAt = Never;
    cycles = 0;
    awaitingTrigger = false;
    startTimeOrDelay = 0;
    animationTimer.cancel();
    if (!triggerAnimations.empty()) {
      // trigger next animations
      FOCUSLOG("=== Animation triggers %d other animations", triggerAnimations.size());
      for (AnimatorList::iterator pos=triggerAnimations.begin(); pos!=triggerAnimations.end(); ++pos) {
        (*pos)->trigger();
      }
    }
    if (aCallback && doneCB) {
      AnimationDoneCB cb = doneCB;
      doneCB = NULL;
      cb(currentValue, aCompleted);
    }
  }
}


ValueAnimatorPtr ValueAnimator::from(double aFrom)
{
  internalStop(true, false); // abort previous animation, if any
  startValue = aFrom;
  return this;
}


ValueAnimatorPtr ValueAnimator::repeat(bool aAutoReverse, int aCycles)
{
  internalStop(true, false); // abort previous animation, if any
  autoreverse = aAutoReverse;
  cycles = aCycles<=0 ? -1 : aCycles;
  return this;
}


ValueAnimatorPtr ValueAnimator::function(AnimationFunction aAnimationFunction)
{
  animationFunction = aAnimationFunction;
  animationParam = 0;
  return this;
}


ValueAnimatorPtr ValueAnimator::function(const string aAnimationType)
{
  animationParam = 3; // current default for our ease function
  if (aAnimationType=="easein") {
    animationFunction = &easeIn;
  }
  else if (aAnimationType=="easeout") {
    animationFunction = &easeOut;
  }
  else if (aAnimationType=="easeinout") {
    animationFunction = &easeInOut;
  }
  else {
    animationFunction = &linear;
  }
  return this;
}


ValueAnimatorPtr ValueAnimator::param(double aAnimationParam)
{
  animationParam = aAnimationParam;
  return this;
}



ValueAnimatorPtr ValueAnimator::startTime(MLMicroSeconds aStartTime)
{
  if (startedAt==Never) {
    absoluteStartTime = true;
    startTimeOrDelay = aStartTime;
  }
  return this;
}


ValueAnimatorPtr ValueAnimator::startDelay(MLMicroSeconds aStartDelay)
{
  if (startedAt==Never) {
    absoluteStartTime = false;
    startTimeOrDelay = aStartDelay;
  }
  return this;
}


ValueAnimatorPtr ValueAnimator::runAfter(ValueAnimatorPtr aPreceedingAnimation)
{
  // have preceeding animation trigger myself when it is done
  if (aPreceedingAnimation) {
    aPreceedingAnimation->triggerAnimations.push_back(this);
    awaitingTrigger = true;
  }
  return this;
}


MLMicroSeconds ValueAnimator::animate(double aTo, MLMicroSeconds aDuration, AnimationDoneCB aDoneCB, MLMicroSeconds aMinStepTime, double aStepSize)
{
  internalStop(true, false); // abort previous animation, if any
  currentValue = startValue;
  duration = aDuration;
  doneCB = aDoneCB;
  if (!valueSetter) {
    // cannot do anything
    internalStop(true, false);
    return Infinite; // no need to call step()
  }
  // precalculate operating params
  distance = aTo-startValue;
  if (!animationFunction) animationFunction = &linear; // default to linear
  stepTime = aMinStepTime>0 ? aMinStepTime : defaultMinStepTime; // default to not-too-small steps
  if (cycles==0) {
    // not yet set by repeat() -> default operation
    cycles = 1;
    autoreverse = false;
  }
  // calculate steps
  int steps = (int)(duration/stepTime);
  if (aStepSize>0) {
    int sizedsteps = distance/aStepSize;
    if (sizedsteps<steps) {
      // given step size allows less frequent steps
      steps = sizedsteps;
      if (steps>0) stepTime = duration/steps;
    }
  }
  if (steps==0) stepTime = 0; // signals no steps for start()
  // is startable now
  if (awaitingTrigger) return Infinite; // ..but needs to wait for trigger first
  // trigger right now
  return trigger();
}


MLMicroSeconds ValueAnimator::trigger()
{
  if (startTimeOrDelay) {
    if (!absoluteStartTime) {
      // make absolute
      startTimeOrDelay = MainLoop::now()+startTimeOrDelay;
      absoluteStartTime = true;
    }
    FOCUSLOG("=== Triggered Animation, but must await delay of %.2f seconds", (double)(startTimeOrDelay-MainLoop::now())/Second);
    if (selfTiming) {
      // schedule a timer to start
      animationTimer.executeOnceAt(boost::bind(&ValueAnimator::start, this), startTimeOrDelay);
    }
    awaitingTrigger = false; // not waiting for trigger any more
    return startTimeOrDelay; // this is when we need to start
  }
  // can start right now
  return start();
}


MLMicroSeconds ValueAnimator::start()
{
  awaitingTrigger = false; // not waiting for trigger any more
  startTimeOrDelay = 0; // no delay any more, step() will run animation
  absoluteStartTime = false;
  FOCUSLOG("=== Start Animation: from=%3.2f, distance=%3.2f, to=%3.2f, duration=%2.3f S, stepTime=%2.3f S", startValue, distance, startValue+distance, (double)duration/Second, (double)stepTime/Second);
  startedAt = MainLoop::now();
  // start animation or just finish it if there are no steps (no step time)
  if (stepTime>0) {
    // set current value
    valueSetter(currentValue);
    MLMicroSeconds nextStep = startedAt+stepTime;
    if (selfTiming) {
      animationTimer.executeOnceAt(boost::bind(&ValueAnimator::autoStep, this, _1, _2), nextStep);
    }
    return nextStep;
  }
  // immediately done (no steps)
  return cycleComplete(startedAt);
}


MLMicroSeconds ValueAnimator::cycleComplete(MLMicroSeconds aNow)
{
  // set precise end value
  FOCUSLOG(">>> Animation Cycle completes after %3.3f S, with last value=%3.2f, final value=%3.2f, cycles=%d, autoreverse=%d", (double)(aNow-startedAt)/Second, currentValue, startValue+distance, cycles, autoreverse);
  currentValue = startValue+distance;
  valueSetter(currentValue);
  // check cycles
  if (cycles>0) cycles--;
  if (cycles>0 || cycles<0) {
    // continues
    if (autoreverse) {
      startValue = currentValue;
      distance = -distance;
    }
    else {
      // back to start
      currentValue = startValue;
    }
    // continue stepping
    startedAt = aNow;
    return aNow+stepTime;
  }
  internalStop(true, true);
  return Infinite; // no need to call step() (again)
}


MLMicroSeconds ValueAnimator::step()
{
  if (awaitingTrigger) return Infinite; // still waiting for getting triggered
  MLMicroSeconds now = MainLoop::now();
  if (startTimeOrDelay) {
    // delayed start
    if (now<startTimeOrDelay) {
      // still waiting for start
      return startTimeOrDelay; // need to be called again at start time
    }
    // now actually start running
    return start();
  }
  if (!inProgress()) return Infinite;
  double progress = (double)(now-startedAt)/duration;
  if (progress>=1) {
    // reached end of cycle
    return cycleComplete(now);
  }
  else if (progress<0) {
    progress = 0; // should not happen
  }
  // cycle continues
  double fprog = animationFunction(progress, animationParam);
  FOCUSLOG("--- Animation step: time since cycle start: %3.3f S, start=%3.2f, distance=%+3.2f: progress %.3f -> %.3f (delta = %.3f) -> newValue=%3.2f", (double)(now-startedAt)/Second, startValue, distance, progress, fprog, startValue+distance*fprog-currentValue, startValue+distance*fprog);
  currentValue = startValue+distance*fprog;
  valueSetter(currentValue);
  return now+stepTime;
}


void ValueAnimator::autoStep(MLTimer &aTimer, MLMicroSeconds aNow)
{
  MLMicroSeconds nextStep = step();
  if (nextStep!=Never) {
    MainLoop::currentMainLoop().retriggerTimer(aTimer, stepTime);
  }
}


// MARK: Animation Functions

double ValueAnimator::linear(double aProgress, double aTuning)
{
  return aProgress;
}


// From: https://hackernoon.com/ease-in-out-the-sigmoid-factory-c5116d8abce9
// y = f(x) = (0.5 / s(1,k)) * s(2*x,k) + 0.5
// s(t,a) = 1/(1+exp(-a*t)) - 0.5

static inline double s(double a, double t)
{
   return 1/(1+exp(-a*t)) - 0.5;
}

static double sigmoid(double t, double k)
{
  return (0.5 / s(1,k)) * s(2*t-1,k) + 0.5;
}



double ValueAnimator::easeIn(double aProgress, double aTuning)
{
  return 2*sigmoid(aProgress/2, aTuning); // first half 0..0.5
}


double ValueAnimator::easeOut(double aProgress, double aTuning)
{
  return 2*sigmoid(aProgress/2+0.5, aTuning); // second half 0.5..1
}


double ValueAnimator::easeInOut(double aProgress, double aTuning)
{
  return sigmoid(aProgress, aTuning);
}


#if ENABLE_ANIMATOR_SCRIPT_FUNCS

using namespace P44Script;

// .delay(startdelay)
static const BuiltInArgDesc delay_args[] = { { numeric } };
static const size_t delay_numargs = sizeof(delay_args)/sizeof(BuiltInArgDesc);
static void delay_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->startDelay(f->arg(0)->doubleValue()*Second);
  f->finish(a); // return myself for chaining calls
}

// .runafter(animator)
static const BuiltInArgDesc runafter_args[] = { { any } };
static const size_t runafter_numargs = sizeof(runafter_args)/sizeof(BuiltInArgDesc);
static void runafter_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObjPtr a = boost::dynamic_pointer_cast<ValueAnimatorObj>(f->thisObj());
  assert(a);
  ValueAnimatorObjPtr after = boost::dynamic_pointer_cast<ValueAnimatorObj>(f->arg(0));
  if (!after) {
    f->finish(new ErrorValue(ScriptError::Invalid, "argument must be an animator"));
    return;
  }
  a->animator()->runAfter(after->animator());
  f->finish(a); // return myself for chaining calls
}

// .repeat(repetitions [,autoreverse])
static const BuiltInArgDesc repeat_args[] = { { numeric }, { numeric|optionalarg } };
static const size_t repeat_numargs = sizeof(repeat_args)/sizeof(BuiltInArgDesc);
static void repeat_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->repeat(f->arg(1)->boolValue(), f->arg(0)->doubleValue());
  f->finish(a); // return myself for chaining calls
}

// .function(animationfunctionname [, animationfunctionparam])
static const BuiltInArgDesc function_args[] = { { text }, { numeric|optionalarg } };
static const size_t function_numargs = sizeof(function_args)/sizeof(BuiltInArgDesc);
static void function_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->function(f->arg(0)->stringValue());
  if (f->numArgs()>1) {
    a->animator()->param(f->arg(1)->doubleValue());
  }
  f->finish(a); // return myself for chaining calls
}

// .from(initialvalue)
static const BuiltInArgDesc from_args[] = { { numeric } };
static const size_t from_numargs = sizeof(from_args)/sizeof(BuiltInArgDesc);
static void from_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->from(f->arg(0)->doubleValue());
  f->finish(a); // return myself for chaining calls
}

// .runto(endvalue, intime [, minsteptime])    actually start animation
static void animation_complete(ValueAnimatorObjPtr aAnimationObj, double aReachedValue, bool aCompleted)
{
  aAnimationObj->sendEvent(new NumericValue(aCompleted));
}
static const BuiltInArgDesc runto_args[] = { { numeric }, { numeric }, { numeric|optionalarg } };
static const size_t runto_numargs = sizeof(runto_args)/sizeof(BuiltInArgDesc);
static void runto_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObjPtr a = boost::dynamic_pointer_cast<ValueAnimatorObj>(f->thisObj());
  assert(a);
  MLMicroSeconds minStepTime = 0;
  if (f->numArgs()>2) minStepTime = f->arg(2)->doubleValue()*Second;
  a->animator()->animate(f->arg(0)->doubleValue(), f->arg(1)->doubleValue()*Second, boost::bind(&animation_complete, a, _1, _2), minStepTime);
  f->finish(); // no return value
}

// .stop()
static void stop_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->stop();
  f->finish(); // no return value
}


static ScriptObjPtr current_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(aParentObj.get());
  return new NumericValue(a->animator()->current());
}


static const BuiltinMemberDescriptor animatorFunctions[] = {
  { "delay", executable|any, delay_numargs, delay_args, &delay_func },
  { "runafter", executable|null, runafter_numargs, runafter_args, &runafter_func },
  { "repeat", executable|any, repeat_numargs, repeat_args, &repeat_func },
  { "function", executable|any, function_numargs, function_args, &function_func },
  { "from", executable|any, from_numargs, from_args, &from_func },
  { "runto", executable|null, runto_numargs, runto_args, &runto_func },
  { "stop", executable|any, 0, NULL, &stop_func },
  { "current", builtinmember|numeric, 0, NULL, (BuiltinFunctionImplementation)&current_accessor }, // Note: correct '.accessor=&lrg_accessor' form does not work with OpenWrt g++, so need ugly cast here
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedAnimatorFunctionLookupP = NULL;

ValueAnimatorObj::ValueAnimatorObj(ValueAnimatorPtr aAnimator) :
  mAnimator(aAnimator)
{
  if (sharedAnimatorFunctionLookupP==NULL) {
    sharedAnimatorFunctionLookupP = new BuiltInMemberLookup(animatorFunctions);
    sharedAnimatorFunctionLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedAnimatorFunctionLookupP);
}

EventSource* ValueAnimatorObj::eventSource() const
{
  if (!mAnimator->inProgress()) return NULL; // no longer running -> no event source any more
  return static_cast<EventSource*>(const_cast<ValueAnimatorObj*>(this));
}

#endif // ENABLE_ANIMATOR_SCRIPT_FUNCS
