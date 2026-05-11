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

#include "jsonwebsocketclient.hpp"

#if ENABLE_JSON_WEBSOCKET

#include "mainloop.hpp"
#include "logger.hpp"

using namespace boost;
namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
using tcp = boost::asio::ip::tcp;

namespace p44 {

  #if ENABLE_NAMED_ERRORS
  const char* JsonWebsocketError::errorName() const
  {
    switch (getErrorCode()) {
      case NotConnected: return "NotConnected";
      case ConnectionFailed: return "ConnectionFailed";
      case HandshakeFailed: return "HandshakeFailed";
      case InvalidJSON: return "InvalidJSON";
      case SendFailed: return "SendFailed";
      case ReceiveFailed: return "ReceiveFailed";
      case Timeout: return "Timeout";
      case NetworkError: return "NetworkError";
      case AlreadyConnected: return "AlreadyConnected";
      case InvalidURL: return "InvalidURL";
      default: return "UnknownError";
    }
  }
  #endif


  JsonWebsocketClient::JsonWebsocketClient(MainLoop &aMainLoop) :
    mMainLoop(aMainLoop),
    mSecure(false),
    mConnected(false),
    mConnecting(false),
    mDisconnecting(false),
    mSending(false),
    mChildThread(NULL),
    mReadBuffer(new beast::flat_buffer())
  {
    OLOG(LOG_DEBUG, "JsonWebsocketClient created");
  }


  JsonWebsocketClient::~JsonWebsocketClient()
  {
    OLOG(LOG_DEBUG, "JsonWebsocketClient destroyed");
    disconnect(true);
  }


  void JsonWebsocketClient::setLogLevelOffset(int aLogLevelOffset)
  {
    P44LoggingObj::setLogLevelOffset(aLogLevelOffset);
  }


  ErrorPtr JsonWebsocketClient::parseWebsocketURL(const string &aURL, string &aHost, string &aPort, string &aPath)
  {
    // Parse URL: ws://host[:port]/path or wss://host[:port]/path
    size_t protocolEnd = aURL.find("://");
    if (protocolEnd == string::npos)
      return ErrorPtr(new JsonWebsocketError(JsonWebsocketError::InvalidURL));

    string protocol = aURL.substr(0, protocolEnd);
    if (protocol != "ws" && protocol != "wss")
      return ErrorPtr(new JsonWebsocketError(JsonWebsocketError::InvalidURL));

    size_t hostStart = protocolEnd + 3;
    size_t pathStart = aURL.find('/', hostStart);

    string hostPort;
    if (pathStart == string::npos) {
      hostPort = aURL.substr(hostStart);
      aPath = "/";
    } else {
      hostPort = aURL.substr(hostStart, pathStart - hostStart);
      aPath = aURL.substr(pathStart);
    }

    size_t portStart = hostPort.find(':');
    if (portStart == string::npos) {
      aHost = hostPort;
      aPort = (protocol == "wss") ? "443" : "80";
    } else {
      aHost = hostPort.substr(0, portStart);
      aPort = hostPort.substr(portStart + 1);
    }

    return ErrorPtr(); // OK
  }


