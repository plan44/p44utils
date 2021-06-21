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

#ifndef __p44utils__valueanimator__
#define __p44utils__valueanimator__

#include "p44utils_common.hpp"

#if ENABLE_P44SCRIPT && !defined(ENABLE_ANIMATOR_SCRIPT_FUNCS)
  #define ENABLE_ANIMATOR_SCRIPT_FUNCS 1
#endif

#if ENABLE_ANIMATOR_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif


using namespace std;

namespace p44 {

  #define ANIMATION_MIN_STEP_TIME (15*MilliSecond)

  class ValueAnimator;
  typedef boost::intrusive_ptr<ValueAnimator> ValueAnimatorPtr;

  /// callback provided by animation targets called by animator to change a value
  /// @param aNewValue value to set
  typedef boost::function<void (double aNewValue)> ValueSetterCB;

  /// callback for end of animation
  /// @param aReachedValue value reached with end of animation
  /// @param aCompleted set if animation successfully ran to end
  typedef boost::function<void (double aReachedValue, bool aCompleted)> AnimationDoneCB;

  /// Animation function type
  /// @param aProgress linear progress between 0..1
  /// @param aTuning function specific tuning value
  /// @return output value between 0..1 according to aProgress
  typedef double(*AnimationFunction)(double aProgress, double aTuning);

  class ValueAnimator : public P44Obj
  {
    typedef std::list<ValueAnimatorPtr> AnimatorList;

    ValueSetterCB mValueSetter;
    AnimationDoneCB mDoneCB;
    MLMicroSeconds mStartedAt; // if Never -> not running
    MLMicroSeconds mDefaultMinStepTime; ///< default minimum step time (usually set by creator of animator based on HW constraints)
    MLMicroSeconds mMinStepTime; ///< minimum step time for this animation as set via stepParams()
    MLMicroSeconds mStepTime; ///< actual step time used for this animation, might be larger when a minimal step size specified
    double mStepSize; ///< step size wanted for this animation, 0 if step size should be calculated from step time
    MLMicroSeconds mDuration;
    double mStartValue;
    double mCurrentValue;
    double mDistance;
    AnimationFunction mAnimationFunction;
    double mAnimationParam;
    bool mSelfTiming;
    MLTicket mAnimationTimer;
    bool mAutoreverse;
    int mRepeat; ///< the number of repetitions to run, -1 = forever
    int mCycles; ///< remaining cycles in current run
    AnimatorList mTriggerAnimations; ///< the animations to trigger when this one ends
    bool mAwaitingTrigger; ///< set when animation awaits a trigger from another animation
    bool mAbsoluteStartTime; ///< true when startTimeOrDelay is absolute, false when start is relative to the time the animation is triggered
    MLMicroSeconds mStartTimeOrDelay; ///< starting time (when relativeStartTime==false) or start delay (when relativeStartTime==true)

  public:

    /// Create an animator for a value
    /// @param aValueSetter the callback to use for changing the value
    /// @param aSelfTiming if set, the animator will time itself by scheduling timers in the mainloop
    ///    if not set, the animator expects to have its step() method called as indicated by step() and animate()'s return values
    ValueAnimator(ValueSetterCB aValueSetter, bool aSelfTiming = false, MLMicroSeconds aDefaultMinStepTime = 0);
    virtual ~ValueAnimator();

    /// Reset to default parameters
    void reset();

    /// Start animation
    /// @note start value and repeat parameters must be set before
    /// @param aTo ending value
    /// @param aDuration overall duration of of the animation
    /// @param aDoneCB called when the animation completes or is stopped with reporting enabled
    /// @note if animator was created with aSelfTiming==true, step() is called by an internal timer an MUST NOT be called directly!
    /// @return Infinite if there is no need to call step (animation has no steps or needs trigger first), otherwise mainloop time of when to call again
    MLMicroSeconds animate(double aTo, MLMicroSeconds aDuration, AnimationDoneCB aDoneCB = NULL);

    /// set stepping params
    /// @param aMinStepTime the minimum time between steps. If 0, ANIMATION_MIN_STEP_TIME is used
    /// @param aStepSize the desired step size. If 0, step size is determined by aMinStepTime (or its default)
    /// @note stepsize and steptime is only used when autostepping and for the recommended call-again time returned by step()
    ///   Actual stepping is done whenever step() is called, relative to the start time
    ValueAnimatorPtr stepParams(MLMicroSeconds aMinStepTime = 0, double aStepSize = 0);

