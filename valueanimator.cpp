//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2020-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 7

#include "valueanimator.hpp"

#include <math.h>

using namespace p44;


// MARK: ValueAnimator

ValueAnimator::ValueAnimator(ValueSetterCB aValueSetter, bool aSelfTiming, MLMicroSeconds aDefaultMinStepTime) :
  mValueSetter(aValueSetter),
  mSelfTiming(aSelfTiming),
  mDefaultMinStepTime(aDefaultMinStepTime>0 ? aDefaultMinStepTime : ANIMATION_MIN_STEP_TIME),
  mStepSize(0), // auto-calculate step size
  mStartValue(0),
  mDistance(0),
  mCurrentValue(0),
  mStartedAt(Never),
  mCycles(0),
  mAwaitingTrigger(false),
  mStartTimeOrDelay(0),
  mAbsoluteStartTime(false)
{
  reset();
}


void ValueAnimator::reset()
{
  mMinStepTime = mDefaultMinStepTime;
  mRepeat = 1;
  mAutoreverse = false;
  mAnimationFunction = NULL;
  mAnimationParam = 3;
}



ValueAnimator::~ValueAnimator()
{
  internalStop(false, true);
}


bool ValueAnimator::valid()
{
  return mValueSetter!=NoOP;
}


bool ValueAnimator::inProgress()
{
  return valid() && (mStartedAt!=Never || mStartTimeOrDelay!=0 || mAwaitingTrigger);
}


void ValueAnimator::stop(bool aAndReport)
{
  internalStop(aAndReport, false);
}


void ValueAnimator::internalStop(bool aCallback, bool aCompleted)
{
  if (mStartedAt!=Never) {
    FOCUSLOG("=== Animation stops with value=%3.2f, completed=%d, callback=%d", mCurrentValue, aCompleted, aCallback);
    mStartedAt = Never;
    mStartValue = mCurrentValue; // save for re-starting the animation
    mCycles = 0;
    mAwaitingTrigger = false;
    mStartTimeOrDelay = 0;
    mAnimationTimer.cancel();
    if (!mTriggerAnimations.empty()) {
      // trigger next animations
      FOCUSLOG("=== Animation triggers %zu other animations", mTriggerAnimations.size());
      for (AnimatorList::iterator pos=mTriggerAnimations.begin(); pos!=mTriggerAnimations.end(); ++pos) {
        (*pos)->trigger();
      }
    }
    if (aCallback && mDoneCB) {
      AnimationDoneCB cb = mDoneCB;
      mDoneCB = NoOP;
      cb(mCurrentValue, aCompleted);
    }
  }
}


ValueAnimatorPtr ValueAnimator::from(double aFrom)
{
  internalStop(true, false); // abort previous animation, if any
  mStartValue = aFrom;
  return this;
}


ValueAnimatorPtr ValueAnimator::stepParams(MLMicroSeconds aMinStepTime, double aStepSize)
{
  if (aMinStepTime>0) mMinStepTime = aMinStepTime;
  mStepSize = aStepSize;
  return this;
}


ValueAnimatorPtr ValueAnimator::repeat(bool aAutoReverse, int aRepeat)
{
  internalStop(true, false); // abort previous animation, if any
  mAutoreverse = aAutoReverse;
  mRepeat = aRepeat<=0 ? -1 : aRepeat;
  return this;
}


ValueAnimatorPtr ValueAnimator::function(AnimationFunction aAnimationFunction)
{
  mAnimationFunction = aAnimationFunction;
  mAnimationParam = 0;
  return this;
}


ValueAnimatorPtr ValueAnimator::function(const string aAnimationType)
{
  mAnimationParam = 3; // current default for our ease function
  if (aAnimationType=="easein") {
    mAnimationFunction = &easeIn;
  }
  else if (aAnimationType=="easeout") {
    mAnimationFunction = &easeOut;
  }
  else if (aAnimationType=="easeinout") {
    mAnimationFunction = &easeInOut;
  }
  else {
    mAnimationFunction = &linear;
  }
  return this;
}


ValueAnimatorPtr ValueAnimator::param(double aAnimationParam)
{
  mAnimationParam = aAnimationParam;
  return this;
}



