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

#include "catch_amalgamated.hpp"

#include "utils.hpp"

TEST_CASE( "non null C String", "[utils]" ) {
  REQUIRE( string(p44::nonNullCStr(NULL)) == "" );
  REQUIRE( string(p44::nonNullCStr(" something ")) == " something " );
}

TEST_CASE( "Whitespace trimming", "[utils]" ) {
  REQUIRE( p44::trimWhiteSpace(" something ") == "something" );
  REQUIRE( p44::trimWhiteSpace(" \t\n something\r\t \t ") == "something" );
  REQUIRE( p44::trimWhiteSpace(" something ", true, false) == "something " );
  REQUIRE( p44::trimWhiteSpace(" something ", false, true) == " something" );
}

TEST_CASE( "lowercase", "[utils]" ) {
  REQUIRE( p44::lowerCase(string("UPPER And lower")) == "upper and lower" );
  REQUIRE( p44::lowerCase("UPPER And lower") == "upper and lower" );
  REQUIRE( p44::lowerCase("UPPER And lower and more", 15) == "upper and lower" );
}

TEST_CASE( "quoting", "[utils]" ) {
  REQUIRE( p44::shellQuote("some words") == "'some words'" );
  REQUIRE( p44::shellQuote("some special chars: \\ \" ' \n \t \r \x0c") == "'some special chars: \\ \" '\"'\"' \n \t \r \x0c'" );
  REQUIRE( p44::cstringQuote("some words") == "\"some words\"" );
  REQUIRE( p44::cstringQuote("some special chars: \\ \" ' \n \t \r \x0c") == "\"some special chars: \\\\ \\\" ' \\n \\t \\r \\x0c\"" );
}

TEST_CASE( "GTIN digit checking", "[utils]" ) {
  REQUIRE( p44::gtinCheckDigit(7640161170049) == 0 );
  REQUIRE( p44::gtinCheckDigit(7640161170040) == 9 );
  REQUIRE( p44::gtinCheckDigit(7640161170042)+7640161170042 == 7640161170049 );
}

TEST_CASE( "mac address formatting", "[utils]" ) {
  REQUIRE( p44::macAddressToString(0x1F2F3F4F5F6F) == "1F2F3F4F5F6F" );
  REQUIRE( p44::macAddressToString(0x1F2F3F4F5F6F,':') == "1F:2F:3F:4F:5F:6F" );
}

TEST_CASE( "mac address parsing", "[utils]" ) {
  REQUIRE( p44::stringToMacAddress("1F:2F:3F:4F:5F:6F") == 0x1F2F3F4F5F6F );
  REQUIRE( p44::stringToMacAddress("1F2F3F4F5F6F") == 0x1F2F3F4F5F6F );
  REQUIRE( p44::stringToMacAddress("1-2-3-4F-5F:6F", true) == 0x0102034F5F6F );
}

TEST_CASE( "IPv4 address formatting", "[utils]" ) {
  REQUIRE( p44::ipv4ToString(0xC0A83B2A) == "192.168.59.42" );
  REQUIRE( p44::ipv4ToString(0xFFFFFFFF) == "255.255.255.255" );
}

TEST_CASE( "IPv4 address parsing", "[utils]" ) {
  REQUIRE( p44::stringToIpv4("192.168.59.42") == 0xC0A83B2A );
  REQUIRE( p44::stringToIpv4("192.168.59") == 0 );
}

