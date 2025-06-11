//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__backlight__
#define __p44utils__backlight__

#include "p44utils_common.hpp"

#ifndef ENABLE_BACKLIGHT
  #define ENABLE_BACKLIGHT (ENABLE_LVGL) // assume backlight support makes sense when we have lvgl
#endif

#include "iopin.hpp"

using namespace std;

namespace p44 {


  /// Wrapper for Linux kernel SysFS support for screen backlights
  class BacklightControl : public AnalogIOPin
  {
    typedef AnalogIOPin inherited;

    int mBacklightFD;
    uint32_t mMaxBrightness;

  public:

    /// Create Backlight analog brightness control
    BacklightControl(const char* aBacklightName);
    virtual ~BacklightControl();

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

#endif /* defined(__p44utils__backlight__) */
