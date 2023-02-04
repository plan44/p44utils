//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2016-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "valueunits.hpp"

TEST_CASE( "Duration", "[valueunits]" ) {
  REQUIRE( p44::format_duration(0, 1, true) == "0\"" );
  REQUIRE( p44::format_duration(0, 1, false) == "0 second" );
  REQUIRE( p44::format_duration(10, 1, true) == "10\"" );
  REQUIRE( p44::format_duration(10, 1, false) == "10 second" );
  REQUIRE( p44::format_duration(90000, -1, true) == "90000\"" );
  REQUIRE( p44::format_duration(90000, 1, true) == "1d" );
  REQUIRE( p44::format_duration(90000, -2, true) == "1500'" );
  REQUIRE( p44::format_duration(90000, 2, true) == "1d 1h" );
  REQUIRE( p44::format_duration(93599, 2, true) == "1d 1h" );
  REQUIRE( p44::format_duration(90000, -3, true) == "25h" );
  REQUIRE( p44::format_duration(90000, 4, true) == "1d 1h" );
  REQUIRE( p44::format_duration(86400, 4, true) == "1d" );
  REQUIRE( p44::format_duration(86401, 4, true) == "1d 1\"" );
  REQUIRE( p44::format_duration(86460, 4, true) == "1d 1\'" );
  REQUIRE( p44::format_duration(90001, 4, true) == "1d 1h 1\"" );
  REQUIRE( p44::format_duration(98765, 4, true) == "1d 3h 26' 5\"" );
  REQUIRE( p44::format_duration(98765, 3, true) == "1d 3h 26'" );
  REQUIRE( p44::format_duration(98765, 2, true) == "1d 3h" );
  REQUIRE( p44::format_duration(98765, 1, true) == "1d" );
  REQUIRE( p44::format_duration(98765, 4, true) == "1d 3h 26' 5\"" ); // min/second with other units: space between
  REQUIRE( p44::format_duration(1565, 4, true) == "26'5\"" ); // min/second only: no space between
}


