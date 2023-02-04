//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "timeutils.hpp"

#include <math.h>


using namespace p44;

// This code is derived from http://www.sci.fi/~benefon/rscalc.c
// written by Jarmo Lammi 1999-2001

#define DEGS (180.0/M_PI)
#define RADS (M_PI/180.0)
#define SUNDIA 0.53
#define AIRREFR (34.0/60.0) // athmospheric refraction degrees

/// Get the days to J2000
/// @param y year
/// @param m month
/// @param d day
/// @param h UT in decimal hours
/// @return days to year 2000
/// @note FNday only works between 1901 to 2099 - see Meeus chapter 7
static double FNday (int y, int m, int d, double h)
{
  long luku = - 7 * (y + (m + 9)/12)/4 + 275*m/9 + d;
  luku += (long)y*367;
  return (double)luku - 730531.5 + h/24.0;
};

/// @return an angle in the range 0 to 2*pi
static double FNrange (double x)
{
  double b = 0.5*x / M_PI;
  double a = 2.0*M_PI * (b - (long)(b));
  if (a < 0) a = 2.0*M_PI + a;
  return a;
};

/// Calculates the hour angle
/// @param lat latitude
/// @param declin declination
/// @return hour angle
static double f0(double lat, double declin)
{
  double fo,dfo;
  // Correction: different sign at S HS
  dfo = RADS*(0.5*SUNDIA + AIRREFR); if (lat < 0.0) dfo = -dfo;
  fo = tan(declin + dfo) * tan(lat*RADS);
  if (fo>0.99999) fo=1.0; // to avoid overflow //
  fo = asin(fo) + M_PI/2.0;
  return fo;
};

/// Calculates the hourangle for twilight times
/// @param lat latitude
/// @param declin declination
/// @return hour angle for twilight times
static double f1(double lat, double declin)
{
  double fi,df1;
  // Correction: different sign at S HS
  df1 = RADS * 6.0; if (lat < 0.0) df1 = -df1;
  fi = tan(declin + df1) * tan(lat*RADS);
  if (fi>0.99999) fi=1.0; // to avoid overflow //
  fi = asin(fi) + M_PI/2.0;
  return fi;
};


//  // Find the ecliptic longitude of the Sun
//  static double FNsun (double d)
//  {
//    //   mean longitude of the Sun
//    double L = FNrange(280.461 * RADS + .9856474 * RADS * d);
//    //   mean anomaly of the Sun
//    double g = FNrange(357.528 * RADS + .9856003 * RADS * d);
//    //   Ecliptic longitude of the Sun
//    return FNrange(L + 1.915 * RADS * sin(g) + .02 * RADS * sin(2 * g));
//  };


void p44::getSunParams(time_t aTime, const GeoLocation &aGeoLocation, SunParams &aSunParams)
{
  struct tm p;
  double L, g, daylen;
  double tz, y, m, day, h, d, lambda, obliq, alpha, delta, LL, equation, ha, hb;

  localtime_r(&aTime, &p);
  #ifndef ESP_PLATFORM
  tz = (double)p.tm_gmtoff/3600.0; // hours east of GMT
  #else
  // TODO: add TZ support for ESP32
  #warning "no TZ support in ESP32 at this time, assuming UTC"
  tz = 0;
  #endif
  y = p.tm_year;
  y += 1900;
  m = p.tm_mon + 1;
  day = p.tm_mday;
  h = 12; // noon
  d = FNday(y, m, day, h);
  // mean longitude of the Sun
  L = FNrange(280.461 * RADS + .9856474 * RADS * d);
  // mean anomaly of the Sun
  g = FNrange(357.528 * RADS + .9856003 * RADS * d);
  // find ecliptic longitude of the Sun
  lambda = FNrange(L + 1.915 * RADS * sin(g) + .02 * RADS * sin(2 * g));
  // Obliquity of the ecliptic
  obliq = 23.439 * RADS - .0000004 * RADS * d;
  //   Find the RA and DEC of the Sun
  alpha = atan2(cos(obliq) * sin(lambda), cos(lambda));
  delta = asin(sin(obliq) * sin(lambda));
  // Find the Equation of Time in minutes
  // (Correction suggested by David Smith)
  LL = L - alpha;
  if (L < M_PI) LL += 2.0*M_PI;
  equation = 1440.0 * (1.0 - LL / M_PI/2.0);
  ha = f0(aGeoLocation.latitude,delta);
  hb = f1(aGeoLocation.latitude,delta);
  aSunParams.twilight = hb - ha;  // length of twilight in radians
  aSunParams.twilight = 12.0*aSunParams.twilight/M_PI;    // length of twilight in hours
  // Conversion of angle to hours and minutes //
  daylen = DEGS*ha/7.5;
  if (daylen<0.0001) { daylen = 0.0; }
  // arctic winter
  aSunParams.sunrise = 12.0 - 12.0 * ha/M_PI + tz - aGeoLocation.longitude/15.0 + equation/60.0;
  aSunParams.sunset = 12.0 + 12.0 * ha/M_PI + tz - aGeoLocation.longitude/15.0 + equation/60.0;
  aSunParams.noon = aSunParams.sunrise + 12.0 * ha/M_PI;
  aSunParams.maxAltitude = 90.0 + delta * DEGS - aGeoLocation.latitude;
  // Correction for S HS suggested by David Smith
  // to express altitude as degrees from the N horizon
  if (aGeoLocation.latitude < delta * DEGS) aSunParams.maxAltitude = 180.0 - aSunParams.maxAltitude;
  if (aSunParams.sunrise > 24.0) aSunParams.sunrise-= 24.0;
  if (aSunParams.sunset > 24.0) aSunParams.sunset-= 24.0;
}


double p44::sunrise(time_t aTime, const GeoLocation &aGeoLocation, bool aTwilight)
{
  SunParams p;
  getSunParams(aTime, aGeoLocation, p);
  return p.sunrise - (aTwilight ? p.twilight : 0);
}


double p44::sunset(time_t aTime, const GeoLocation &aGeoLocation, bool aTwilight)
{
  SunParams p;
  getSunParams(aTime, aGeoLocation, p);
  return p.sunset + (aTwilight ? p.twilight : 0);
}
