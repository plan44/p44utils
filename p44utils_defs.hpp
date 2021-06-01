//
//  Copyright (c) 2014-2020 plan44.ch / Lukas Zeller, Zurich, Switzerland
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


  /// tristate type
  typedef enum {
    yes = 1,
    no = 0,
    undefined = -1
  } Tristate;


} // namespace p44

#endif /* __p44utils__p44utils_defs__ */
