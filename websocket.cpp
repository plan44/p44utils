//
//  Copyright (c) 2021-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

// File scope debugging options
// - Set ALWAYS_DEBUG to 1 to enable DBGLOG output even in non-DEBUG builds of this file
#define ALWAYS_DEBUG 0
// - set FOCUSLOGLEVEL to non-zero log level (usually, 5,6, or 7==LOG_DEBUG) to get focus (extensive logging) for this file
//   Note: must be before including "logger.hpp" (or anything that includes "logger.hpp")
#define FOCUSLOGLEVEL 7

#include "websocket.hpp"

#if ENABLE_UWSC

using namespace p44;

WebSocketClient::WebSocketClient(MainLoop &aMainLoop) :
  inherited(aMainLoop),
  mUwscClient(NULL)
{
}


WebSocketClient::~WebSocketClient()
{
  if (mUwscClient) {
    // Note: send_close eventually causes freeing uwsc_client automatically
    mUwscClient->send_close(mUwscClient, UWSC_CLOSE_STATUS_ABNORMAL_CLOSE, "websocket object deleted");
    mUwscClient = NULL;
  }
}


struct uwsc_client_wrapper {
  struct uwsc_client uwscClient;
  WebSocketClient* webSocketClientP;
};

static struct uwsc_client *wrapped_uwsc_new(
  WebSocketClient* aWebSocketClientP,
  struct ev_loop *aLoop, const char *aUrl,
  int aPingInterval, const char *aExtraHeader
) {
  struct uwsc_client_wrapper *wcl;

  wcl = (struct uwsc_client_wrapper*)malloc(sizeof(struct uwsc_client_wrapper));
  if (!wcl) {
    uwsc_log_err("malloc failed: %s\n", strerror(errno));
    return NULL;
  }
  wcl->webSocketClientP = aWebSocketClientP;
  if (uwsc_init((struct uwsc_client *)wcl, aLoop, aUrl, aPingInterval, aExtraHeader) < 0) {
    free(wcl);
    return NULL;
  }
  return (struct uwsc_client *)wcl;
}

static WebSocketClient* wsclient(struct uwsc_client* aCl)
{
  void* h = aCl;
  struct uwsc_client_wrapper* wcl = (struct uwsc_client_wrapper*)h;
  return wcl->webSocketClientP;
}


static void uwsc_onopen(struct uwsc_client *cl)
{
  wsclient(cl)->cb_onopen();
}


static void uwsc_onmessage(struct uwsc_client *cl, void *data, size_t len, bool binary)
{
  string msg;
  msg.assign((char *)data, len);
  wsclient(cl)->cb_onmessage(msg);
}

static void uwsc_onerror(struct uwsc_client *cl, int err, const char *msg)
{
  ErrorPtr errobj = Error::err<WebSocketError>(err, "%s", msg);
  ev_break(cl->loop, EVBREAK_ALL);
  wsclient(cl)->cb_onerror(errobj);
}

static void uwsc_onclose(struct uwsc_client *cl, int code, const char *reason)
{
  ev_break(cl->loop, EVBREAK_ALL);
  wsclient(cl)->cb_onclose();
}


void WebSocketClient::cb_onopen()
{
  DBGLOG(LOG_NOTICE,"onopen");
  if (mOnOpenCloseCB) {
    StatusCB cb = mOnOpenCloseCB;
    mOnOpenCloseCB = NULL;
    cb(ErrorPtr());
  }
}


void WebSocketClient::cb_onclose()
{
  DBGLOG(LOG_NOTICE,"onclose");
  mUwscClient = NULL; // note: it frees itself on close, make sure we don't interact with it any more
  if (mOnOpenCloseCB) {
    StatusCB cb = mOnOpenCloseCB;
    mOnOpenCloseCB = NULL;
    cb(ErrorPtr());
  }
}


void WebSocketClient::cb_onmessage(const string aMessage)
{
  DBGLOG(LOG_NOTICE,"onmessage: %s", aMessage.c_str());
  if (mOnMessageCB) {
    mOnMessageCB(aMessage, ErrorPtr());
  }
}


void WebSocketClient::cb_onerror(ErrorPtr aError)
{
  DBGLOG(LOG_NOTICE,"onerror: %s", Error::text(aError));
  if (mOnMessageCB) {
    mOnMessageCB("", aError);
  }
}


void WebSocketClient::clearCallbacks()
{
  mOnOpenCloseCB = NULL;
  mOnMessageCB = NULL;
}



