//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2025 plan44.ch / Lukas Zeller, Zurich, Switzerland
//
//  Author: Michael Tross <digitalstrom@tross.org>
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

#ifndef __p44utils__jsonwebsocketclient__
#define __p44utils__jsonwebsocketclient__

#include "p44utils_main.hpp"

#if ENABLE_JSON_WEBSOCKET

#include "jsonobject.hpp"
#include "p44obj.hpp"
#include "logger.hpp"
#include "mainloop.hpp"

// Boost.Beast WebSocket includes
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio.hpp>
#include <memory>
#include <string>
#include <queue>

using namespace std;

namespace p44 {

  class JsonWebsocketClient;
  typedef boost::intrusive_ptr<JsonWebsocketClient> JsonWebsocketClientPtr;

  class JsonWebsocketError : public Error
  {
  public:
    enum {
      OK,
      NotConnected,          ///< WebSocket not connected
      ConnectionFailed,      ///< Failed to establish connection
      HandshakeFailed,       ///< WebSocket handshake failed
      InvalidJSON,           ///< Invalid JSON received
      SendFailed,            ///< Failed to send message
      ReceiveFailed,         ///< Failed to receive message
      Timeout,               ///< Operation timeout
      NetworkError,          ///< Network connectivity issue
      AlreadyConnected,      ///< Already connected (cannot connect again)
      InvalidURL,            ///< Invalid WebSocket URL
    };
    typedef int ErrorCodes;

    static const char *domain() { return "JsonWebsocket"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return JsonWebsocketError::domain(); };
    explicit JsonWebsocketError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE;
    #endif
  };


  /// Callback for WebSocket messages
  /// @param aMessage the JSON object received from WebSocket
  /// @param aError error if any
  typedef boost::function<void (JsonObjectPtr aMessage, ErrorPtr aError)> JsonWebsocketMessageCB;

  /// Callback for WebSocket connection status
  /// @param aConnected true if connected, false if disconnected
  /// @param aError error if connection failed
  typedef boost::function<void (bool aConnected, ErrorPtr aError)> JsonWebsocketStatusCB;


  /// JSON WebSocket client using Boost.Beast
  /// This class provides asynchronous WebSocket communication for JSON messages
  /// All operations are non-blocking and integrated with p44utils MainLoop
  class JsonWebsocketClient : public P44LoggingObj
  {
    typedef P44LoggingObj inherited;
    friend class JsonWebsocketOperation;

  public:

    JsonWebsocketClient(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~JsonWebsocketClient();

    /// set the log level offset on this logging object
    virtual void setLogLevelOffset(int aLogLevelOffset) P44_OVERRIDE;

    /// Connect to a WebSocket server
    /// @param aURL the WebSocket URL (e.g., "ws://192.168.1.100/ws" or "wss://device.local/ws")
    /// @param aConnectedCallback called when connection is established or fails
    /// @note the URL must be in format ws://host[:port]/path or wss://host[:port]/path
    /// @note this method is non-blocking; callback will be called later
    void connect(const string &aURL, JsonWebsocketStatusCB aConnectedCallback);

    /// Disconnect from WebSocket server
    /// @param aImmediately if true, close immediately; if false, send proper close frame
    void disconnect(bool aImmediately = false);

    /// Check if currently connected
    /// @return true if WebSocket is connected and ready
    bool isConnected() const { return mConnected; }

    /// Send a JSON message via WebSocket
    /// @param aMessage the JSON object to send
    /// @param aResultCallback optional callback for send confirmation
    /// @return false if client is not connected or too many pending messages
    bool sendJson(JsonObjectPtr aMessage, JsonWebsocketMessageCB aResultCallback = JsonWebsocketMessageCB());

    /// Set callback for incoming messages
    /// @param aMessageCallback called when a JSON message is received
    void setMessageCallback(JsonWebsocketMessageCB aMessageCallback);

    /// Set callback for connection status changes
    /// @param aStatusCallback called when connection is established or lost
    void setStatusCallback(JsonWebsocketStatusCB aStatusCallback);

    /// Get the connected URL
    string getURL() const { return mURL; }

  protected:

    /// Parse WebSocket URL into components
    /// @param aURL the WebSocket URL
    /// @param aHost output parameter for hostname
    /// @param aPort output parameter for port
    /// @param aPath output parameter for path
    /// @return error if URL is invalid
    static ErrorPtr parseWebsocketURL(const string &aURL, string &aHost, string &aPort, string &aPath);

  private:

    MainLoop &mMainLoop;

    // URL components
    string mURL;
    string mHost;
    string mPort;
    string mPath;
    bool mSecure;             ///< true for wss://, false for ws://

    // Connection state
    bool mConnected;
    bool mConnecting;
    bool mDisconnecting;
    bool mSending;             ///< true while an async_write is in flight

    // ASIO and Beast components
    typedef boost::asio::ip::tcp tcp;
    typedef boost::beast::websocket::stream<tcp::socket> WebsocketStream;

    std::shared_ptr<boost::asio::io_context> mIoContext;
    std::shared_ptr<WebsocketStream> mWebsocket;
    std::shared_ptr<boost::asio::ip::tcp::resolver> mResolver;

    // Background I/O thread (runs io_context::run())
    ChildThreadWrapperPtr mIoThread;
    ChildThreadWrapper* mChildThread;  ///< raw pointer valid only while thread runs

    // Message queues
    std::queue<string> mPendingSendMessages;
    static const size_t MAX_PENDING_MESSAGES = 100;

    // Buffers
    std::shared_ptr<boost::beast::flat_buffer> mReadBuffer;

    // Callbacks
    JsonWebsocketMessageCB mMessageCallback;
    JsonWebsocketStatusCB mStatusCallback;
    JsonWebsocketStatusCB mConnectCallback;

    // Async operation methods (all run in io_context thread)
    void doConnect();
    void handleResolve(const boost::system::error_code &aError, tcp::resolver::results_type aResults);
    void handleConnect(const boost::system::error_code &aError);
    void handleHandshake(const boost::system::error_code &aError);
    void startRead();
    void handleRead(const boost::system::error_code &aError, size_t aBytesTransferred);
    void handleWrite(const boost::system::error_code &aError, size_t aBytesTransferred);
    void handleClose(const boost::system::error_code &aError);
    void sendNextMessage();

    // Helper methods (run on main thread)
    void processMessage(const string &aMessageStr);
    void onConnected();
    void onDisconnected(ErrorPtr aError);
    void cleanup();

    // Cross-thread marshaling: call from io_context thread to run on main thread
    void marshalToMainLoop(boost::function<void()> aCallback);
    void signalConnectError(ErrorPtr aError);
  };

} // namespace p44

#endif // ENABLE_JSON_WEBSOCKET

#endif // __p44utils__jsonwebsocketclient__