  void JsonWebsocketClient::connect(const string &aURL, JsonWebsocketStatusCB aConnectedCallback)
  {
    if (mConnected || mConnecting) {
      if (aConnectedCallback)
        aConnectedCallback(false, ErrorPtr(new JsonWebsocketError(JsonWebsocketError::AlreadyConnected)));
      return;
    }

    mURL = aURL;
    mConnectCallback = aConnectedCallback;
    mConnecting = true;
    mDisconnecting = false;

    ErrorPtr err = parseWebsocketURL(aURL, mHost, mPort, mPath);
    if (err) {
      mConnecting = false;
      if (mConnectCallback) {
        mConnectCallback(false, err);
        mConnectCallback = JsonWebsocketStatusCB();
      }
      return;
    }

    mSecure = (aURL.find("wss://") == 0);

    OLOG(LOG_NOTICE, "Connecting to WebSocket: %s (host=%s, port=%s, path=%s)",
         aURL.c_str(), mHost.c_str(), mPort.c_str(), mPath.c_str());

    // Create io_context for this connection
    mIoContext = std::make_shared<net::io_context>();

    // Start background thread that runs the io_context.
    // All beast I/O operations run in this thread; callbacks are marshaled back to
    // the main thread via executeOnParentThread().
    mChildThread = NULL;
    mIoThread = mMainLoop.executeInThread(
      [this](ChildThreadWrapper &aThread) {
        mChildThread = &aThread;
        // Post the initial async connect into the io_context before running it
        net::post(*mIoContext, [this]() { doConnect(); });
        mIoContext->run(); // blocks until io_context is stopped or runs out of work
        mChildThread = NULL;
      },
      [this](ChildThreadWrapper &aChildThread, ThreadSignals aSignalCode) {
        if (aSignalCode == threadSignalCompleted) {
          OLOG(LOG_DEBUG, "WebSocket I/O thread completed");
          mIoThread.reset();
        }
      }
    );
  }


  // ---- runs in io_context thread ----

  void JsonWebsocketClient::doConnect()
  {
    if (!mConnecting) return;

    try {
      mResolver = std::make_shared<tcp::resolver>(*mIoContext);
      mWebsocket = std::make_shared<WebsocketStream>(*mIoContext);

      mResolver->async_resolve(mHost, mPort,
        [this](beast::error_code ec, tcp::resolver::results_type results) {
          handleResolve(ec, results);
        });
    }
    catch (const std::exception &e) {
      OLOG(LOG_ERR, "doConnect exception: %s", e.what());
      signalConnectError(Error::err<JsonWebsocketError>(JsonWebsocketError::ConnectionFailed));
    }
  }


  void JsonWebsocketClient::handleResolve(const boost::system::error_code &ec, tcp::resolver::results_type aResults)
  {
    if (ec) {
      OLOG(LOG_ERR, "DNS resolve failed: %s", ec.message().c_str());
      signalConnectError(Error::err<JsonWebsocketError>(JsonWebsocketError::ConnectionFailed));
      return;
    }

    net::async_connect(mWebsocket->next_layer(), aResults,
      [this](beast::error_code ec, const tcp::endpoint &) {
        handleConnect(ec);
      });
  }