ValueAnimatorPtr ValueAnimator::startTime(MLMicroSeconds aStartTime)
{
  if (mStartedAt==Never) {
    mAbsoluteStartTime = true;
    mStartTimeOrDelay = aStartTime;
  }
  return this;
}


ValueAnimatorPtr ValueAnimator::startDelay(MLMicroSeconds aStartDelay)
{
  if (mStartedAt==Never) {
    mAbsoluteStartTime = false;
    mStartTimeOrDelay = aStartDelay;
  }
  return this;
}


ValueAnimatorPtr ValueAnimator::runAfter(ValueAnimatorPtr aPreceedingAnimation)
{
  // have preceeding animation trigger myself when it is done
  if (aPreceedingAnimation) {
    aPreceedingAnimation->mTriggerAnimations.push_back(this);
    mAwaitingTrigger = true;
  }
  return this;
}


MLMicroSeconds ValueAnimator::animate(double aTo, MLMicroSeconds aDuration, AnimationDoneCB aDoneCB)
{
  internalStop(true, false); // abort previous animation, if any
  mCurrentValue = mStartValue;
  mDuration = aDuration;
  mDoneCB = aDoneCB;
  if (!mValueSetter) {
    // cannot do anything
    internalStop(true, false);
    return Infinite; // no need to call step()
  }
  // precalculate operating params
  mDistance = aTo-mStartValue;
  if (!mAnimationFunction) mAnimationFunction = &linear; // default to linear
  mStepTime = mMinStepTime;
  mCycles = mRepeat;
  // calculate steps
  int steps = (int)(mDuration/mStepTime);
  if (mStepSize>0) {
    int sizedsteps = mDistance/mStepSize;
    if (sizedsteps<steps) {
      // given step size allows less frequent steps
      steps = sizedsteps;
      if (steps>0) mStepTime = mDuration/steps;
    }
  }
  if (steps==0) mStepTime = 0; // signals no steps for start()
  FOCUSLOG(
    "=== Set up Animation: current=%3.2f, distance=%3.2f, duration=%2.3f S, steps=%d, stepSize=%.3f, stepTime=%2.3f S",
    mCurrentValue, mDistance, (double)mDuration/Second, steps, mStepSize, (double)mStepTime/Second
  );
  // is startable now
  if (mAwaitingTrigger) return Infinite; // ..but needs to wait for trigger first
  // trigger right now
  return trigger();
}


MLMicroSeconds ValueAnimator::trigger()
{
  if (mStartTimeOrDelay) {
    if (!mAbsoluteStartTime) {
      // make absolute
      mStartTimeOrDelay = MainLoop::now()+mStartTimeOrDelay;
      mAbsoluteStartTime = true;
    }
    FOCUSLOG("=== Triggered Animation, but must await delay of %.2f seconds", (double)(mStartTimeOrDelay-MainLoop::now())/Second);
    if (mSelfTiming) {
      // schedule a timer to start
      mAnimationTimer.executeOnceAt(boost::bind(&ValueAnimator::start, this), mStartTimeOrDelay);
    }
    mAwaitingTrigger = false; // not waiting for trigger any more
    return mStartTimeOrDelay; // this is when we need to start
  }
  // can start right now
  return start();
}


MLMicroSeconds ValueAnimator::start()
{
  mAwaitingTrigger = false; // not waiting for trigger any more
  mStartTimeOrDelay = 0; // no delay any more, step() will run animation
  mAbsoluteStartTime = false;
  FOCUSLOG("=== Start Animation: from=%3.2f, distance=%3.2f, to=%3.2f, duration=%2.3f S, stepTime=%2.3f S", mStartValue, mDistance, mStartValue+mDistance, (double)mDuration/Second, (double)mStepTime/Second);
  mStartedAt = MainLoop::now();
  // start animation or just finish it if there are no steps (no step time)
  if (mStepTime>0) {
    // set current value
    mValueSetter(mCurrentValue);
    MLMicroSeconds nextStep = mStartedAt+mStepTime;
    if (mSelfTiming) {
      mAnimationTimer.executeOnceAt(boost::bind(&ValueAnimator::autoStep, this, _1, _2), nextStep);
    }
    return nextStep;
  }
  // immediately done (no steps)
  FOCUSLOG("=== No steps, no Animation: setting end value=%3.2f", mCurrentValue);
  return cycleComplete(mStartedAt);
}


