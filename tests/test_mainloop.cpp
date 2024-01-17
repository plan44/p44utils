//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2018-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "p44utils_common.hpp"
#include "mainloop.hpp"

using namespace p44;

class MainloopFixture {

public:

  MainLoop &mainloop;

  MainloopFixture() :
    mainloop(MainLoop::currentMainLoop())
  {
  };

};


TEST_CASE_METHOD(MainloopFixture, "test now() is actually running", "[mainloop],[time]") {
  MLMicroSeconds mainlooptime = mainloop.now();
  CAPTURE(mainlooptime);
  sleep(1);
  REQUIRE(mainloop.now()-mainlooptime == Catch::Approx(1*Second).epsilon(0.1));
}

TEST_CASE_METHOD(MainloopFixture, "test unixtime() is actually running", "[mainloop],[time]") {
  MLMicroSeconds unixtime = mainloop.unixtime();
  CAPTURE(unixtime);
  sleep(1);
  REQUIRE(mainloop.unixtime()-unixtime == Catch::Approx(1*Second).epsilon(0.1));
}


TEST_CASE_METHOD(MainloopFixture, "test conversion from unixtime and back", "[mainloop],[time]") {
  MLMicroSeconds m = mainloop.unixTimeToMainLoopTime(0);
  REQUIRE(abs(mainloop.mainLoopTimeToUnixTime(m))<10);
}


TEST_CASE_METHOD(MainloopFixture, "test conversion to unixtime and back", "[mainloop],[time]") {
  MLMicroSeconds m = mainloop.mainLoopTimeToUnixTime(0);
  REQUIRE(abs(mainloop.unixTimeToMainLoopTime(m))<10);
}




