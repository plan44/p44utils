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

#include "httpcomm.hpp"

#if ENABLE_APPLICATION_SUPPORT
  #include "application.hpp" // we need it for paths
#endif

using namespace p44;

#if ENABLE_NAMED_ERRORS
const char* HttpCommError::errorName() const
{
  switch(getErrorCode()) {
    case invalidParameters: return "invalidParameters";
    case noConnection: return "noConnection";
    case read: return "read";
    case write: return "write";
    case civetwebError: return "civetwebError";
  }
  return NULL;
}
#endif // ENABLE_NAMED_ERRORS

typedef enum {
  httpThreadSignalDataReady = threadSignalUserSignal
} HttpThreadSignals;



HttpComm::HttpComm(MainLoop &aMainLoop) :
  mMainLoop(aMainLoop),
  mRequestInProgress(false),
  mMgConn(NULL),
  mHttpAuthInfo(NULL),
  mAuthMode(digest_only),
  mTimeout(Never),
  mBufferSz(2048),
  mServerCertVfyDir("*"), // default to platform's generic certificate checking method / root cert store
  mResponseDataFd(-1),
  mStreamResult(false),
  mDataProcessingPending(false)
{
}


HttpComm::~HttpComm()
{
  if (mHttpAuthInfo) free(mHttpAuthInfo); // we own this
  terminate();
}


void HttpComm::terminate()
{
  mResponseCallback.clear(); // prevent calling back now
  cancelRequest(); // make sure subthread is cancelled
}