MLMicroSeconds ValueAnimator::cycleComplete(MLMicroSeconds aNow)
{
  // set precise end value
  FOCUSLOG(">>> Animation Cycle completes after %3.3f S, with last value=%3.2f, final value=%3.2f, cycles=%d, autoreverse=%d", (double)(aNow-mStartedAt)/Second, mCurrentValue, mStartValue+mDistance, mCycles, mAutoreverse);
  mCurrentValue = mStartValue+mDistance;
  mValueSetter(mCurrentValue);
  // check cycles
  if (mCycles>0) mCycles--;
  if (mCycles>0 || mCycles<0) {
    // continues
    if (mAutoreverse) {
      mStartValue = mCurrentValue;
      mDistance = -mDistance;
    }
    else {
      // back to start
      mCurrentValue = mStartValue;
    }
    // continue stepping
    mStartedAt = aNow;
    return aNow+mStepTime;
  }
  internalStop(true, true);
  return Infinite; // no need to call step() (again)
}


MLMicroSeconds ValueAnimator::step(MLMicroSeconds aNow)
{
  if (mAwaitingTrigger) return Infinite; // still waiting for getting triggered
  if (aNow==Never) aNow = MainLoop::now();
  if (mStartTimeOrDelay) {
    // delayed start
    if (aNow<mStartTimeOrDelay) {
      // still waiting for start
      return mStartTimeOrDelay; // need to be called again at start time
    }
    // now actually start running
    return start();
  }
  if (!inProgress()) return Infinite;
  double progress = (double)(aNow-mStartedAt)/mDuration;
  if (progress>=1) {
    // reached end of cycle
    return cycleComplete(aNow);
  }
  else if (progress<0) {
    progress = 0; // should not happen
  }
  // cycle continues
  double fprog = mAnimationFunction(progress, mAnimationParam);
  FOCUSLOG(
    "--- Animation step: time since cycle start: %3.3f S, start=%3.2f, distance=%+3.2f: progress %.3f -> %.3f (delta = %.3f) -> newValue=%3.2f",
    (double)(aNow-mStartedAt)/Second, mStartValue, mDistance, progress, fprog, mStartValue+mDistance*fprog-mCurrentValue, mStartValue+mDistance*fprog
  );
  mCurrentValue = mStartValue+mDistance*fprog;
  mValueSetter(mCurrentValue);
  return aNow+mStepTime;
}


void ValueAnimator::autoStep(MLTimer &aTimer, MLMicroSeconds aNow)
{
  MLMicroSeconds nextStep = step();
  if (nextStep>0) {
    MainLoop::currentMainLoop().retriggerTimer(aTimer, 0, nextStep, MainLoop::absolute);
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
  return 2*sigmoid(aProgress/2+0.5, aTuning)-1; // second half 0.5..1
}


double ValueAnimator::easeInOut(double aProgress, double aTuning)
{
  return sigmoid(aProgress, aTuning);
}


#if ENABLE_ANIMATOR_SCRIPT_FUNCS

using namespace P44Script;

// .delay(startdelay)
FUNC_ARG_DEFS(delay, { numeric } );
static void delay_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->startDelay(f->arg(0)->doubleValue()*Second);
  f->finish(a); // return myself for chaining calls
}


// .runafter(animator)
FUNC_ARG_DEFS(runafter, { anyvalid } );
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
FUNC_ARG_DEFS(repeat, { numeric }, { numeric|optionalarg } );
static void repeat_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->repeat(f->arg(1)->boolValue(), f->arg(0)->doubleValue());
  f->finish(a); // return myself for chaining calls
}


// .function(animationfunctionname [, animationfunctionparam])
FUNC_ARG_DEFS(function, { text }, { numeric|optionalarg } );
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
FUNC_ARG_DEFS(from, { numeric } );
static void from_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->from(f->arg(0)->doubleValue());
  f->finish(a); // return myself for chaining calls
}