void WebSocketClient::connectTo(StatusCB aOnOpenCB, const string aUrl, MLMicroSeconds aPingInterval, const string aExtraHeaders)
{
  ErrorPtr err;
  if (mUwscClient) {
    err = Error::err<WebSocketError>(UWSC_ERROR_CONNECT, "already connected");
  }
  else {
    mUwscClient = wrapped_uwsc_new(this, MainLoop::currentMainLoop().libevLoop(), aUrl.c_str(), (int)(aPingInterval/Second), aExtraHeaders.empty() ? NULL : aExtraHeaders.c_str());
    if (!mUwscClient) {
      err = Error::err<WebSocketError>(UWSC_ERROR_NOT_SUPPORT);
    }
    else {
      mUwscClient->onopen = uwsc_onopen;
      mUwscClient->onmessage = uwsc_onmessage;
      mUwscClient->onerror = uwsc_onerror;
      mUwscClient->onclose = uwsc_onclose;
      mOnOpenCloseCB = aOnOpenCB;
      return;
    }
  }
  // report immediate failure
  if (aOnOpenCB) {
    StatusCB cb = mOnOpenCloseCB;
    mOnOpenCloseCB = NULL;
    cb(err);
  }
}


void WebSocketClient::close(StatusCB aOnCloseCB, int aWebSocketCloseCode, const char* aReason)
{
  if (mOnOpenCloseCB) {
    // still in progress opening or closing, call back
    StatusCB cb = mOnOpenCloseCB;
    mOnOpenCloseCB = NULL;
    cb(Error::err<WebSocketError>(UWSC_ERROR_CONNECT, "closing before finished opening"));
  }
  if (mUwscClient) {
    mOnOpenCloseCB = aOnCloseCB;
    mUwscClient->send_close(mUwscClient, aWebSocketCloseCode, aReason);
  }
  else {
    if (aOnCloseCB) {
      aOnCloseCB(Error::err<WebSocketError>(UWSC_ERROR_CONNECT, "not open"));
    }
  }
}


ErrorPtr WebSocketClient::send(const string aMessage, int aWebSocketOpCode)
{
  ErrorPtr err;
  if (mUwscClient && !mOnOpenCloseCB) {
    int res = mUwscClient->send(mUwscClient, aMessage.c_str(), aMessage.size(), aWebSocketOpCode);
    if (res<0) {
      err = Error::err<WebSocketError>(UWSC_ERROR_IO, "cannot send");
    }
  }
  else {
    err = Error::err<WebSocketError>(UWSC_ERROR_CONNECT, "websocket is not (yet) connected");
  }
  return err;
}


// MARK: - script support

#if ENABLE_WEBSOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT

using namespace P44Script;


// close([code [, reason])
static const BuiltInArgDesc close_args[] = { { numeric|optionalarg }, { text|optionalarg } };
static const size_t close_numargs = sizeof(close_args)/sizeof(BuiltInArgDesc);
static void websocket_closed(BuiltinFunctionContextPtr f)
{
  f->finish();
}
static void close_func(BuiltinFunctionContextPtr f)
{
  WebSocketObj* s = dynamic_cast<WebSocketObj*>(f->thisObj().get());
  assert(s);
  ErrorPtr err;
  int closeCode = UWSC_CLOSE_STATUS_NORMAL;
  string closeReason;
  if (f->arg(0)->defined()) closeCode = f->arg(0)->intValue();
  if (f->arg(1)->defined()) closeReason = f->arg(1)->stringValue();
  s->webSocket()->close(boost::bind(&websocket_closed, f), closeCode, closeReason.empty() ? NULL : closeReason.c_str());
}


// send(data [, opcode])
static const BuiltInArgDesc send_args[] = { { any }, { numeric|optionalarg } };
static const size_t send_numargs = sizeof(send_args)/sizeof(BuiltInArgDesc);
static void send_func(BuiltinFunctionContextPtr f)
{
  WebSocketObj* s = dynamic_cast<WebSocketObj*>(f->thisObj().get());
  assert(s);
  ErrorPtr err;
  int opCode = UWSC_OP_TEXT;
  if (f->arg(1)->defined()) opCode = f->arg(1)->intValue();
  string data = f->arg(0)->stringValue(); // might be binary
  err = s->webSocket()->send(data, opCode);
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err));
  }
  else {
    f->finish();
  }
}