void HttpComm::requestThread(ChildThreadWrapper &aThread)
{
  string protocol, hostSpec, host, doc;
  uint16_t port;

  mRequestError.reset();
  mResponse.clear();
  splitURL(mRequestURL.c_str(), &protocol, &hostSpec, &doc, NULL, NULL);
  bool useSSL = false;
  if (protocol=="http") {
    port = 80;
    useSSL = false;
  }
  else if (protocol=="https") {
    port = 443;
    useSSL = true;
  }
  else {
    mRequestError = Error::err<HttpCommError>(HttpCommError::invalidParameters, "invalid protocol");
  }
  splitHost(hostSpec.c_str(), &host, &port);
  if (Error::isOK(mRequestError)) {
    // now issue request
    const size_t ebufSz = 100;
    char ebuf[ebufSz];
    string extraHeaders;
    for (HttpHeaderMap::iterator pos=mRequestHeaders.begin(); pos!=mRequestHeaders.end(); ++pos) {
      extraHeaders += string_format("%s: %s\r\n", pos->first.c_str(), pos->second.c_str());
    }
    #if !USE_LIBMONGOOSE
    struct mg_client_options copts;
    copts.host = host.c_str();
    copts.host_name = host.c_str(); // important for servers that need SNI for establishing SSL connection
    copts.port = port;
    copts.client_cert = mClientCertFile.empty() ? NULL : mClientCertFile.c_str();
    copts.server_cert = mServerCertVfyDir.empty() ? NULL : mServerCertVfyDir.c_str();
    copts.timeout = mTimeout==Never ? -2 : (double)mTimeout/Second;
    if (mRequestBody.length()>0) {
      // is a request which sends data in the HTTP message body (e.g. POST)
      mMgConn = mg_download_secure(
        &copts,
        useSSL,
        mMethod.c_str(),
        doc.c_str(),
        mUsername.empty() ? NULL : mUsername.c_str(),
        mPassword.empty() ? NULL : mPassword.c_str(),
        &mHttpAuthInfo,
        (int)mAuthMode,
        ebuf, ebufSz,
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "%s"
        "\r\n"
        "%s",
        mContentType.c_str(),
        (long)mRequestBody.length(),
        extraHeaders.c_str(),
        mRequestBody.c_str()
      );
    }
    else {
      // no request body (e.g. GET, DELETE)
      mMgConn = mg_download_secure(
        &copts,
        useSSL,
        mMethod.c_str(),
        doc.c_str(),
        mUsername.empty() ? NULL : mUsername.c_str(),
        mPassword.empty() ? NULL : mPassword.c_str(),
        &mHttpAuthInfo,
        (int)mAuthMode,
        ebuf, ebufSz,
        "Content-Length: 0\r\n"
        "%s"
        "\r\n",
        extraHeaders.c_str()
      );
    }
    #else
    int tmo = timeout==Never ? -1 : (int)(timeout/MilliSecond);
    if (requestBody.length()>0) {
      // is a request which sends data in the HTTP message body (e.g. POST)
      mgConn = mg_download_ex(
        host.c_str(),
        port,
        useSSL,
        tmo,
        method.c_str(),
        doc.c_str(),
        username.empty() ? NULL : username.c_str(),
        password.empty() ? NULL : password.c_str(),
        &httpAuthInfo,
        ebuf, ebufSz,
        "Content-Type: %s\r\n"
        "Content-Length: %ld\r\n"
        "%s"
        "\r\n"
        "%s",
        contentType.c_str(),
        requestBody.length(),
        extraHeaders.c_str(),
        requestBody.c_str()
      );
    }
    else {
      // no request body (e.g. GET, DELETE)
      mgConn = mg_download_ex(
        host.c_str(),
        port,
        useSSL,
        tmo,
        method.c_str(),
        doc.c_str(),
        username.empty() ? NULL : username.c_str(),
        password.empty() ? NULL : password.c_str(),
        &httpAuthInfo,
        ebuf, ebufSz,
        "%s"
        "\r\n",
        extraHeaders.c_str()
      );
    }
    #endif
    if (!mMgConn) {
      mRequestError = Error::err_cstr<HttpCommError>(HttpCommError::civetwebError, ebuf);
    }
    else {
      // successfully initiated connection
      #if !USE_LIBMONGOOSE
      const struct mg_response_info *responseInfo = mg_get_response_info(mMgConn);
      mResponseStatus = responseInfo->status_code;
      #else
      struct mg_request_info *responseInfo = mg_get_request_info(mgConn);
      responseStatus = 0;
      sscanf(responseInfo->uri, "%d", &responseStatus);  // status code string is in uri
      #endif
      // check for auth
      if (mResponseStatus==401) {
        LOG(LOG_DEBUG, "401 - http auth?")
      }
      // accept 200..203 status codes as OK (these are: success, created, accepted, non-authorative(=proxy modified))
      if (mResponseStatus<200 || mResponseStatus>203) {
        // Important: report status as WebError, not HttpCommError, because it is not technically an error on the HTTP transport level
        mRequestError = WebError::webErr(mResponseStatus,"HTTP non-ok status");
      }
      // - get headers if requested
      if (mResponseHeaders) {
        if (responseInfo) {
          for (int i=0; i<responseInfo->num_headers; i++) {
            (*mResponseHeaders)[responseInfo->http_headers[i].name] = responseInfo->http_headers[i].value;
          }
        }
      }
      if (Error::isOK(mRequestError) || mRequestError->isDomain(WebError::domain())) {
        // - read data
        uint8_t *bufferP = new uint8_t[mBufferSz];
        int errCause;
        #if !USE_LIBMONGOOSE
        double to = mStreamResult ? TMO_SOMETHING : copts.timeout;
        #endif
        while (true) {
          #if !USE_LIBMONGOOSE
          ssize_t res = mg_read_ex(mMgConn, bufferP, mBufferSz, to, &errCause);
          if (mStreamResult && res<0 && errCause==EC_TIMEOUT) {
            continue;
          }
          else if (res==0 || (res<0 && errCause==EC_CLOSED)) {
            // connection has closed, all bytes read
            if (mStreamResult) {
              // when streaming, signal end-of-stream condition by an empty data response
              mResponse.clear();
            }
            break;
          }
          else if (res<0) {
            // read error
            mRequestError = Error::err<HttpCommError>(HttpCommError::read, "HTTP read error: %s", errCause==EC_TIMEOUT ? "timeout" : strerror(errno));
            break;
          }
          #else
          ssize_t res = mg_read_ex(mgConn, bufferP, bufferSz, (int)streamResult);
          if (res==0) {
            // connection has closed, all bytes read
            if (streamResult) {
              // when streaming, signal of stream condition by an empty data response
              response.clear();
            }
            break;
          }
          else if (res<0) {
            // read error
            requestError = Error::err<HttpCommError>(HttpCommError::read, "HTTP read error: %s", strerror(errno));
            break;
          }
          #endif
          else {
            // data read
            if (mResponseDataFd>=0) {
              // write to fd
              write(mResponseDataFd, bufferP, res);
            }
            else if (mStreamResult) {
              // pass back the data chunk now
              mResponse.assign((const char *)bufferP, (size_t)res);
              mDataProcessingPending = true;
              aThread.signalParentThread(httpThreadSignalDataReady);
              // now wait until data has been processed in main thread
              while (mDataProcessingPending) {
                // FIXME: ugly - add better thread signalling here
                usleep(50000); // 50mS
              }
            }
            else {
              // just collect entire response in string
              mResponse.append((const char *)bufferP, (size_t)res);
            }
          }
        }
        delete[] bufferP;
      } // if content to read
      // done, close connection
      if (mMgConn) {
        mg_close_connection(mMgConn);
        mMgConn = NULL;
      }
    }
  }
  // ending the thread function will call the requestThreadSignal on the main thread
}



