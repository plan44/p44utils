//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "p44utils_main.hpp"

#if USE_LIBMONGOOSE
  #include "mongoose.h"
#else
  #include "civetweb.h"
#endif


#if ENABLE_P44SCRIPT && !defined(ENABLE_HTTP_SCRIPT_FUNCS)
  #define ENABLE_HTTP_SCRIPT_FUNCS 1
#endif

#if ENABLE_HTTP_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif

#define CONTENT_TYPE_HTML "text/html; charset=UTF-8"
#define CONTENT_TYPE_JSON "application/json; charset=UTF-8"
#define CONTENT_TYPE_FORMDATA "application/x-www-form-urlencoded; charset=UTF-8"

using namespace std;

namespace p44 {


  // Errors

  class HttpCommError : public Error
  {
  public:
    enum {
      invalidParameters = 10000,
      noConnection = 10001,
      read = 10002, // includes timeout
      write = 10003, // includes timeout
      civetwebError = 20000
    };
    typedef uint16_t ErrorCodes;

    static const char *domain() { return "HttpComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return HttpCommError::domain(); };
    HttpCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE;
    #endif // ENABLE_NAMED_ERRORS
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

  public:
    typedef enum {
      digest_only = 0, // only digest auth is allowed
      basic_on_request = 1, // basic is used when server requests it
      basic_first = 2, // basic auth is attempted in first try without server asking for it
    } AuthMode; ///< http auth mode

    HttpHeaderMapPtr mResponseHeaders; ///< the response headers when httpRequest is called with aSaveHeaders
    int mResponseStatus; ///< set to the status code of the response (in all cases, success or not, 0 if none)

  private:

    HttpCommCB mResponseCallback;

    // vars used in subthread, only access when !requestInProgress
    string mRequestURL;
    string mMethod;
    string mContentType;
    string mRequestBody;
    string mUsername;
    string mPassword;
    AuthMode mAuthMode;
    string mClientCertFile;
    string mServerCertVfyDir;
    int mResponseDataFd;
    size_t mBufferSz; ///< buffer size for civetweb/mongoose data read operations
    bool mStreamResult; ///< if set, result will be "streamed", meaning callback will be called multiple times as data chunks arrive
    MLMicroSeconds mTimeout; ///< timeout, Never = use default, do not set
    struct mg_connection *mMgConn; ///< mongoose connection
    void *mHttpAuthInfo; ///< opaque auth info kept stored between connections

  protected:

    MainLoop &mMainLoop;

    bool mRequestInProgress; ///< set when request is in progress and no new request can be issued

    // vars used in subthread, only access when !requestInProgress
    ChildThreadWrapperPtr mChildThread;
    string mResponse;
    ErrorPtr mRequestError;
    HttpHeaderMap mRequestHeaders; ///< extra request headers to be included with the request(s)
    bool mDataProcessingPending; ///< set until main thread has returned from callback in streamResult mode

  public:

    HttpComm(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~HttpComm();

    /// clear request headers
    void clearRequestHeaders() { mRequestHeaders.clear(); };

    /// add a request header (will be used on all subsequent requests
    /// @param aHeaderName the name of the header
    /// @param aHeaderValue the value of the header
    void addRequestHeader(const string aHeaderName, const string aHeaderValue) { mRequestHeaders[aHeaderName] = aHeaderValue; };

    /// set http (digest) auth credentials (will be used on all subsequent requests)
    /// @param aUsername user name (empty means no http auth user)
    /// @param aPassword password (empty means no http auth pw)
    /// @param aAuthMode defaults to digest_only.
    void setHttpAuthCredentials(const string aUsername, const string aPassword, AuthMode aAuthMode = digest_only) { mUsername = aUsername; mPassword = aPassword; mAuthMode = aAuthMode; };

    /// explicitly set socket timeout to use
    /// @param aTimeout set to timeout value or Never for no timeout at all
    void setTimeout(MLMicroSeconds aTimeout) { mTimeout = aTimeout; };

    /// explicitly set a receiving data buffer size
    /// @param aBufferSize size of buffer for receiving data. Default is ok for API calls -
    ///   only set a large buffer when you need more performance for receiving a lot of data.
    void setBufferSize(size_t aBufferSize) { mBufferSz = aBufferSize; };

    /// explicitly set a client certificate path
    /// @param aClientCertFile set file path to a client certificate to use with the connection.
    ///   Use empty string to use no certificate.
    void setClientCertFile(const string aClientCertFile) { mClientCertFile = aClientCertFile; };

    /// explicitly set a client certificate path
    /// @param aServerCertVfyDir set file path to the root certificate file or directory for checking
    ///   the server's certificate.
    ///   - Use empty string to not verify server certificate at all
    ///   - Use "*" to use platform's default certificate checking (usually CA files in /etc/ssl/cert)
    ///   - Specify a path (such as "/etc/ssl/cert") to specify a certs directory
    ///     (which must contain OpenSSL style certs and hash links created with c_rehash utility)
    ///   - prefix a file name with "=" to specify a CAFile (multiple certs in one file)
    void setServerCertVfyDir(const string aServerCertVfyDir) { mServerCertVfyDir = aServerCertVfyDir; };

    
    /// send a HTTP or HTTPS request
    /// @param aURL the http or https URL to access
    /// @param aResponseCallback will be called when request completes, returning response or error
    /// @param aMethod the HTTP method to use (defaults to "GET")
    /// @param aRequestBody a C string containing the request body to send, or NULL if none
    /// @param aContentType the content type for the body to send (including a charset spec, possibly), or NULL to use default
    /// @param aResponseDataFd if>=0, response data will be written to that file descriptor
    /// @param aSaveHeaders if true, mResponseHeaders will be set to a string,string map containing the headers
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


    // Utilities
    static string urlEncode(const string &aString, bool aFormURLEncoded);
    static void appendFormValue(string &aDataString, const string &aFieldname, const string &aValue);

  protected:
    virtual const char *defaultContentType() { return CONTENT_TYPE_HTML; };

    virtual void requestThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode);

  private:
    void requestThread(ChildThreadWrapper &aThread);

  };

  #if ENABLE_HTTP_SCRIPT_FUNCS && ENABLE_P44SCRIPT
  namespace P44Script {

    /// represents the global objects related to http
    class HttpLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      HttpLookup();
    };

  }
  #endif


  
} // namespace p44


#endif /* defined(__p44utils__httpcomm__) */