    /// set repetition parameters
    /// @param aAutoReverse if set, animation direction is reversed after each cycle
    /// @param aRepeat number of repeating cycles (running forth and back with autoreverse counts as 2 cycles), <=0 for endless repeat
    /// @return the animator to allow chaining
    ValueAnimatorPtr repeat(bool aAutoReverse, int aRepeat);

    /// set animation function
    /// @param aAnimationFunction the animation function to use
    /// @return the animator to allow chaining
    ValueAnimatorPtr function(AnimationFunction aAnimationFunction);

    /// set animation function by name
    /// @param aAnimationType name of a standard animation type, selecting an internal function
    /// @return the animator to allow chaining
    ValueAnimatorPtr function(const string aAnimationType);

    /// set animation function parameter
    /// @param aAnimationParam a parameter to the animation function
    /// @return the animator to allow chaining
    ValueAnimatorPtr param(double aAnimationParam);

    /// set start value
    /// @param aFrom starting value
    /// @return the animator to allow chaining
    ValueAnimatorPtr from(double aFrom);

    /// set start time
    /// @param aStartTime mainloop time when animation should start
    /// @note if set, this invalidates runAfter and startDelay
    /// @return the animator to allow chaining
    ValueAnimatorPtr startTime(MLMicroSeconds aStartTime);

    /// set start delay
    /// @param aStartDelay start delay from the time when start is enabled (relevant when chained)
    /// @return the animator to allow chaining
    ValueAnimatorPtr startDelay(MLMicroSeconds aStartDelay);

    /// run only after another animator ends
    /// @param aPreceedingAnimation the preceeding animation which should trigger starting this animation (possibly with startDelay)
    /// @return the animator to allow chaining
    ValueAnimatorPtr runAfter(ValueAnimatorPtr aPreceedingAnimation);

    /// Stop ongoing animation
    /// @param aAndReport if set, the animation done callback, if any, is executed
    void stop(bool aAndReport = false);

    /// calculate and apply changes
    /// @note if animator was created with aSelfTiming==true, step() is called by an internal timer an MUST NOT be called directly!
    /// @note unless self-timing, this must be called again latest at the time demanded by return value.
    ///   If it is called more often, animation steps will be smaller, if it is called too late, animation might stutter but will
    ///   still keep the overall timing as good as possible.
    /// @note do not call step() too often, as it always causes the value setter to be executed, which might not be
    ///   efficent to do much more often than needed
    /// @return Infinite if there is no immediate need to call step again, otherwise mainloop time of when to call again (latest)
    MLMicroSeconds step();

    /// @return true when an animation is in progress, including waiting for start (after delay or at trigger)
    bool inProgress();

    /// @return true when the animator is valid, i.e. has a value setter
    bool valid();

    /// @return current value
    double current() { return mCurrentValue; }


    /// Animation functions
    static double linear(double aProgress, double aTuning);
    static double easeIn(double aProgress, double aTuning);
    static double easeOut(double aProgress, double aTuning);
    static double easeInOut(double aProgress, double aTuning);

  private:

    MLMicroSeconds trigger(); ///< internal: trigger to run (possibly with delay)
    MLMicroSeconds start(); ///< internal: start

    void autoStep(MLTimer &aTimer, MLMicroSeconds aNow);
    void internalStop(bool aReport, bool aCompleted);
    MLMicroSeconds cycleComplete(MLMicroSeconds aNow);

  };


  #if ENABLE_ANIMATOR_SCRIPT_FUNCS
  namespace P44Script {

    /// represents a view of a P44lrgraphics view hierarchy
    class ValueAnimatorObj : public P44Script::StructuredLookupObject, public P44Script::EventSource
    {
      typedef P44Script::StructuredLookupObject inherited;
      ValueAnimatorPtr mAnimator;
    public:
      ValueAnimatorObj(ValueAnimatorPtr aAnimator);
      virtual string getAnnotation() const P44_OVERRIDE { return "animator"; };
      ValueAnimatorPtr animator() { return mAnimator; }
      virtual EventSource *eventSource() const P44_OVERRIDE;
    };
    typedef boost::intrusive_ptr<ValueAnimatorObj> ValueAnimatorObjPtr;

  } // namespace P44Script
  #endif // ENABLE_ANIMATOR_SCRIPT_FUNCS


} // namespace p44


#endif /* defined(__p44utils__valueanimator__) */