  void JsonWebsocketClient::handleConnect(const boost::system::error_code &ec)
  {
    if (ec) {
      OLOG(LOG_ERR, "TCP connect failed: %s", ec.message().c_str());
      signalConnectError(Error::err<JsonWebsocketError>(JsonWebsocketError::ConnectionFailed));
      return;
    }

    OLOG(LOG_DEBUG, "TCP connection established");

    mWebsocket->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));
    mWebsocket->set_option(websocket::stream_base::decorator(
      [](websocket::request_type& req) {
        req.set(beast::http::field::user_agent, string(BOOST_BEAST_VERSION_STRING) + " p44utils-client");
      }
    ));

    mWebsocket->async_handshake(mHost, mPath,
      [this](beast::error_code ec) {
        handleHandshake(ec);
      });
  }


  void JsonWebsocketClient::handleHandshake(const boost::system::error_code &ec)
  {
    if (ec) {
      OLOG(LOG_ERR, "WebSocket handshake failed: %s", ec.message().c_str());
      signalConnectError(Error::err<JsonWebsocketError>(JsonWebsocketError::HandshakeFailed));
      return;
    }

    OLOG(LOG_NOTICE, "WebSocket handshake successful");

    mConnected = true;
    mConnecting = false;

    // Notify main thread of successful connection.
    // executeOnParentThread() blocks this thread until main thread is done.
    marshalToMainLoop([this]() {
      onConnected();
    });

    // Start the async read loop
    startRead();
  }


  void JsonWebsocketClient::startRead()
  {
    // Called from io_context thread. Launches a single async_read.
    if (!mConnected || !mWebsocket || mDisconnecting) return;

    mReadBuffer->clear();
    mWebsocket->async_read(*mReadBuffer,
      [this](beast::error_code ec, size_t bytes) {
        handleRead(ec, bytes);
      });
  }


  void JsonWebsocketClient::handleRead(const boost::system::error_code &ec, size_t aBytesTransferred)
  {
    if (ec) {
      if (mDisconnecting) return; // normal close — no error callback
      OLOG(LOG_ERR, "WebSocket read failed: %s", ec.message().c_str());
      mConnected = false;
      ErrorPtr err = Error::err<JsonWebsocketError>(JsonWebsocketError::ReceiveFailed);
      marshalToMainLoop([this, err]() {
        onDisconnected(err);
      });
      return;
    }

    // Copy the message string before clearing the buffer
    string msg(
      static_cast<const char*>(mReadBuffer->data().data()),
      mReadBuffer->data().size()
    );

    OLOG(LOG_DEBUG, "WebSocket message received (%zu bytes)", aBytesTransferred);

    // Deliver message to main thread. This blocks the io_context thread until
    // the main thread has processed the callback — which is fine for WLED's low
    // message rate and ensures ordered, single-threaded delivery.
    marshalToMainLoop([this, msg]() {
      processMessage(msg);
    });

    // Continue reading after the main-thread callback has returned
    startRead();
  }


  void JsonWebsocketClient::handleWrite(const boost::system::error_code &ec, size_t /*aBytesTransferred*/)
  {
    mSending = false;

    if (ec) {
      OLOG(LOG_ERR, "WebSocket write failed: %s", ec.message().c_str());
      mConnected = false;
      ErrorPtr err = Error::err<JsonWebsocketError>(JsonWebsocketError::SendFailed);
      marshalToMainLoop([this, err]() {
        onDisconnected(err);
      });
      return;
    }

    // Send next queued message if any
    if (!mPendingSendMessages.empty()) {
      sendNextMessage();
    }
  }


  void JsonWebsocketClient::handleClose(const boost::system::error_code &ec)
  {
    // Graceful close complete — stop io_context so the thread exits
    if (mIoContext) mIoContext->stop();
  }


  // ---- called from main thread ----

  bool JsonWebsocketClient::sendJson(JsonObjectPtr aMessage, JsonWebsocketMessageCB aResultCallback)
  {
    if (!mConnected || !aMessage || mDisconnecting) return false;

    if (mPendingSendMessages.size() >= MAX_PENDING_MESSAGES) {
      OLOG(LOG_WARNING, "Too many pending messages, dropping");
      return false;
    }

    string msgStr = aMessage->json_str();

    // Post to io_context so it runs on the I/O thread alongside reads/writes.
    // net::post() is thread-safe and the lambda is the only place that touches
    // mPendingSendMessages and mSending, keeping them io_context-thread-only.
    net::post(*mIoContext, [this, msgStr]() {
      if (!mConnected || !mWebsocket) return;
      mPendingSendMessages.push(msgStr);
      if (!mSending) sendNextMessage();
    });

    OLOG(LOG_DEBUG, "Queued message for sending: %s", msgStr.c_str());
    return true;
  }


  // ---- runs in io_context thread ----

  void JsonWebsocketClient::sendNextMessage()
  {
    if (!mConnected || !mWebsocket || mPendingSendMessages.empty()) return;

    mSending = true;
    string msgStr = mPendingSendMessages.front();
    mPendingSendMessages.pop();

    mWebsocket->async_write(net::buffer(msgStr),
      [this](beast::error_code ec, size_t bytes) {
        handleWrite(ec, bytes);
      });

    OLOG(LOG_DEBUG, "Sending WebSocket message");
  }


  // ---- called from main thread ----

  void JsonWebsocketClient::disconnect(bool aImmediately)
  {
    if (!mConnected && !mConnecting) return;

    OLOG(LOG_NOTICE, "Disconnecting WebSocket (immediately=%d)", aImmediately);

    mDisconnecting = true;

    if (aImmediately || !mIoContext) {
      // Hard disconnect: stop io_context and close the socket.
      // Any blocking async operations in the io_context thread will return with
      // an error; handleRead/handleWrite check mDisconnecting and exit quietly.
      cleanup();
      mConnected = false;
      mConnecting = false;
      onDisconnected(ErrorPtr());
    } else {
      // Graceful disconnect: send a proper WebSocket close frame.
      // Post to io_context so it runs safely alongside pending reads.
      net::post(*mIoContext, [this]() {
        if (!mWebsocket) return;
        mWebsocket->async_close(websocket::close_code::normal,
          [this](beast::error_code ec) {
            handleClose(ec);
          });
      });
    }
  }


  void JsonWebsocketClient::cleanup()
  {
    // Stop io_context first so the child thread's run() returns
    if (mIoContext) {
      mIoContext->stop();
    }

    // Close the underlying socket to unblock any pending async operations
    try {
      if (mWebsocket) {
        mWebsocket->next_layer().close();
      }
    } catch (...) {}

    mWebsocket.reset();
    mResolver.reset();
    // Keep mIoContext in case the thread hasn't finished yet; it will be reset
    // when mIoThread is released after the thread signals threadSignalCompleted.
  }


  // ---- callbacks (run on main thread) ----

  void JsonWebsocketClient::processMessage(const string &aMessageStr)
  {
    ErrorPtr err;
    JsonObjectPtr jsonMsg = JsonObject::objFromText(aMessageStr.c_str(), aMessageStr.size(), &err);

    if (err || !jsonMsg) {
      OLOG(LOG_WARNING, "Invalid JSON received: %s", aMessageStr.c_str());
      if (mMessageCallback)
        mMessageCallback(JsonObjectPtr(), ErrorPtr(new JsonWebsocketError(JsonWebsocketError::InvalidJSON)));
      return;
    }

    OLOG(LOG_DEBUG, "Processed JSON message");
    if (mMessageCallback) mMessageCallback(jsonMsg, ErrorPtr());
  }


  void JsonWebsocketClient::onConnected()
  {
    OLOG(LOG_NOTICE, "WebSocket connected");

    if (mConnectCallback) {
      mConnectCallback(true, ErrorPtr());
      mConnectCallback = JsonWebsocketStatusCB(); // call once
    }
    if (mStatusCallback) mStatusCallback(true, ErrorPtr());
  }


  void JsonWebsocketClient::onDisconnected(ErrorPtr aError)
  {
    OLOG(LOG_NOTICE, "WebSocket disconnected: %s", aError ? aError->text() : "normal");

    if (mConnectCallback) {
      mConnectCallback(false, aError);
      mConnectCallback = JsonWebsocketStatusCB();
    }
    if (mStatusCallback) mStatusCallback(false, aError);
  }


  void JsonWebsocketClient::setMessageCallback(JsonWebsocketMessageCB aMessageCallback)
  {
    mMessageCallback = aMessageCallback;
  }


  void JsonWebsocketClient::setStatusCallback(JsonWebsocketStatusCB aStatusCallback)
  {
    mStatusCallback = aStatusCallback;
  }


  // ---- cross-thread marshaling ----

  void JsonWebsocketClient::marshalToMainLoop(boost::function<void()> aCallback)
  {
    // Called from io_context thread. Blocks io_context thread until main thread
    // executes aCallback, then unblocks.
    ChildThreadWrapper *thread = mChildThread; // local copy for safety
    if (!thread) return;
    thread->executeOnParentThread([aCallback](ChildThreadWrapper &) -> ErrorPtr {
      aCallback();
      return ErrorPtr();
    });
  }


  void JsonWebsocketClient::signalConnectError(ErrorPtr aError)
  {
    // Called from io_context thread on connection failure
    mConnected = false;
    mConnecting = false;
    // Stop io_context so thread exits after we return
    if (mIoContext) mIoContext->stop();
    marshalToMainLoop([this, aError]() {
      onDisconnected(aError);
    });
  }

} // namespace p44

#endif // ENABLE_JSON_WEBSOCKET
