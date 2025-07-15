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

#ifndef __p44utils__jsonwebclient__
#define __p44utils__jsonwebclient__

#include "p44utils_main.hpp"

#include "jsonobject.hpp"
#include "httpcomm.hpp"

using namespace std;

namespace p44 {

  class JsonWebClient;

  typedef boost::intrusive_ptr<JsonWebClient> JsonWebClientPtr;

  /// callback for returning response data or reporting error
  /// @param aJsonResponse the response string
  /// @param aError an error object if an error occurred, empty pointer otherwise
  typedef boost::function<void (JsonObjectPtr aJsonResponse, ErrorPtr aError)> JsonWebClientCB;


  /// wrapper for non-blocking http client communication
  /// @note this class' implementation is not suitable for handling huge http requests and answers. It is
  ///   intended for accessing web APIs with short messages.
  class JsonWebClient : public HttpComm
  {
    typedef HttpComm inherited;

    JsonWebClientCB mJsonResponseCallback;

  public:

    JsonWebClient(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~JsonWebClient();

    /// send a JSON request via HTTP or HTTPS
    /// @param aURL the http or https URL to send JSON request to
    /// @param aResponseCallback will be called when request completes, returning response or error
    /// @param aMethod the HTTP method to use (defaults to "GET")
    /// @param aJsonRequest the JSON request to send (defaults to none)
    /// @param aContentType the content type/charset or NULL to use the default (application/json; charset=UTF-8)
    /// @param aSaveHeaders if true, mResponseHeaders will be set to a string,string map containing the headers
    /// @return false if no request could be initiated (already busy with another request).
    ///   If false, aHttpCallback will not be called
    bool jsonRequest(const char *aURL, JsonWebClientCB aResponseCallback, const char *aMethod = "GET", JsonObjectPtr aJsonRequest = JsonObjectPtr(), const char* aContentType = NULL, bool aSaveHeaders = false);

    /// send a request expected to return JSON (but not having a JSON request body) via HTTP or HTTPS
    /// @param aURL the http or https URL to send JSON request to
    /// @param aResponseCallback will be called when request completes, returning response or error
    /// @param aMethod the HTTP method to use (defaults to "POST")
    /// @param aPostData the raw POST data to send (for POST or PUT requests)
    /// @param aContentType the content type/charset or NULL to use the default (application/x-www-form-urlencoded; charset=UTF-8)
    /// @param aSaveHeaders if true, mResponseHeaders will be set to a string,string map containing the headers
    /// @return false if no request could be initiated (already busy with another request).
    ///   If false, aHttpCallback will not be called
    bool jsonReturningRequest(const char *aURL, JsonWebClientCB aResponseCallback, const char *aMethod = "POST", const string &aPostData = "", const char* aContentType = NULL, bool aSaveHeaders = false);

  protected:

    virtual const char *defaultContentType() { return CONTENT_TYPE_JSON; };

    virtual void requestThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode);

  };

} // namespace p44

#endif /* defined(__p44utils__jsonwebclient__) */
