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

// default geolocation
#if !defined(DEFAULT_LATITUDE) || !defined(DEFAULT_LONGITUDE)
  #define DEFAULT_LONGITUDE 8.474552
  #define DEFAULT_LATITUDE 47.394691
  #define DEFAULT_HEIGHTABOVESEA 396
#endif
#if !defined(DEFAULT_HEIGHTABOVESEA)
  #define DEFAULT_HEIGHTABOVESEA 0
#endif


namespace p44 {

  class GeoLocation
  {
  public:
    double latitude; ///< latitude in degrees north of equator
    double longitude; ///< longitude in degrees east of Greenwich
    double heightAboveSea; ///< height above sea level in meters
    GeoLocation() : longitude(DEFAULT_LONGITUDE), latitude(DEFAULT_LATITUDE), heightAboveSea(DEFAULT_HEIGHTABOVESEA) {};
    GeoLocation(double aLatitude, double aLongitude) : latitude(aLatitude), longitude(aLongitude), heightAboveSea(0) {};
    GeoLocation(double aLatitude, double aLongitude, double aHeightAboveSea) : latitude(aLatitude), longitude(aLongitude), heightAboveSea(aHeightAboveSea) {};
  };


  typedef struct {
    double sunrise; ///< sunrise time in hours
    double sunset; ///< sun set time in hours
    double twilight; ///< duration of twilight in hours (before sunrise, after sunset)
    double noon; ///< time of noon in hours
    double maxAltitude; ///< max altitude of the sun in degrees
  } SunParams;

  /// get sun parameters for a given day
  /// @param aTime unix time of the day
  /// @param aGeoLocation geolocation with latitude/longitude set
  /// @param aSunParams will get the sun parameters for the given day, place and timezone
  void getSunParams(time_t aTime, const GeoLocation &aGeoLocation, SunParams &aSunParams);

  /// get sunrise info
  /// @param aTime unix time of the day
  /// @param aGeoLocation geolocation with latitude/longitude set
  /// @param aTwilight if set, (approx) time of when morning twilight starts is returned
  /// @return sunrise or morning twilight time in hours
  double sunrise(time_t aTime, const GeoLocation &aGeoLocation, bool aTwilight);

  /// get sunset info
  /// @param aTime unix time of the day
  /// @param aGeoLocation geolocation with latitude/longitude set
  /// @param aTwilight if set, (approx) time of when evening twilight ends is returned
  /// @return sunrise or morning twilight time in hours
  double sunset(time_t aTime, const GeoLocation &aGeoLocation, bool aTwilight);


} // namespace p44

#endif // __p44utils__timeutils__
