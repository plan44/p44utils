//
//  Copyright (c) 2016-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "p44utils_common.hpp"
#include "httpcomm.hpp"

using namespace p44;

class HttpFixture {

public:

  string URL;
  string method;
  string requestBody;
  string contentType;
  bool streamResult;
  MLMicroSeconds timeout;

  HttpCommPtr http;

  ErrorPtr httpErr;
  string response;
  MLMicroSeconds tm;
  int chunks;

  HttpFixture()
  {
    http = HttpCommPtr(new HttpComm(MainLoop::currentMainLoop()));
  };


  void testRes(const string &aResponse, ErrorPtr aError)
  {
    chunks += 1;
    if (streamResult) {
      if (aResponse.size()!=0) {
        // not end of stream
        response += aResponse;
        return; // continue reading from stream
      }
    }
    else {
      response = aResponse;
    }
    tm = MainLoop::now()-tm; // calculate duration
    httpErr = aError;
    MainLoop::currentMainLoop().terminate(EXIT_SUCCESS);
  };


  void perform()
  {
    // start timing
    chunks = 0;
    tm = MainLoop::now();
    // start request
    http->setTimeout(timeout);
    if (!http->httpRequest(
      URL.c_str(),
      boost::bind(&HttpFixture::testRes, this, _1, _2),
      method.empty() ? NULL : method.c_str(),
      requestBody.empty() ? NULL : requestBody.c_str(),
      contentType.empty() ? NULL : contentType.c_str(),
      -1,
      true,
      streamResult
    )) {
      MainLoop::currentMainLoop().terminate(EXIT_FAILURE);
    }
  };


  int runHttp(
    string aURL,
    string aMethod = "GET",
    MLMicroSeconds aTimeout = 5*Second,
    string aRequestBody = "",
    string aContentType = "",
    bool aStreamResult = false
  ) {
    // save params
    URL = aURL;
    method = aMethod;
    timeout = aTimeout;
    requestBody = aRequestBody;
    contentType = aContentType;
    streamResult = aStreamResult;
    // schedule execution
    MainLoop::currentMainLoop().executeOnce(boost::bind(&HttpFixture::perform, this));
    // now let mainloop run (and terminate)
    return MainLoop::currentMainLoop().run(true);
  };


};


#define TEST_URL "plan44.ch/testing/httptest.php"
#define NOCERT_TEST_URL "localhost/"
#define WRONGCN_TEST_URL "plan442.nine.ch/testing/httptest.php"
#define ERR404_TEST_URL "plan44.ch/testing/BADhttptest.php"
#define ERR500_TEST_URL "plan44.ch/testing/httptest.php?err=500"
#define SLOWDATA_TEST_URL "plan44.ch/testing/httptest.php?delay=3"
#define STREAMDATA_TEST_URL "plan44.ch/testing/httptest.php?stream=1"
#define NOTRESPOND_TEST_URL "192.168.42.23"
#define AUTH_TEST_URL "plan44.ch/testing/authenticated/httptest.php"
#define AUTH_TEST_USER "testing"
#define AUTH_TEST_PW "testing"

TEST_CASE_METHOD(HttpFixture, "http GET test: request to known-good server", "[http]") {
  REQUIRE(runHttp("http://" TEST_URL)==EXIT_SUCCESS);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
}

TEST_CASE_METHOD(HttpFixture, "DNS test: known not existing URL", "[http]") {
  REQUIRE(runHttp("http://anurlthatxyzdoesnotexxxist.com", "GET", 2*Second)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::civetwebError));
  REQUIRE(tm < 2.1*Second);
}

TEST_CASE_METHOD(HttpFixture, "http timeout test: not responding IPv4", "[http]") {
  REQUIRE(runHttp("http://" NOTRESPOND_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::civetwebError));
  REQUIRE(tm < 2.1*Second);
}

