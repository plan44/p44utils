//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2019-2023 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#include "ubus.hpp"

#if ENABLE_UBUS

using namespace p44;


UbusServer::UbusServer() :
  mUbusServerCtx(NULL)
{
}


UbusServer::~UbusServer()
{
  stopServer();
}


ErrorPtr UbusServer::startServer()
{
  if (!mUbusServerCtx) {
    // create derived context
    mUbusServerCtx = new UbusServerCtx(*this);
    // init the context, create the ubus socket (using the default socket by passing NULL)
    if (ubus_connect_ctx(&mUbusServerCtx->ctx, NULL)<0) {
      delete mUbusServerCtx;
      mUbusServerCtx = NULL;
      return Error::err<UbusError>(UBUS_STATUS_CONNECTION_FAILED);
    }
    // register poll handler for the ubus socket
    MainLoop::currentMainLoop().registerPollHandler(mUbusServerCtx->ctx.sock.fd, POLLIN, boost::bind(&UbusServer::pollHandler, this, _1, _2));
    // register the objects
    for (UbusObjectsList::iterator pos = mUbusObjects.begin(); pos!=mUbusObjects.end(); ++pos) {
      int ret = ubus_add_object(&mUbusServerCtx->ctx, (*pos)->getUbusObj());
      if (ret) {
        return Error::err<UbusError>(ret);
      }
    }
  }
  return ErrorPtr();
}


void UbusServer::stopServer()
{
  if (mUbusServerCtx) {
    ubus_shutdown(&mUbusServerCtx->ctx);
    delete mUbusServerCtx;
    mUbusServerCtx = NULL;
  }
}


#define UBUS_RESTART_INTERVAL (10*Second)

void UbusServer::restartServer()
{
  stopServer();
  mRestartTicket.executeOnce(boost::bind(&UbusServer::retryStartServer, this), UBUS_RESTART_INTERVAL);
}


void UbusServer::retryStartServer()
{
  ErrorPtr err = startServer();
  if (Error::notOK(err)) {
    restartServer();
  }
}



bool UbusServer::pollHandler(int aFD, int aPollFlags)
{
  FOCUSOLOG("ubusPollHandler(time==%lld, fd==%d, pollflags==0x%X)", MainLoop::now(), aFD, aPollFlags);
  // Note: test POLLIN first, because we might get a POLLHUP in parallel - so make sure we process data before hanging up
  if ((aPollFlags & POLLIN)) {
    // let ubus handle it
    ubus_handle_event(&mUbusServerCtx->ctx);
  }
  if (aPollFlags & (POLLHUP|POLLERR)) {
    // some sort of error
    OLOG(LOG_WARNING, "socket closed or returned error: terminating connection");
    restartServer();
  }
  // handled
  return true;
}


void UbusServer::registerObject(UbusObjectPtr aUbusObject)
{
  // only save in my list, will be actually registered at startServer()
  // (because ubus_add_object() needs a active context which is created not before ubus_connect_ctx())
  mUbusObjects.push_back(aUbusObject);
}



// MARK: - UbusRequest

UbusRequest::UbusRequest(UbusServerPtr aUbusServer, struct ubus_request_data *aReq, const char *aMethodName, JsonObjectPtr aMsg) :
  mUbusServer(aUbusServer),
  mCurrentReqP(aReq),
  mRequestMethod(aMethodName),
  mRequestMsg(aMsg)
{
}


UbusRequest::~UbusRequest()
{
  if (!responded()) {
    // make sure unresponded requests don't hang, but error out
    sendResponse(JsonObjectPtr(), UBUS_STATUS_UNKNOWN_ERROR);
  }
}


void UbusRequest::defer()
{
  if (mUbusServer && mCurrentReqP) {
    ubus_defer_request(&mUbusServer->mUbusServerCtx->ctx, mCurrentReqP, &mDeferredReq);
    mCurrentReqP = NULL; // can no longer directly respond
  }
}


bool UbusRequest::responded()
{
  return mUbusServer.get()==NULL;
}