void HttpComm::requestThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)
{
  DBGLOG(LOG_DEBUG, "HttpComm: Received signal from child thread: %d", aSignalCode);
  if (aSignalCode==threadSignalCompleted) {
    DBGLOG(LOG_DEBUG, "- HTTP subthread exited - request completed");
    mRequestInProgress = false; // thread completed
    // call back with result of request
    // Note: as this callback might initiate another request already and overwrite the callback, copy it here
    HttpCommCB cb = mResponseCallback;
    string resp = mResponse;
    ErrorPtr reqErr = mRequestError;
    // release child thread object now
    mResponseCallback.clear();
    mChildThread.reset();
    // now execute callback
    if (cb) cb(resp, reqErr);
  }
  else if (aSignalCode==httpThreadSignalDataReady) {
    // data chunk ready in streamResult mode
    DBGLOG(LOG_DEBUG, "- HTTP subthread delivers chunk of data - request going on");
    //DBGLOG(LOG_DEBUG, "- data: %s", response.c_str());
    // callback may NOT issue another request on this httpComm, so no need to copy it
    if (mResponseCallback) mResponseCallback(mResponse, mRequestError);
    mDataProcessingPending = false; // child thread can go on reading
  }
  else if (aSignalCode==threadSignalCancelled) {
    // Note: mgConn is owned by child thread and should NOT be accessed from other threads, normally.
    //   This is an exception, because here the thread has been aborted, and if mgConn is still open,
    //   it has to be closed to avoid leaking memory or file descriptors.
    if (mMgConn) {
      mg_close_connection(mMgConn);
      mMgConn = NULL;
    }
  }
}



bool HttpComm::httpRequest(
  const char *aURL,
  HttpCommCB aResponseCallback,
  const char *aMethod,
  const char* aRequestBody,
  const char* aContentType,
  int aResponseDataFd,
  bool aSaveHeaders,
  bool aStreamResult
)
{
  if (mRequestInProgress || !aURL)
    return false; // blocked or no URL
  mResponseDataFd = aResponseDataFd;
  mResponseHeaders.reset();
  mRequestError.reset();
  if (aSaveHeaders)
    mResponseHeaders = HttpHeaderMapPtr(new HttpHeaderMap);
  mRequestURL = aURL;
  mResponseCallback = aResponseCallback;
  mMethod = aMethod;
  mRequestBody = nonNullCStr(aRequestBody);
  if (aContentType)
    mContentType = aContentType; // use specified content type
  else
    mContentType = defaultContentType(); // use default for the class
  mStreamResult = aStreamResult;
  // now let subthread handle this
  mRequestInProgress = true;
  mChildThread = MainLoop::currentMainLoop().executeInThread(
    boost::bind(&HttpComm::requestThread, this, _1),
    boost::bind(&HttpComm::requestThreadSignal, this, _1, _2)
  );
  return true; // could be initiated (even if immediately ended due to error, but callback was called)
}


