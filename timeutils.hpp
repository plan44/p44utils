//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__timeutils__
#define __p44utils__timeutils__

#include "p44utils_common.hpp"

#include <time.h>

using namespace std;

namespace p44 {

  typedef struct {
    double sunrise; ///< sunrise time in hours
    double sunset; ///< sun set time in hours
    double twilight; ///< duration of twilight in hours (before sunrise, after sunset)
    double noon; ///< time of noon in hours
    double maxAltitude; ///< max altitude of the sun in degrees
  } SunParams;

  /// get sun parameters for a given day
  /// @param aTime unix time of the day
  /// @param aLat latitude
  /// @param aLong longitude
  /// @param aSunParams will get the sun parameters for the given day, place and timezone
  void getSunParams(time_t aTime, double aLat, double aLong, SunParams &aSunParams);

  /// get sunrise info
  /// @param aTime unix time of the day
  /// @param aLat latitude
  /// @param aLong longitude
  /// @param aTwilight if set, (approx) time of when morning twilight starts is returned
  /// @return sunrise or morning twilight time in hours
  double sunrise(time_t aTime, double aLat, double aLong, bool aTwilight);

  /// get sunset info
  /// @param aTime unix time of the day
  /// @param aLat latitude
  /// @param aLong longitude
  /// @param aTwilight if set, (approx) time of when evening twilight ends is returned
  /// @return sunrise or morning twilight time in hours
  double sunset(time_t aTime, double aLat, double aLong, bool aTwilight);


} // namespace p44

#endif // __p44utils__timeutils__
