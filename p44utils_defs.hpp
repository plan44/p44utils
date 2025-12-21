//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2014-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


#ifndef __p44utils__p44utils_defs__
#define __p44utils__p44utils_defs__

#include "p44utils_minimal.hpp"

/// Definitions: types, constants with no dependency on any other header file

using namespace std;

namespace p44 {

  /// @name Mainloop timing unit
  /// @{
  typedef long long MLMicroSeconds; ///< Mainloop time in microseconds
  const MLMicroSeconds Never = 0;
  const MLMicroSeconds Infinite = -1;
  const MLMicroSeconds MicroSecond = 1;
  const MLMicroSeconds MilliSecond = 1000;
  const MLMicroSeconds Second = 1000*MilliSecond;
  const MLMicroSeconds Minute = 60*Second;
  const MLMicroSeconds Hour = 60*Minute;
  const MLMicroSeconds Day = 24*Hour;
  /// @}

  #define DEFINED_TIME(t) (t>0) // any time at or before 0 is not a defined time (but Never or Infinite)
  #define DEFINED_INTERVAL(dt) (dt>=0) // Interval 0 is defined, negative is not an interval
  #define NONZERO_INTERVAL(dt) (dt>0) // zero interval sometimes means "Never"

  /// Special Constant to use for empty boost::function arguments
  /// @note use this, not NULL, because NULL is nullptr in more recent C++ environments,
  ///    which is incompatible with boost::function constructors. A plain 0 is compatible.
  #define NoOP 0

  /// tristate type
  typedef enum {
    yes = 1,
    no = 0,
    undefined = -1
  } Tristate;


} // namespace p44

#endif /* __p44utils__p44utils_defs__ */