void UbusRequest::sendResponse(JsonObjectPtr aResponse, int aUbusErr)
{
  mUbusErr = aUbusErr;
  if (mUbusServer) {
    // send reply
    POLOG(mUbusServer, LOG_INFO, "response status: %d, message: %s", aUbusErr, JsonObject::text(aResponse));
    struct blob_buf responseBuffer;
    memset(&responseBuffer, 0, sizeof(responseBuffer)); // essential for blob_buf_init
    blob_buf_init(&responseBuffer, 0);
    if (aResponse) blobmsg_add_object(&responseBuffer, (struct json_object *)aResponse->jsoncObj());
    if (mCurrentReqP) {
      ubus_send_reply(&mUbusServer->mUbusServerCtx->ctx, mCurrentReqP, responseBuffer.head);
    }
    else {
      // is a deferred request
      ubus_send_reply(&mUbusServer->mUbusServerCtx->ctx, &mDeferredReq, responseBuffer.head);
      ubus_complete_deferred_request(&mUbusServer->mUbusServerCtx->ctx, &mDeferredReq, mUbusErr);
      mUbusErr = UBUS_STATUS_OK;
    }
    // response is out, can no longer be used and must not keep server alive any more
    mRequestMsg.reset(); // no message any more
    mRequestMethod.clear(); // no method any more
    mUbusServer.reset(); // release server
  }
}



int UbusServer::methodHandler(
  struct ubus_object *obj,
  struct ubus_request_data *req, const char *method,
  struct blob_attr *msg
)
{
  // Dispatch call to registered object, if any
  // - msg is a table container w/o having a header for it
  JsonObjectPtr jsonMsg = JsonObject::newObj();
  blobMsgToJsonContainer(jsonMsg, blobmsg_data(msg), blobmsg_data_len(msg));
  OLOG(LOG_INFO, "object '%s' got method call '%s' with message: %s", obj->name, method, JsonObject::text(jsonMsg));

  // wrap request for processing
  UbusRequestPtr ureq = UbusRequestPtr(new UbusRequest(this, req, method, jsonMsg));
  // look for object
  for (UbusObjectsList::iterator pos = mUbusObjects.begin(); pos!=mUbusObjects.end(); ++pos) {
    if ((*pos)->objName==obj->name && (*pos)->methodHandler) {
      // object found and has a method handler -> call it
      (*pos)->methodHandler(ureq);
      // defer if not yet finished
      ureq->defer();
      return 0;
    }
  }
  // no object to handle it -> immediately respond with "unsupported"
  ureq->sendResponse(JsonObjectPtr(), UBUS_STATUS_NOT_SUPPORTED);
  return 0;
}


// MARK: - convert blob message to json
// NOTE: other way around (json -> blobmsg) exists in blobmsg_json already, also using json-C


void UbusServer::blobMsgToJsonContainer(JsonObjectPtr aContainer, const void *aBlobData, int aBlobLen)
{
  struct blob_attr *pos;
  int rem = aBlobLen;
  __blob_for_each_attr(pos, aBlobData, rem) {
    if (aContainer->isType(json_type_array)) {
      aContainer->arrayAppend(blobMsgToJsonObject(pos));
    }
    else {
      aContainer->add(blobmsg_name(pos), blobMsgToJsonObject(pos));
    }
  }
}


