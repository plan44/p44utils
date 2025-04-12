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

  MainLoop &mMainloop;

  MainloopFixture() :
    mMainloop(MainLoop::currentMainLoop())
  {
  };

};


class AsyncMainloopFixture {

public:

  MainLoop &mMainloop;
  ChildThreadWrapperPtr mChildThread;
  MLTicket mTimerTicket;
  int mMainThreadCounter;
  int mSubThreadCounter;
  const int cMaxCount = 5;
  ErrorPtr mTestStatus;

  AsyncMainloopFixture() :
    mMainloop(MainLoop::currentMainLoop())
  {
  };

  void routineProcessor(ChildThreadWrapper &aThread)
  {
    aThread.crossThreadCallProcessor();
  }

  void startRoutineProcessor()
  {
    mChildThread = mMainloop.executeInThread(boost::bind(&AsyncMainloopFixture::routineProcessor, this, _1), NoOP);
  }


  void startMainloopWith(SimpleCB aDoThis)
  {
    mMainloop.executeNow(boost::bind(&AsyncMainloopFixture::doSimple, this, aDoThis));
    mMainloop.run(true); // restart when already used!
  }

  void doSimple(SimpleCB aDoThis)
  {
    aDoThis();
  }

  // test case implementations

  // - blocking call to child process returning status

  void callChildBlocking()
  {
    mTestStatus.reset();
    startRoutineProcessor();
    LOG(LOG_NOTICE, "calling routine on child thread now");
    mTestStatus = mChildThread->executeOnChildThread(
      boost::bind(&AsyncMainloopFixture::returnOK, this, _1)
    );
    LOG(LOG_NOTICE, "returned from blocking call");
    LOG(LOG_NOTICE, "mainloop exits now");
    mMainloop.terminate(EXIT_SUCCESS);
  }

  ErrorPtr returnOK(ChildThreadWrapper &aThread)
  {
    LOG(LOG_NOTICE, "routine on child thread executes");
    return Error::ok();
  }


  // - non-blocking call to child process returning status

  void startNonBlockingCounter()
  {
    mTimerTicket.executeOnce(boost::bind(&AsyncMainloopFixture::nonblockingCounter, this, _1, _2));
    mMainThreadCounter = 0;
  }

  void nonblockingCounter(MLTimer &aTimer, MLMicroSeconds aNow)
  {
    mMainThreadCounter++;
    LOG(LOG_NOTICE, "mainloop incremented counter = %d", mMainThreadCounter);
    if (mMainThreadCounter<cMaxCount+2) {
      mMainloop.retriggerTimer(aTimer, 1*Second);
      return;
    }
    // end the test
    LOG(LOG_NOTICE, "mainloop exits now");
    mMainloop.terminate(EXIT_SUCCESS);
  }

  ErrorPtr blockingCounter(ChildThreadWrapper &aThread)
  {
    while(!aThread.shouldTerminate() && mSubThreadCounter<cMaxCount) {
      mSubThreadCounter++;
      LOG(LOG_NOTICE, "subthread incremented counter = %d", mSubThreadCounter);
      MainLoop::sleep(1*Second);
    }
    LOG(LOG_NOTICE, "blocking subthread will end now");
    return Error::ok();
  }

  void blockingDone(ErrorPtr aStatus)
  {
    LOG(LOG_NOTICE, "blocking subthread confirmed done");
    mTestStatus = aStatus; // should be explicit OK
  }


  void counterCompare()
  {
    mTestStatus.reset();
    startRoutineProcessor();
    startNonBlockingCounter();
    mChildThread->executeOnChildThreadAsync(
      boost::bind(&AsyncMainloopFixture::blockingCounter, this, _1),
      boost::bind(&AsyncMainloopFixture::blockingDone, this, _1)
    );
  }

};




TEST_CASE_METHOD(MainloopFixture, "test now() is actually running", "[mainloop],[time]") {
  MLMicroSeconds mainlooptime = mMainloop.now();
  CAPTURE(mainlooptime);
  sleep(1);
  REQUIRE(mMainloop.now()-mainlooptime == Catch::Approx(1*Second).epsilon(0.1));
}

TEST_CASE_METHOD(MainloopFixture, "test unixtime() is actually running", "[mainloop],[time]") {
  MLMicroSeconds unixtime = mMainloop.unixtime();
  CAPTURE(unixtime);
  sleep(1);
  REQUIRE(mMainloop.unixtime()-unixtime == Catch::Approx(1*Second).epsilon(0.1));
}


TEST_CASE_METHOD(MainloopFixture, "test conversion from unixtime and back", "[mainloop],[time]") {
  MLMicroSeconds m = mMainloop.unixTimeToMainLoopTime(0);
  REQUIRE(abs(mMainloop.mainLoopTimeToUnixTime(m))<10);
}


TEST_CASE_METHOD(MainloopFixture, "test conversion to unixtime and back", "[mainloop],[time]") {
  MLMicroSeconds m = mMainloop.mainLoopTimeToUnixTime(0);
  REQUIRE(abs(mMainloop.unixTimeToMainLoopTime(m))<10);
}


TEST_CASE_METHOD(AsyncMainloopFixture, "blocking subthread routine called blocking from main thread", "[mainloop],[threads]") {
  // calling a routine on the child thread, blocking and returning status
  startMainloopWith(boost::bind(&AsyncMainloopFixture::callChildBlocking, this));
  REQUIRE(Error::isError(mTestStatus, Error::domain(), Error::OK) == true);
}

TEST_CASE_METHOD(AsyncMainloopFixture, "blocking subthread routine called non-blocking from main thread", "[mainloop],[threads]") {
  // the counter relying on mainloop timers running must continue running while the subthread runs a blocking counter loop
  // - main thread counter counts 2 steps further until ending the test, to give subthread the chance to signal routine end
  startMainloopWith(boost::bind(&AsyncMainloopFixture::counterCompare, this));
  REQUIRE(mMainThreadCounter >= cMaxCount);
  REQUIRE(mSubThreadCounter == cMaxCount);
  REQUIRE(Error::isError(mTestStatus, Error::domain(), Error::OK) == true);
}