TEST_CASE_METHOD(HttpFixture, "http auth: no credentials", "[http]") {
  REQUIRE(runHttp("http://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
}

TEST_CASE_METHOD(HttpFixture, "http auth: bad credentials", "[http]") {
  http->setHttpAuthCredentials("BAD" AUTH_TEST_USER, "BAD" AUTH_TEST_PW);
  REQUIRE(runHttp("http://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
}

TEST_CASE_METHOD(HttpFixture, "http auth: correct credentials", "[http]") {
  http->setHttpAuthCredentials(AUTH_TEST_USER, AUTH_TEST_PW);
  REQUIRE(runHttp("http://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
}

TEST_CASE_METHOD(HttpFixture, "test http Error 404", "[http]") {
  REQUIRE(runHttp("http://" ERR404_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 404));
}

TEST_CASE_METHOD(HttpFixture, "test http Error 500", "[http]") {
  REQUIRE(runHttp("http://" ERR500_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 500));
}

TEST_CASE_METHOD(HttpFixture, "http data timeout", "[http]") {
  REQUIRE(runHttp("http://" SLOWDATA_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
  //WARN(string_format("time = %.3f", (double)tm/Second));
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(tm == Approx(2*Second).epsilon(0.2));
  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::read));
}

TEST_CASE_METHOD(HttpFixture, "http slow data", "[http]") {
  REQUIRE(runHttp("http://" SLOWDATA_TEST_URL, "GET", 6*Second)==EXIT_SUCCESS);
  //WARN(string_format("time = %.3f", (double)tm/Second));
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
  REQUIRE(tm == Approx(3*Second).epsilon(0.2));
}

TEST_CASE_METHOD(HttpFixture, "https GET test: request to known-good server with valid cert", "[https]") {
  http->setServerCertVfyDir("*");
  REQUIRE(runHttp("https://" TEST_URL)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
}

TEST_CASE_METHOD(HttpFixture, "https GET test: request to working server with no verifyable cert", "[https]") {
  // default is platform cert checking, must error out even without using setServerCertVfyDir("*")!
  REQUIRE(runHttp("https://" NOCERT_TEST_URL)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(!Error::isOK(httpErr));
}

TEST_CASE_METHOD(HttpFixture, "https GET test without checking to local server w/o cert", "[https]") {
  http->setServerCertVfyDir(""); // no checking
  REQUIRE(runHttp("https://" NOCERT_TEST_URL)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
}

TEST_CASE_METHOD(HttpFixture, "https GET test: request to working server with valid cert but wrong CN", "[https]") {
  // default is platform cert checking, must error out even without using setServerCertVfyDir("*")!
  REQUIRE(runHttp("https://" WRONGCN_TEST_URL)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(!Error::isOK(httpErr));
}

TEST_CASE_METHOD(HttpFixture, "https timeout test: not responding IPv4", "[https]") {
  REQUIRE(runHttp("https://" NOTRESPOND_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::civetwebError));
  REQUIRE(tm < 2.1*Second);
}

TEST_CASE_METHOD(HttpFixture, "https auth: no credentials", "[https]") {
  REQUIRE(runHttp("https://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
}

TEST_CASE_METHOD(HttpFixture, "https auth: bad credentials", "[https]") {
  http->setHttpAuthCredentials("BAD" AUTH_TEST_USER, "BAD" AUTH_TEST_PW);
  REQUIRE(runHttp("https://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isError(httpErr, WebError::domain(), 401));
}

TEST_CASE_METHOD(HttpFixture, "https auth: correct credentials", "[https]") {
  http->setHttpAuthCredentials(AUTH_TEST_USER, AUTH_TEST_PW);
  REQUIRE(runHttp("https://" AUTH_TEST_URL, "GET")==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
}

TEST_CASE_METHOD(HttpFixture, "https data timeout", "[https]") {
  REQUIRE(runHttp("https://" SLOWDATA_TEST_URL, "GET", 2*Second)==EXIT_SUCCESS);
  //WARN(string_format("time = %.3f", (double)tm/Second));
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(tm > 2*Second); /* SSL handshake takes too much to know exactly how long it will take */
  REQUIRE(Error::isError(httpErr, HttpCommError::domain(), HttpCommError::read));
}

TEST_CASE_METHOD(HttpFixture, "https slow data", "[https]") {
  REQUIRE(runHttp("https://" SLOWDATA_TEST_URL, "GET", 6*Second)==EXIT_SUCCESS);
  //WARN(string_format("time = %.3f", (double)tm/Second));
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
  REQUIRE(tm > 3*Second); /* SSL handshake takes too much to know exactly how long it will take */
}

TEST_CASE_METHOD(HttpFixture, "http stream data", "[http],[FOCUS]") {
  REQUIRE(runHttp("http://" STREAMDATA_TEST_URL, "GET", Never, "", "", true)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
  REQUIRE(chunks == 5); /* should be 4 chunks, plus empty terminating response */
}

TEST_CASE_METHOD(HttpFixture, "https stream data", "[https],[FOCUS]") {
  REQUIRE(runHttp("https://" STREAMDATA_TEST_URL, "GET", Never, "", "", true)==EXIT_SUCCESS);
  INFO(URL);
  INFO(Error::text(httpErr));
  REQUIRE(Error::isOK(httpErr));
  REQUIRE(response.size()>0);
  REQUIRE(chunks == 5); /* should be 4 chunks, plus empty terminating response */
}




