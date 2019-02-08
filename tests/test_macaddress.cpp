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

#include "macaddress.hpp"

TEST_CASE( "platform MAC address and IPv4 detection", "[macaddress]" ) {

  uint32_t myipv4 = p44::ipv4Address();
  uint64_t mymac = p44::macAddress();

  SECTION("my own IP address must not be zero") {
    REQUIRE( myipv4 != 0 );
  }

  SECTION("my own MAC address must not be zero") {
    REQUIRE( mymac  != 0 );
  }

  SECTION("MAC arp lookup for my own IP address should return my own MAC address") {
    uint64_t mymacArp;
    p44::getMacAddressByIpv4(myipv4, mymacArp);
    REQUIRE( mymacArp==mymac );
  }

}

TEST_CASE( "specific interface's MAC address", "[macaddress]" ) {

  SECTION("asking for a invalid interface's MAC address must return zero") {
    uint64_t mymac = p44::macAddress("xyz");
    REQUIRE( mymac == 0 );
  }

  SECTION("asking for a invalid interface's IPv4 address must return zero") {
    uint32_t myipv4 = p44::ipv4Address("xyz");
    REQUIRE( myipv4 == 0 );
  }

  #ifdef __APPLE__
  SECTION("asking for 'en0' interface (macOS, WiFi must be on) should return a MAC address + IP") {
    uint64_t mymac = p44::macAddress("en0");
    uint32_t myipv4 = p44::ipv4Address("en0");
    REQUIRE( mymac != 0 );
    REQUIRE( myipv4 != 0 );
  }
  #else
  SECTION("asking for 'eth0' interface (Linux) should return a MAC address + IP") {
    uint64_t mymac = p44::macAddress("eth0");
    uint32_t myipv4 = p44::ipv4Address("eth0");
    REQUIRE( mymac != 0 );
    REQUIRE( myipv4 != 0 );
  }
  #endif


}

