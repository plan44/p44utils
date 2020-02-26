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
  /// @param aAbaCompletedorted set if animation successfully ran to end
  typedef boost::function<void (double aReachedValue, bool aCompleted)> AnimationDoneCB;

  /// Animation function type
  /// @param aProgress linear progress between 0..1
  /// @param aTuning function specific tuning value
  /// @return output value between 0..1 according to aProgress
  typedef double(*AnimationFunction)(double aProgress, double aTuning);

  class ValueAnimator : public P44Obj
  {
    ValueSetterCB valueSetter;
    AnimationDoneCB doneCB;
    MLMicroSeconds startTime; // if Never -> not running
    MLMicroSeconds stepTime;
    MLMicroSeconds duration;
    double startValue;
    double currentValue;
    double distance;
    AnimationFunction animationFunction;
    double animationParam;
    bool selfTiming;
    MLTicket animationTimer;
    bool autoreverse;
    int cycles;

  public:

    /// Create an animator for a value
    /// @param aValueSetter the callback to use for changing the value
    /// @param aSelfTiming if set, the animator will time itself by scheduling timers in the mainloop
    ///    if not set, the animator expects to have it's step() method called as indicated by step() and animate()'s return values
    ValueAnimator(ValueSetterCB aValueSetter, bool aSelfTiming = false);
    virtual ~ValueAnimator();

    /// Start animation
    /// @note start value and repeat parameters must be set before
    /// @param aTo ending value
    /// @param aDuration overall duration of of the animation
    /// @param aDoneCB called when the animation completes or is stopped with reporting enabled
    /// @param aMinStepTime the minimum time between steps. If 0, ANIMATION_MIN_STEP_TIME is used
    /// @param aStepSize the desired step size. If 0, step size is determined by aMinStepTime (or its default)
    /// @note stepsize and steptime is only used when autostepping and for the recommended call-again time returned by step()
    ///   Actual stepping is done whenever step() is called, relative to the start time
    /// @return Infinite if there is no need to call step (animation has no steps), otherwise mainloop time of when to call again
    MLMicroSeconds animate(double aTo, MLMicroSeconds aDuration, AnimationDoneCB aDoneCB = NULL, MLMicroSeconds aMinStepTime = 0, double aStepSize = 0);

    /// set repetition parameters
    /// @param aAutoReverse if set, animation direction is reversed after each cycle
    /// @param aCycles number of cycles (running forth and back with autoreverse counts as 2 cycles), 0 for endless repeat
    /// @return the animator to allow chaining
    ValueAnimatorPtr repeat(bool aAutoReverse, int aCycles);

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
    ValueAnimatorPtr from(double aFrom);

    /// Stop ongoing animation
    /// @param aAndReport if set, the animation done callback, if any, is executed
    void stop(bool aAndReport = false);

    /// calculate and apply changes
    /// @note this must be called as demanded by return value
    /// @return Infinite if there is no immediate need to call step again, otherwise mainloop time of when to call again
    MLMicroSeconds step();

    /// @return true when an animation is in progress
    bool inProgress();

    /// @return true when the animator is valid, i.e. has a value setter
    bool valid();


    /// Animation functions
    static double linear(double aProgress, double aTuning);
    static double easeIn(double aProgress, double aTuning);
    static double easeOut(double aProgress, double aTuning);
    static double easeInOut(double aProgress, double aTuning);

  private:

    void autoStep(MLTimer &aTimer, MLMicroSeconds aNow);
    void internalStop(bool aReport, bool aCompleted);
    MLMicroSeconds cycleComplete(MLMicroSeconds aNow);

  };

} // namespace p44


#endif /* defined(__p44utils__valueanimator__) */