// .step(minsteptime [, stepsize])
FUNC_ARG_DEFS(step, { numeric }, { numeric|optionalarg }  );
static void step_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  // undefined arg returns 0 which means default for stepParams()
  a->animator()->stepParams(f->arg(0)->doubleValue()*Second, f->arg(1)->doubleValue());
  f->finish(a); // return myself for chaining calls
}


// .runto(endvalue, intime [, minsteptime [, stepsize]])    actually start animation
static void animation_complete(ValueAnimatorObjPtr aAnimationObj, double aReachedValue, bool aCompleted)
{
  aAnimationObj->sendEvent(aAnimationObj);
}
FUNC_ARG_DEFS(runto, { numeric }, { numeric }, { numeric|optionalarg } );
static void runto_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObjPtr a = boost::dynamic_pointer_cast<ValueAnimatorObj>(f->thisObj());
  assert(a);
  if (f->numArgs()>2) {
    a->animator()->stepParams(f->arg(2)->doubleValue()*Second, f->arg(3)->doubleValue());
  }
  a->animator()->animate(f->arg(0)->doubleValue(), f->arg(1)->doubleValue()*Second, boost::bind(&animation_complete, a, _1, _2));
  f->finish(a); // return animator so we can for example wait for it, or assign it to a variable
}


// .stop()
static void stop_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->stop();
  f->finish(); // no return value
}


// .reset()
static void reset_func(BuiltinFunctionContextPtr f)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(f->thisObj().get());
  assert(a);
  a->animator()->stop();
  a->animator()->reset();
  f->finish(); // no return value
}



static ScriptObjPtr current_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite, BuiltinMemberDescriptor*)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(aParentObj.get());
  return new NumericValue(a->animator()->current());
}


static ScriptObjPtr running_accessor(BuiltInMemberLookup& aMemberLookup, ScriptObjPtr aParentObj, ScriptObjPtr aObjToWrite, BuiltinMemberDescriptor*)
{
  ValueAnimatorObj* a = dynamic_cast<ValueAnimatorObj*>(aParentObj.get());
  MLMicroSeconds st = a->animator()->startedAt();
  if (st==Never) return new AnnotatedNullValue("animator not running");
  else return new NumericValue((double)(MainLoop::now()-st)/Second); // number of seconds already running
}


static const BuiltinMemberDescriptor animatorFunctions[] = {
  FUNC_DEF_W_ARG(delay, executable|anyvalid),
  FUNC_DEF_W_ARG(runafter, executable|null),
  FUNC_DEF_W_ARG(repeat, executable|anyvalid),
  FUNC_DEF_W_ARG(function, executable|anyvalid),
  FUNC_DEF_W_ARG(from, executable|anyvalid),
  FUNC_DEF_W_ARG(runto, executable|null),
  FUNC_DEF_W_ARG(step, executable|null),
  FUNC_DEF_NOARG(stop, executable|anyvalid),
  FUNC_DEF_NOARG(reset, executable|anyvalid),
  MEMBER_DEF(current, builtinvalue|numeric),
  MEMBER_DEF(running, builtinvalue|numeric),
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedAnimatorFunctionLookupP = NULL;

ValueAnimatorObj::ValueAnimatorObj(ValueAnimatorPtr aAnimator) :
  mAnimator(aAnimator)
{
  registerSharedLookup(sharedAnimatorFunctionLookupP, animatorFunctions);
}

TypeInfo ValueAnimatorObj::getTypeInfo() const
{
  return
    inherited::getTypeInfo()
    |oneshot|keeporiginal|freezable
    |(!mAnimator->inProgress() ? nowait : 0);
}


bool ValueAnimatorObj::isEventSource() const
{
  return true; // is always an event source
}

void ValueAnimatorObj::registerForFilteredEvents(EventSink* aEventSink, intptr_t aRegId)
{
  registerForEvents(aEventSink, aRegId); // no filtering
}



#endif // ENABLE_ANIMATOR_SCRIPT_FUNCS