// message()
static void message_func(BuiltinFunctionContextPtr f)
{
  WebSocketObj* s = dynamic_cast<WebSocketObj*>(f->thisObj().get());
  assert(s);
  // return event source for messages
  f->finish(new OneShotEventNullValue(s, "websocket message"));
}


static const BuiltinMemberDescriptor webSocketFunctions[] = {
  { "send", executable|error, send_numargs, send_args, &send_func },
  { "close", executable|error, close_numargs, close_args, &close_func },
  { "message", executable|text|null, 0, NULL, &message_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedWebSocketFunctionLookupP = NULL;

WebSocketObj::WebSocketObj(WebSocketClientPtr aWebSocket) :
  mWebSocket(aWebSocket)
{
  registerSharedLookup(sharedWebSocketFunctionLookupP, webSocketFunctions);
  mWebSocket->setMessageHandler(boost::bind(&WebSocketObj::gotMessage, this, _1, _2));
}

static void websocket_away(WebSocketClientPtr aWebSocket)
{
  DBGLOG(LOG_NOTICE, "websocket properly closed after disposal of WebSocketObj");
  aWebSocket->clearCallbacks(); // make sure we can get deleted
}

WebSocketObj::~WebSocketObj()
{
  mWebSocket->clearCallbacks();
  // callback makes sure websocket can close gracefully before object gets killed
  mWebSocket->close(boost::bind(&websocket_away, mWebSocket), UWSC_CLOSE_STATUS_GOINGAWAY, "deleted");
}


void WebSocketObj::gotMessage(const string aMessage, ErrorPtr aError)
{
  if (Error::notOK(aError)) {
    sendEvent(new ErrorValue(aError));
  }
  else {
    sendEvent(new StringValue(aMessage));
  }
}


// websocket(url_or_config_obj [,protocol [, pinginterval]])
static const BuiltInArgDesc websocket_args[] = { { text|json|object }, { text|optionalarg }, { numeric|optionalarg } };
static const size_t websocket_numargs = sizeof(websocket_args)/sizeof(BuiltInArgDesc);
static void websocket_connected(BuiltinFunctionContextPtr f, WebSocketClientPtr aWebsocket, ErrorPtr aError)
{
  if (Error::isOK(aError)) {
    f->finish(new WebSocketObj(aWebsocket));
  }
  else {
    f->finish(new ErrorValue(aError));
  }
}
static void websocket_func(BuiltinFunctionContextPtr f)
{
  string url;
  JsonObjectPtr extraHeaders;
  MLMicroSeconds pingInterval = 5*Minute; // default to every 5 min
  if (f->arg(0)->hasType(text)) {
    url = f->arg(0)->stringValue();
    if (f->numArgs()>=2) {
      extraHeaders = JsonObject::newObj();
      extraHeaders->add("Sec-WebSocket-Protocol", JsonObject::newString(f->arg(1)->stringValue()));
      f->arg(2)->jsonValue();
    }
    if (f->numArgs()>=3) pingInterval = f->arg(2)->doubleValue()*Second;
  }
  else {
    // configured by object
    JsonObjectPtr cfg = f->arg(0)->jsonValue();
    JsonObjectPtr o;
    if (cfg->get("url", o)) url = o->stringValue();
    if (cfg->get("pinginterval", o)) pingInterval = o->doubleValue()*Second;
    extraHeaders = cfg->get("headers");
    if (cfg->get("protocol", o)) {
      if (!extraHeaders) extraHeaders = JsonObject::newObj();
      extraHeaders->add("Sec-WebSocket-Protocol", o);
    }
  }
  string ehstr;
  if (extraHeaders) {
    extraHeaders->resetKeyIteration();
    string hn;
    JsonObjectPtr hv;
    while (extraHeaders->nextKeyValue(hn, hv)) {
      string_format_append(ehstr, "%s: %s\r\n", hn.c_str(), hv->stringValue().c_str());
    }
  }
  WebSocketClientPtr websocket = new WebSocketClient();
  websocket->connectTo(boost::bind(&websocket_connected, f, websocket, _1), url, pingInterval, ehstr);
}

static const BuiltinMemberDescriptor websocketGlobals[] = {
  { "websocket", executable|null, websocket_numargs, websocket_args, &websocket_func },
  { NULL } // terminator
};

WebSocketLookup::WebSocketLookup() :
  inherited(websocketGlobals)
{
}

#endif // ENABLE_WEBSOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT
#endif // ENABLE_UWSC