void HttpComm::cancelRequest()
{
  if (mRequestInProgress && mChildThread) {
    mChildThread->cancel();
    mRequestInProgress = false; // prevent cancelling multiple times
  }
}

// MARK: - Utilities


//  8.2.1. The form-urlencoded Media Type
//
//     The default encoding for all forms is `application/x-www-form-
//     urlencoded'. A form data set is represented in this media type as
//     follows:
//
//          1. The form field names and values are escaped: space
//          characters are replaced by `+', and then reserved characters
//          are escaped as per [URL]; that is, non-alphanumeric
//          characters are replaced by `%HH', a percent sign and two
//          hexadecimal digits representing the ASCII code of the
//          character. Line breaks, as in multi-line text field values,
//          are represented as CR LF pairs, i.e. `%0D%0A'.
//
//          2. The fields are listed in the order they appear in the
//          document with the name separated from the value by `=' and
//          the pairs separated from each other by `&'. Fields with null
//          values may be omitted. In particular, unselected radio
//          buttons and checkboxes should not appear in the encoded
//          data, but hidden fields with VALUE attributes present
//          should.
//
//              NOTE - The URI from a query form submission can be
//              used in a normal anchor style hyperlink.
//              Unfortunately, the use of the `&' character to
//              separate form fields interacts with its use in SGML
//              attribute values as an entity reference delimiter.
//              For example, the URI `http://host/?x=1&y=2' must be
//              written `<a href="http://host/?x=1&#38;y=2"' or `<a
//              href="http://host/?x=1&amp;y=2">'.
//
//              HTTP server implementors, and in particular, CGI
//              implementors are encouraged to support the use of
//              `;' in place of `&' to save users the trouble of
//              escaping `&' characters this way.

string HttpComm::urlEncode(const string &aString, bool aFormURLEncoded)
{
  string result;
  const char *p = aString.c_str();
  while (char c = *p++) {
    if (aFormURLEncoded && c==' ')
      result += '+'; // replace spaces by pluses
    else if (!isalnum(c))
      string_format_append(result, "%%%02X", (unsigned int)c & 0xFF);
    else
      result += c; // just append
  }
  return result;
}


void HttpComm::appendFormValue(string &aDataString, const string &aFieldname, const string &aValue)
{
  if (aDataString.size()>0) aDataString += '&';
  aDataString += urlEncode(aFieldname, true);
  aDataString += '=';
  aDataString += urlEncode(aValue, true);
}



// MARK: - script support

#if ENABLE_HTTP_SCRIPT_FUNCS && ENABLE_P44SCRIPT

using namespace P44Script;

static void httpFuncDone(BuiltinFunctionContextPtr f, HttpCommPtr aHttpAction, bool aWithMeta, const string &aResponse, ErrorPtr aError)
{
  POLOG(f, LOG_INFO, "http action returns '%s', error = %s", aResponse.c_str(), Error::text(aError));
  if (aWithMeta) {
    if (Error::isOK(aError) || Error::isDomain(aError, WebError::domain())) {
      JsonObjectPtr resp = JsonObject::newObj();
      resp->add("status", JsonObject::newInt32(aHttpAction->mResponseStatus));
      resp->add("data", JsonObject::newString(aResponse));
      if (aHttpAction->mResponseHeaders) {
        JsonObjectPtr hdrs = JsonObject::newObj();
        for (HttpHeaderMap::iterator pos = aHttpAction->mResponseHeaders->begin(); pos!=aHttpAction->mResponseHeaders->end(); ++pos) {
          hdrs->add(pos->first.c_str(), JsonObject::newString(pos->second));
        }
        resp->add("headers", hdrs);
      }
      f->finish(ScriptObj::valueFromJSON(resp));
      return;
    }
  }
  else {
    if (Error::isOK(aError)) {
      f->finish(new StringValue(aResponse));
      return;
    }
  }
  // report as script error
  f->finish(new ErrorValue(aError));
}

