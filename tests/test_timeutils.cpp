//
//  Copyright (c) 2016-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "catch.hpp"

#include "timeutils.hpp"
#include <stdlib.h>

// plan44.ch LOC
#define LONG 8.474552
#define LAT 47.394691
#define TZ "CET-1CEST-2,M3.5.0/2,M10.5.0/3"
#define WINTEROFFS 1
#define SUMMEROFFS 2
#define PRECISION (3.0/60)
#define TWILIGHTFACTOR 0.75


TEST_CASE( "sun rise and set", "[timeutils]" ) {
  setenv("TZ", TZ, 1);
  tzset();
  p44::SunParams p;
  struct tm tim;

  SECTION("spring in Zürich") {
    tim.tm_year = 2019-1900;
    tim.tm_mon = 3-1; // march, winter time
    tim.tm_mday = 21;
    tim.tm_hour = 23;
    tim.tm_min = 42;
    tim.tm_sec = 0;
    time_t t = mktime(&tim);
    localtime_r(&t, &tim); // convert back to get tz offset
    REQUIRE( tim.tm_gmtoff==WINTEROFFS*3600 );
    p44::getSunParams(t, p44::GeoLocation(LAT, LONG), p);
    REQUIRE( p.sunrise == Approx(6+27.0/60).margin(PRECISION) );
    REQUIRE( p.sunset == Approx(18+39.0/60).margin(PRECISION) );
    REQUIRE( p.noon == Approx(12+33.0/60).margin(PRECISION) );
    REQUIRE( p.twilight/TWILIGHTFACTOR == Approx(0+30.0/60).margin(0.2) ); // not very precise, 12min
  }

  SECTION("summer in zürich") {
    tim.tm_year = 2019-1900;
    tim.tm_mon = 6-1; // june, summer time
    tim.tm_mday = 11;
    tim.tm_hour = 11;
    tim.tm_min = 11;
    tim.tm_sec = 11;
    time_t t = mktime(&tim);
    localtime_r(&t, &tim); // convert back to get tz offset
    REQUIRE( tim.tm_gmtoff==SUMMEROFFS*3600 );
    p44::getSunParams(t, p44::GeoLocation(LAT, LONG), p);
    REQUIRE( p.sunrise == Approx(5+29.0/60).margin(PRECISION) );
    REQUIRE( p.sunset == Approx(21+21.0/60).margin(PRECISION) );
    REQUIRE( p.noon == Approx(13+25.0/60).margin(PRECISION) );
    REQUIRE( p.twilight/TWILIGHTFACTOR == Approx(0+40.0/60).margin(0.2) ); // not very precise, 12min
  }


  SECTION("santaclaus in zürich") {
    tim.tm_year = 2019-1900;
    tim.tm_mon = 12-1; // june, summer time
    tim.tm_mday = 6;
    tim.tm_hour = 6;
    tim.tm_min = 30;
    tim.tm_sec = 0;
    time_t t = mktime(&tim);
    localtime_r(&t, &tim); // convert back to get tz offset
    REQUIRE( tim.tm_gmtoff==WINTEROFFS*3600 );
    p44::getSunParams(t, p44::GeoLocation(LAT, LONG), p);
    REQUIRE( p.sunrise == Approx(7+57.0/60).margin(PRECISION) );
    REQUIRE( p.sunset == Approx(16+35.0/60).margin(PRECISION) );
    REQUIRE( p.noon == Approx(12+16.0/60).margin(PRECISION) );
    REQUIRE( p.twilight/TWILIGHTFACTOR == Approx(0+36.0/60).margin(0.2) ); // not very precise, 12min
  }



}

