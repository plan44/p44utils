//
//  Copyright (c) 2016 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