static void httpFuncImpl(BuiltinFunctionContextPtr f, string aMethod)
{
  string url;
  string data;
  JsonObjectPtr params; // for httprequest full-feature function
  JsonObjectPtr o;
  MLMicroSeconds timeout = Never;
  string contentType;
  HttpComm::AuthMode authMode = HttpComm::digest_only;
  bool withmeta = false;
  bool formdata = false;
  JsonObjectPtr jdata;
  if (aMethod.empty()) {
    // httprequest({
    //   "url":"http...",
    //   "method":"POST",
    //   "data":...,
    //   "formdata": bool,
    //   "timeout":20
    //   "user":xxx,
    //   "password":xxx,
    //   "basicauth":xxx
    //   "clientcert":xxx,
    //   "servercert":xxx,
    //   "headers": {
    //      "Content-Type":"...", // custom content type
    //      "header1":"value1",
    //      ...
    //   },
    //   "withmeta": bool
    // } [, data])
    params = f->arg(0)->jsonValue();
    // url must be present
    if (!params->get("url", o, true)) {
      f->finish(new ErrorValue(TextError::err("request object must contain 'url' field")));
      return;
    }
    url = o->stringValue();
    // method defaults to GET, but can be set
    aMethod = "GET"; // default to GET
    if (params->get("method", o)) aMethod = o->stringValue();
    // data can be in the request object or (to allow binary strings), as second parameter
    if (f->numArgs()>=2) {
      if (f->arg(1)->hasType(structured)) jdata = f->arg(1)->jsonValue();
      else data = f->arg(1)->stringValue(); // could be a binary string
    }
    else if (params->get("data", o)) {
      if (o->isType(json_type_object) || o->isType(json_type_array)) jdata = o;
      else data = o->stringValue();
    }
    // timeout is optional
    if (params->get("timeout", o)) timeout = o->doubleValue()*Second;
    // flag to convert JSON data objects to key=value application/x-www-form-urlencoded style
    if (params->get("formdata", o)) formdata = o->boolValue();
  }
  else {
    // xxxurl("<url>"[,timeout][,"<data>"])
    url = f->arg(0)->stringValue();
    int ai = 1;
    if (f->numArgs()>ai) {
      // could be timeout if it is numeric
      if (f->arg(ai)->hasType(numeric)) {
        timeout = f->arg(ai)->doubleValue()*Second;
        ai++;
      }
    }
    if (aMethod!="GET") {
      if (f->numArgs()>ai) {
        if (f->arg(ai)->hasType(structured)) {
          jdata = f->arg(ai)->jsonValue();
        }
        else {
          data = f->arg(ai)->stringValue();
          contentType = CONTENT_TYPE_FORMDATA; // default for non-JSON PUT and POST
        }
      }
    }
  }
  // possibly convert JSON data
  if (jdata) {
    if (jdata->isType(json_type_object) && formdata) {
      // convert JSON objects to formdata
      jdata->resetKeyIteration();
      string field;
      while(jdata->nextKeyValue(field, o)) {
        HttpComm::appendFormValue(data, field, o->stringValue());
      }
      contentType = CONTENT_TYPE_FORMDATA;
    }
    else {
      // just stringified JSON
      data = jdata->stringValue();
      contentType = CONTENT_TYPE_JSON;
    }
  }
  // extract from url
  string user;
  string password;
  string protocol;
  HttpCommPtr httpAction = HttpCommPtr(new HttpComm(MainLoop::currentMainLoop()));
  // force https w/o cert checking when URL begins with a "!"
  if (*url.c_str()=='!') {
    url.erase(0,1); // remove exclamation mark
    httpAction->setServerCertVfyDir(""); // no checking
  }
  // auth might be in URL
  splitURL(url.c_str(), &protocol, NULL, NULL, &user, &password);
  if (protocol=="https") {
    authMode = HttpComm::basic_on_request; // with https, basic auth is reasonably safe, so by default allow it if server requests it
  }
  if (params) {
    // auth can also be in request object
    if (params->get("user", o)) user = o->stringValue();
    if (params->get("password", o)) password = o->stringValue();
    if (params->get("basicauth", o)) {
      string as = o->stringValue();
      if (as=="immediate") authMode = HttpComm::basic_first;
      else if (as=="onrequest") authMode = HttpComm::basic_on_request;
      else authMode = HttpComm::digest_only;
    }
    // explicit client or server certs
    if (params->get("clientcert", o)) {
      httpAction->setClientCertFile(Application::sharedApplication()->dataPath(o->stringValue(), P44SCRIPT_DATA_SUBDIR "/", false));
    }
    if (params->get("servercert", o)) {
      string p = o->stringValue();
      if (p.empty()) httpAction->setServerCertVfyDir(p);
      else httpAction->setServerCertVfyDir(Application::sharedApplication()->dataPath(p, P44SCRIPT_DATA_SUBDIR "/", false));
    }
    // request object might contain extra headers
    if (params->get("headers", o)) {
      o->resetKeyIteration();
      string hn;
      JsonObjectPtr hv;
      while (o->nextKeyValue(hn, hv)) {
        if (hn=="Content-Type") contentType = hv->stringValue(); // special case, content type
        else httpAction->addRequestHeader(hn, hv->stringValue()); // other headers
      }
    }
    // we might want to get all metadata (headers, status, etc.) as an object
    if (params->get("withmeta", o)) withmeta = o->boolValue();
  }
  httpAction->setHttpAuthCredentials(user, password, authMode);
  if (timeout!=Never) httpAction->setTimeout(timeout);
  POLOG(f, LOG_INFO, "issuing %s to %s %s", aMethod.c_str(), url.c_str(), data.c_str());
  f->setAbortCallback(boost::bind(&HttpComm::cancelRequest, httpAction));
  if (!httpAction->httpRequest(
    url.c_str(),
    boost::bind(&httpFuncDone, f, httpAction, withmeta, _1, _2),
    aMethod.c_str(),
    data.c_str(),
    contentType.empty() ? NULL : contentType.c_str(),
    -1,
    withmeta // save headers when we need all meta data
  )) {
    f->finish(new ErrorValue(TextError::err("could not issue http request")));
  }
}

