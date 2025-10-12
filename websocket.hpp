//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2021-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__websocket__
#define __p44utils__websocket__

#include "p44utils_main.hpp"

#if ENABLE_UWSC

extern "C" {
  #include <uwsc/uwsc.h>
}

#if ENABLE_P44SCRIPT && !defined(ENABLE_WEBSOCKET_SCRIPT_FUNCS)
  #define ENABLE_WEBSOCKET_SCRIPT_FUNCS 1
#endif

#if ENABLE_WEBSOCKET_SCRIPT_FUNCS
  #include "p44script.hpp"
#endif




using namespace std;

namespace p44 {

  class WebSocketClient;

  class WebSocketError : public Error
  {
  public:
    // Errors
    typedef int ErrorCodes; // using UWSC_ERROR_xxx
    static const int numErrorCodes = UWSC_ERROR_SSL_HANDSHAKE+1;

    static const char *domain() { return "websocket"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return WebSocketError::domain(); };
    WebSocketError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    // must match 
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "IOError",
      "InvalidHeader",
      "ServerMasked",
      "NotSupported",
      "PingTimeout",
      "Connect",
      "SSLHandshake",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  typedef boost::intrusive_ptr<WebSocketClient> WebSocketClientPtr;


  /// generic callback for delivering a received websocket message
  typedef boost::function<void (const string aMessage, ErrorPtr aError)> WebSocketMessageCB;


  class WebSocketClient :
    public P44Obj
//    #if ENABLE_WEBSOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT
//    , public P44Script::EventSource
//    #endif
  {
    typedef P44Obj inherited;

    struct uwsc_client* mUwscClient;
    StatusCB mOnOpenCloseCB;
    WebSocketMessageCB mOnMessageCB;

  public:

    WebSocketClient(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~WebSocketClient();

    /// establish a websocket connection
    /// @param aOnOpenCB is called when the websocket is open or connection has failed
    /// @param aUrl the websocket URL
    /// @param aPingInterval the ping interval
    /// @param aExtraHeaders extra headers (separated by \r\n), none if string is empty
    void connectTo(StatusCB aOnOpenCB, const string aUrl, MLMicroSeconds aPingInterval, const string aExtraHeaders);

    /// close the websocket
    /// @param aOnCloseCB is called when the websocket is properly closed
    /// @param aWebSocketCloseCode websocket close code (see RFC 6455, Section 11.7), defaults to "normal" close
    /// @param aReason message indicating reason for closing, NULL if none
    void close(StatusCB aOnCloseCB, int aWebSocketCloseCode = UWSC_CLOSE_STATUS_NORMAL, const char* aReason = NULL);

    /// send a message
    /// @param aMessage the message to send
    /// @param aWebSocketOpCode the websocket opcode (see RFC 6455, Section 11.8), defaults to text frame
    ErrorPtr send(const string aMessage, int aWebSocketOpCode = UWSC_OP_TEXT);

    /// set callback for receiving messages and errors
    /// @param aOnMessageCB is called when messages arrive or errors occur
    void setMessageHandler(WebSocketMessageCB aOnMessageCB) { mOnMessageCB = aOnMessageCB; };

    /// clear all callbacks
    /// @note this is important because handlers might cause retain cycles when they have smart ptr arguments
    virtual void clearCallbacks();

    // helpers
    void cb_onopen();
    void cb_onclose();
    void cb_onmessage(const string aMessage);
    void cb_onerror(ErrorPtr aError);

  };


  #if ENABLE_WEBSOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT

  // FIXME: refactor to have WebSocketClient as the event source

  namespace P44Script {

    class WebSocketObj;

    /// represents a WebSocket
    /// Note: is an event source, but does not expose it directly, only via WebSocketMessageObjs
    class WebSocketObj : public StructuredLookupObject, public EventSource
    {
      typedef StructuredLookupObject inherited;
      WebSocketClientPtr mWebSocket;
    public:
      WebSocketObj(WebSocketClientPtr aWebSocket);
      virtual ~WebSocketObj();
      virtual string getAnnotation() const P44_OVERRIDE { return "websocket"; };
      WebSocketClientPtr webSocket() { return mWebSocket; }
    private:
      void gotMessage(const string aMessage, ErrorPtr aError);
    };

    // get global builtins
    const BuiltinMemberDescriptor* webSocketGlobals();

  }

  #endif // ENABLE_WEBSOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT



} // namespace p44

#endif // ENABLE_UWSC
#endif /* defined(__p44utils__websocket__) */

