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

#include "jsonwebclient.hpp"



using namespace p44;

JsonWebClient::JsonWebClient(MainLoop &aMainLoop) :
  HttpComm(aMainLoop)
{
}


JsonWebClient::~JsonWebClient()
{
}


void JsonWebClient::requestThreadSignal(ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode)
{
  if (mJsonResponseCallback) {
    // only if we have a json callback, we need to parse the response at all
    if (aSignalCode==threadSignalCompleted) {
      mRequestInProgress = false; // thread completed
      JsonObjectPtr message;
      if (Error::isOK(mRequestError) || mRequestError->isDomain(WebError::domain())) {
        // try to decode JSON
        struct json_tokener* tokener = json_tokener_new();
        struct json_object *o = json_tokener_parse_ex(tokener, mResponse.c_str(), (int)mResponse.size());
        if (o==NULL) {
          // error (or incomplete JSON, which is fine)
          JsonError::ErrorCodes err = json_tokener_get_error(tokener);
          if (err!=json_tokener_continue) {
            // real JSON error - however, if we already have a http level error, just annotate
            if (Error::notOK(mRequestError))
              mRequestError->prefixMessage("JSON cannot be decoded, probably due to: ");
            else
              mRequestError = ErrorPtr(new JsonError(err));
          }
        }
        else {
          // got JSON object
          message = JsonObject::newObj(o);
        }
        json_tokener_free(tokener);
      }
      // call back with result of request
      LOG(LOG_DEBUG, "JsonWebClient: <- received JSON response (Error=%s), answer:\n%s", Error::text(mRequestError), JsonObject::text(message));
      // Note: this callback might initiate another request already
      if (mJsonResponseCallback) {
        // use this callback, but as callback routine might post another request immediately, we need to free the member first
        JsonWebClientCB cb = mJsonResponseCallback;
        mJsonResponseCallback.clear();
        cb(message, mRequestError);
      }
      // release child thread object now
      mChildThread.reset();
    }
    else {
      // none of the signals we understand at this level (e.g. httpThreadSignalDataReady), let inherited handle that
      inherited::requestThreadSignal(aChildThread, aSignalCode);
    }
  }
  else {
    // no JSON callback, let inherited handle this
    inherited::requestThreadSignal(aChildThread, aSignalCode);
  }
}



bool JsonWebClient::jsonRequest(const char *aURL, JsonWebClientCB aResponseCallback, const char *aMethod, JsonObjectPtr aJsonRequest, const char* aContentType, bool aSaveHeaders)
{
  // set callback
  mJsonResponseCallback = aResponseCallback;
  // encode JSON, if any
  string jsonstring;
  if (aJsonRequest) {
    jsonstring = aJsonRequest->json_c_str();
  }
  LOG(LOG_DEBUG, "JsonWebClient: -> sending %s JSON request to %s:\n%s", aMethod, aURL, jsonstring.c_str());
  return httpRequest(aURL, NoOP, aMethod, jsonstring.c_str(), aContentType,  -1, aSaveHeaders);
}


bool JsonWebClient::jsonReturningRequest(const char *aURL, JsonWebClientCB aResponseCallback, const char *aMethod, const string &aPostData, const char* aContentType, bool aSaveHeaders)
{
  if (!aContentType) aContentType = CONTENT_TYPE_FORMDATA;
  // set callback
  mJsonResponseCallback = aResponseCallback;
  LOG(LOG_DEBUG, "JsonWebClient: -> sending %s raw data request to %s:\n%s", aMethod, aURL, aPostData.c_str());
  return httpRequest(aURL, NoOP, aMethod, aPostData.c_str(), aContentType,  -1, aSaveHeaders);
}