// geturl("<url>"[,timeout])
FUNC_ARG_DEFS(geturl, { text }, { numeric|optionalarg } );
static void geturl_func(BuiltinFunctionContextPtr f)
{
  httpFuncImpl(f, "GET");
}

FUNC_ARG_DEFS(postputurl, { text } , { anyvalid|optionalarg }, { anyvalid|optionalarg } );

// posturl("<url>"[,timeout][,"<data>"])
static void posturl_func(BuiltinFunctionContextPtr f)
{
  httpFuncImpl(f, "POST");
}

// puturl("<url>"[,timeout][,"<data>"])
static void puturl_func(BuiltinFunctionContextPtr f)
{
  httpFuncImpl(f, "PUT");
}


// httprequest(requestparams [,"<data>"])
FUNC_ARG_DEFS(httprequest, { objectvalue }, { anyvalid|optionalarg} );
static void httprequest_func(BuiltinFunctionContextPtr f)
{
  httpFuncImpl(f, "");
}


// urlencode(texttoencode [, x-www-form-urlencoded])
FUNC_ARG_DEFS(urlencode, { text } , { anyvalid|optionalarg }, { anyvalid|optionalarg } );
static void urlencode_func(BuiltinFunctionContextPtr f)
{
  f->finish(new StringValue(HttpComm::urlEncode(f->arg(0)->stringValue(), f->arg(1)->boolValue())));
}


static const BuiltinMemberDescriptor httpGlobals[] = {
  FUNC_DEF_W_ARG(geturl, executable|async|text),
  FUNC_DEF_C_ARG(posturl, executable|async|text, postputurl),
  FUNC_DEF_C_ARG(puturl, executable|async|text, postputurl),
  FUNC_DEF_W_ARG(httprequest, executable|async|text),
  FUNC_DEF_W_ARG(urlencode, executable|text),
  BUILTINS_TERMINATOR
};

HttpLookup::HttpLookup() :
  inherited(httpGlobals)
{
}

#endif // ENABLE_HTTP_SCRIPT_FUNCS && ENABLE_P44SCRIPT