JsonObjectPtr UbusServer::blobMsgToJsonObject(const struct blob_attr *aBlobAttr)
{
  JsonObjectPtr newObj;
  if (!blobmsg_check_attr(aBlobAttr, false))
    return newObj;
  void *data = blobmsg_data(aBlobAttr);
  int len = blobmsg_data_len(aBlobAttr);
  switch(blob_id(aBlobAttr)) {
    case BLOBMSG_TYPE_UNSPEC:
      newObj = JsonObject::newNull();
      break;
    case BLOBMSG_TYPE_BOOL:
      newObj = JsonObject::newBool(*(uint8_t *)data);
      break;
    case BLOBMSG_TYPE_INT16:
      newObj = JsonObject::newInt32(be16_to_cpu(*(uint16_t *)data));
      break;
    case BLOBMSG_TYPE_INT32:
      newObj = JsonObject::newInt32(be32_to_cpu(*(uint32_t *)data));
      break;
    case BLOBMSG_TYPE_INT64:
      newObj = JsonObject::newInt64(be64_to_cpu(*(uint64_t *)data));
      break;
    case BLOBMSG_TYPE_DOUBLE:
      newObj = JsonObject::newDouble(blobmsg_get_double((struct blob_attr *)aBlobAttr));
      break;
    case BLOBMSG_TYPE_STRING:
      newObj = JsonObject::newString((const char *)data, (size_t)len-1); // do not include terminator
      break;
    case BLOBMSG_TYPE_ARRAY:
      newObj = JsonObject::newArray();
      blobMsgToJsonContainer(newObj, data, len);
      break;
    case BLOBMSG_TYPE_TABLE:
      newObj = JsonObject::newObj();
      blobMsgToJsonContainer(newObj, data, len);
      break;
  }
  return newObj;
}


// MARK: ==== C-level method handler callback


static int method_handler(
  struct ubus_context *ctx, struct ubus_object *obj,
  struct ubus_request_data *req, const char *method,
  struct blob_attr *msg
)
{
  UbusServerCtx *c = (UbusServerCtx*)(ctx);
  return c->ubusServer.methodHandler(obj, req, method, msg);
}


// MARK: ==== UbusObject

UbusObject::UbusObject(const string aObjectName, UbusMethodHandler aMethodHandler) :
  objName(aObjectName),
  methodHandler(aMethodHandler),
  registered(false)
{
  // object type
  memset(&ubusObjType, 0, sizeof(ubusObjType));
  ubusObjType.name = objName.c_str(); // type has same name as object
  ubusObjType.id = 0;
  ubusObjType.methods = NULL;
  ubusObjType.n_methods = 0;
  // object instance
  memset(&ubusObj, 0, sizeof(ubusObj));
  ubusObj.name = objName.c_str();
  ubusObj.type = &ubusObjType;
}


UbusObject::~UbusObject()
{
  if (ubusObjType.methods) {
    for (int i=0; i<ubusObjType.n_methods; i++) {
      delete ubusObjType.methods[i].name;
    }
    delete(ubusObjType.methods);
    ubusObjType.methods = NULL;
    ubusObjType.n_methods = 0;
  }
}


struct ubus_object *UbusObject::getUbusObj()
{
  if (!registered) {
    // finalize
    // - object instance inherits methods from type
    ubusObj.n_methods = ubusObjType.n_methods;
    ubusObj.methods = ubusObjType.methods;
    // - lock now
    registered = true;
  }
  return &ubusObj;
}


void UbusObject::addMethod(const string aMethodName, const struct blobmsg_policy *aMethodPolicy)
{
  if (registered) return; // cannot add methods when already registered!
  // extend (or create) array of ubus_method structs
  const struct ubus_method *oldMethods = ubusObjType.methods;
  ubusObjType.methods = new struct ubus_method[ubusObjType.n_methods+1];
  for (int i=0; i<ubusObjType.n_methods; i++) memcpy((void *)&ubusObjType.methods[i], (void *)&oldMethods[i], sizeof(struct ubus_method));
  if (oldMethods) delete[] oldMethods;
  // init new method
  struct ubus_method *m = (struct ubus_method *)&ubusObjType.methods[ubusObjType.n_methods];
  // - name must be allocated
  m->name = new char[aMethodName.size()+1];
  strcpy((char *)m->name, aMethodName.c_str());
  // - handler is always our standard handler
  m->handler = method_handler;
  m->mask = 0;
  m->tags = 0;
  m->n_policy = 0;
  m->policy = NULL;
  if (aMethodPolicy!=NULL) {
    const struct blobmsg_policy *p = aMethodPolicy;
    // search for terminator
    while (p->name!=0) { m->n_policy++; p++; }
    m->policy = aMethodPolicy;
  }
  // finished -> count it
  ubusObjType.n_methods++;
}


#endif // ENABLE_UBUS
