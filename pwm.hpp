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

#ifndef __p44utils__pwm__
#define __p44utils__pwm__

#include "p44utils_common.hpp"

#include "iopin.hpp"

#ifdef ESP_PLATFORM
  #include "driver/ledc.h"
  #include "driver/gpio.h"
#endif

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
    #ifdef ESP_PLATFORM
    gpio_num_t gpioNo; ///< the GPIO to use as PWM output
    ledc_channel_t ledcChannel; ///< ledc channel number
    #else
    int pwmChip; ///< chip number
    int pwmChannel; ///< channel number
    int pwmFD; ///< file descriptor for the "value" file
    #endif

  public:

    /// Create PWM output pin
    /// @param aPwmChip PWM chip number (0,1,...), for ESP32: GPIO number
    /// @param aPwmChannel number (0,1,...), for ESP32: ledc channel number
    /// @param aInitialValue initial duty cycle value (0..100)
    /// @param aPeriodInNs PWM period in nanoseconds, 0 = default
    PWMPin(int aPwmChip, int aPwmChannel, bool aInverted, double aInitialValue, uint32_t aPeriodInNs);
    virtual ~PWMPin();

    /// get value of pin
    /// @return current value (from actual pin for inputs, from last set state for outputs)
    virtual double getValue() P44_OVERRIDE;

    /// set value of pin (NOP for inputs)
    /// @param aValue new value to set output to
    virtual void setValue(double aValue) P44_OVERRIDE;

    /// get range and resolution of this pin
    /// @param aMin minimum value
    /// @param aMax maximum value
    /// @param aResolution resolution (LSBit value)
    /// @return false if no range information is available (arguments are not touched then)
    virtual bool getRange(double &aMin, double &aMax, double &aResolution) P44_OVERRIDE;

  };


	
} // namespace p44

#endif /* defined(__p44utils__pwm__) */
