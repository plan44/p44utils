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

#ifndef __p44utils__pwm__
#define __p44utils__pwm__

#include "p44utils_common.hpp"

#include "iopin.hpp"

using namespace std;

namespace p44 {


  /// Wrapper for PWM output accessed via
  /// generic Linux kernel SysFS support for PWMs
  class PWMPin : public AnalogIOPin
  {
    typedef AnalogIOPin inherited;

    uint32_t activeNs; ///< active time in nanoseconds
    uint32_t periodNs; ///< PWM period in nanoseconds
    bool inverted; ///< pwm inverted
    int pwmChip; ///< chip number
    int pwmChannel; ///< channel number
    int pwmFD; ///< file descriptor for the "value" file

  public:

    /// Create general purpose I/O pin
    /// @param aPwmChip PWM chip number (0,1,...)
    /// @param aPwmChannel number (0,1,...)
    /// @param aInitialValue initial duty cycle value (0..100)
    /// @param aPeriodInNs PWM period in nanoseconds
    PWMPin(int aPwmChip, int aPwmChannel, bool aInverted, double aInitialValue, uint32_t aPeriodInNs);
    virtual ~PWMPin();

    /// get value of pin
    /// @return current value (from actual pin for inputs, from last set state for outputs)
    virtual double getValue() P44_OVERRIDE;

    /// set value of pin (NOP for inputs)
    /// @param aValue new value to set output to
    virtual void setValue(double aValue) P44_OVERRIDE;

  };


	
} // namespace p44

#endif /* defined(__p44utils__pwm__) */
