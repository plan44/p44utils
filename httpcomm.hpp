//
//  Copyright (c) 2013-2017 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__httpcomm__
#define __p44utils__httpcomm__

#include "p44utils_common.hpp"

#include "civetweb.h"

using namespace std;

namespace p44 {


  // Errors

  class HttpCommError : public Error
  {
  public:
    enum {
      invalidParameters = 10000,
      noConnection = 10001,
      read = 10002,
      write = 10003,
      mongooseError = 20000
    };
    typedef uint16_t ErrorCodes;

    static const char *domain() { return "HttpComm"; }
    virtual const char *getErrorDomain() const { return HttpCommError::domain(); };
    HttpCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
  };



  class HttpComm;

  typedef boost::intrusive_ptr<HttpComm> HttpCommPtr;

  typedef std::map<string,string> HttpHeaderMap;
  typedef boost::shared_ptr<HttpHeaderMap> HttpHeaderMapPtr;


  /// callback for returning response data or reporting error
  /// @param aResponse the response string
  /// @param aError an error object if an error occurred, empty pointer otherwise
  typedef boost::function<void (const string &aResponse, ErrorPtr aError)> HttpCommCB;


  /// wrapper for non-blocking http client communication
  /// @note this class' implementation is not suitable for handling huge http requests and answers. It is
  ///   intended for accessing web APIs with short messages.
  class HttpComm : public P44Obj
  {
    typedef P44Obj inherited;

    HttpCommCB responseCallback;

    // vars used in subthread, only access when !requestInProgress
    string requestURL;
    string method;
    string contentType;
    string requestBody;
    string username;
    string password;
    int responseDataFd;
    bool streamResult; ///< if set, result will be "streamed", meaning callback will be called multiple times as data chunks arrive
    MLMicroSeconds timeout; // timeout, Never = use default, do not set
    struct mg_connection *mgConn; // mongoose connection
    void *httpAuthInfo; // opaque auth info kept stored between connections

  public:

    HttpHeaderMapPtr responseHeaders; ///< the response headers when httpRequest is called with aSaveHeaders

  protected:

    MainLoop &mainLoop;

    bool requestInProgress; ///< set when request is in progress and no new request can be issued

    // vars used in subthread, only access when !requestInProgress
    ChildThreadWrapperPtr childThread;
    string response;
    ErrorPtr requestError;
    HttpHeaderMap requestHeaders; ///< extra request headers to be included with the request(s)
    bool dataProcessingPending; ///< set until main thread has returned from callback in streamResult mode

  public:

    HttpComm(MainLoop &aMainLoop);
    virtual ~HttpComm();

    /// clear request headers
    void clearRequestHeaders() { requestHeaders.clear(); };

    /// add a request header (will be used on all subsequent requests
    /// @param aHeaderName the name of the header
    /// @param aHeaderValue the value of the header
    void addRequestHeader(const string aHeaderName, const string aHeaderValue) { requestHeaders[aHeaderName] = aHeaderValue; };

    /// set http (digest) auth credentials (will be used on all subsequent requests)
    /// @param aUsername user name
    /// @param aPassword password
    void setHttpAuthCredentials(const string aUsername, const string aPassword) { username = aUsername; password = aPassword; };

    /// send a HTTP or HTTPS request
    /// @param aURL the http or https URL to access
    /// @param aResponseCallback will be called when request completes, returning response or error
    /// @param aMethod the HTTP method to use (defaults to "GET")
    /// @param aRequestBody a C string containing the request body to send, or NULL if none
    /// @param aContentType the content type for the body to send (including a charset spec, possibly), or NULL to use default
    /// @param aResponseDataFd if>=0, response data will be written to that file descriptor
    /// @param aSaveHeaders if true, responseHeaders will be set to a string,string map containing the headers
    /// @param aStreamResult if true, response body will be delivered in chunks as they become available.
    ///   An empty result will be delivered when stream ends.
    /// @return false if no request could be initiated (already busy with another request).
    ///   If false, aHttpCallback will not be called
    bool httpRequest(
      const char *aURL,
      HttpCommCB aResponseCallback,
      const char *aMethod = "GET",
      const char* aRequestBody = NULL,
      const char *aContentType = NULL,
      int aResponseDataFd = -1,
      bool aSaveHeaders = false,
      bool aStreamResult = false
    );

    /// cancel request, request callbacks will be executed
    void cancelRequest();

    /// terminate operation, no callbacks
    virtual void terminate();

    /// explicitly set socket timeout to use
    void setTimeout(MLMicroSeconds aTimeout) { timeout = aTimeout; };


    // Utilities
    static string urlEncode(const string &aString, bool aFormURLEncoded);
    static void appendFormValue(string &aDataString, const string &aFieldname, const string &aValue);


  protected:
    virtual const char *defaultContentType() { return "text/html; charset=UTF-8"; };

    virtual void requestThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode);

  private:
    void requestThread(ChildThreadWrapper &aThread);

  };

  
} // namespace p44


#endif /* defined(__p44utils__httpcomm__) */
