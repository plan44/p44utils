//
//  Copyright (c) 2019-2021 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

UbusRequest::UbusRequest(UbusServerPtr aUbusServer, struct ubus_request_data *aReq) :
  ubusServer(aUbusServer),
  currentReq(aReq)
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
  if (ubusServer && currentReq) {
    ubus_defer_request(&ubusServer->mUbusServerCtx->ctx, currentReq, &deferredReq);
    currentReq = NULL; // can no longer directly respond
  }
}


bool UbusRequest::responded()
{
  return ubusServer.get()==NULL;
}



void UbusRequest::sendResponse(JsonObjectPtr aResponse, int aUbusErr)
{
  ubusErr = aUbusErr;
  if (ubusServer) {
    // send reply
    POLOG(ubusServer, LOG_INFO, "response status: %d, message: %s", aUbusErr, aResponse ? aResponse->json_c_str() : "<none>");
    struct blob_buf responseBuffer;
    memset(&responseBuffer, 0, sizeof(responseBuffer)); // essential for blob_buf_init
    blob_buf_init(&responseBuffer, 0);
    if (aResponse) blobmsg_add_object(&responseBuffer, (struct json_object *)aResponse->jsoncObj());
    if (currentReq) {
      ubus_send_reply(&ubusServer->mUbusServerCtx->ctx, currentReq, responseBuffer.head);
    }
    else {
      // is a deferred request
      ubus_send_reply(&ubusServer->mUbusServerCtx->ctx, &deferredReq, responseBuffer.head);
      ubus_complete_deferred_request(&ubusServer->mUbusServerCtx->ctx, &deferredReq, ubusErr);
      ubusErr = UBUS_STATUS_OK;
    }
    // response is out, can no longer be used and must not keep server alive any more
    ubusServer.reset();
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
  OLOG(LOG_INFO, "object '%s' got method call '%s' with message: %s", obj->name, method, jsonMsg ? jsonMsg->json_c_str() : "<none>");

  // wrap request for processing
  UbusRequestPtr ureq = UbusRequestPtr(new UbusRequest(this, req));
  // look for object
  for (UbusObjectsList::iterator pos = mUbusObjects.begin(); pos!=mUbusObjects.end(); ++pos) {
    if ((*pos)->objName==obj->name && (*pos)->methodHandler) {
      // object found and has a method handler -> call it
      (*pos)->methodHandler(ureq, method, jsonMsg);
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



// MARK: ==== %%% experimental


#if ___gugus2


enum {
  HELLO_ID,
  HELLO_MSG,
  __HELLO_MAX
};

static const struct blobmsg_policy hello_policy[] = {
  [HELLO_ID] = { .name = "id", .type = BLOBMSG_TYPE_INT32 },
  [HELLO_MSG] = { .name = "msg", .type = BLOBMSG_TYPE_STRING },
  [__HELLO_MAX] = { .name = NULL, .type = BLOBMSG_TYPE_UNSPEC },
};


#define CPP_UBUS_METHOD(_name, _handler, _policy) { _name, _handler, 0, 0, _policy, ARRAY_SIZE(_policy) }
#define CPP_UBUS_OBJECT_TYPE(_name, _methods) { _name, 0, _methods, ARRAY_SIZE(_methods) }

static const struct ubus_method test_methods[] = {
  CPP_UBUS_METHOD("hello", method_handler, hello_policy),
};

static struct ubus_object_type test_object_type =
  CPP_UBUS_OBJECT_TYPE("test", test_methods);

static struct ubus_object test_object;

ErrorPtr UbusServer::registerObject()
{
  test_object.name = "test";
  test_object.type = &test_object_type;
  test_object.methods = test_methods;
  test_object.n_methods = ARRAY_SIZE(test_methods);
  int ret = ubus_add_object(&mUbusServerCtx->ctx, &test_object);
  if (ret) {
    return Error::err<UbusError>(ret);
  }
  return ErrorPtr();
}

#endif


#ifdef ___gugus

static struct ubus_context *ctx;
static struct ubus_subscriber test_event;
static struct blob_buf b;

enum {
  HELLO_ID,
  HELLO_MSG,
  __HELLO_MAX
};

static const struct blobmsg_policy hello_policy[] = {
  [HELLO_ID] = { .name = "id", .type = BLOBMSG_TYPE_INT32 },
  [HELLO_MSG] = { .name = "msg", .type = BLOBMSG_TYPE_STRING },
};

struct hello_request {
  struct ubus_request_data req;
  struct uloop_timeout timeout;
  int fd;
  int idx;
  char data[];
};

static void test_hello_fd_reply(struct uloop_timeout *t)
{
  struct hello_request *req = container_of(t, struct hello_request, timeout);
  char *data;

  data = alloca(strlen(req->data) + 32);
  sprintf(data, "msg%d: %s\n", ++req->idx, req->data);
  if (write(req->fd, data, strlen(data)) < 0) {
    close(req->fd);
    free(req);
    return;
  }

  uloop_timeout_set(&req->timeout, 1000);
}

static void test_hello_reply(struct uloop_timeout *t)
{
  struct hello_request *req = container_of(t, struct hello_request, timeout);
  int fds[2];

  blob_buf_init(&b, 0);
  blobmsg_add_string(&b, "message", req->data);
  ubus_send_reply(ctx, &req->req, b.head);

  if (pipe(fds) == -1) {
    fprintf(stderr, "Failed to create pipe\n");
    return;
  }
  ubus_request_set_fd(ctx, &req->req, fds[0]);
  ubus_complete_deferred_request(ctx, &req->req, 0);
  req->fd = fds[1];

  req->timeout.cb = test_hello_fd_reply;
  test_hello_fd_reply(t);
}

static int test_hello(struct ubus_context *ctx, struct ubus_object *obj,
                      struct ubus_request_data *req, const char *method,
                      struct blob_attr *msg)
{
  struct hello_request *hreq;
  struct blob_attr *tb[__HELLO_MAX];
  const char *format = "%s received a message: %s";
  const char *msgstr = "(unknown)";

  blobmsg_parse(hello_policy, ARRAY_SIZE(hello_policy), tb, blob_data(msg), blob_len(msg));

  if (tb[HELLO_MSG])
    msgstr = blobmsg_data(tb[HELLO_MSG]);

  hreq = calloc(1, sizeof(*hreq) + strlen(format) + strlen(obj->name) + strlen(msgstr) + 1);
  if (!hreq)
    return UBUS_STATUS_UNKNOWN_ERROR;

  sprintf(hreq->data, format, obj->name, msgstr);

  // hreq is now a specialized ubus_request_data, req as first member

  // - defer answering the req (means: copy it into hreq->req, set deferred flag in original req)
  ubus_defer_request(ctx, req, &hreq->req);

  // schedule the deferred handling for later
  hreq->timeout.cb = test_hello_reply;
  uloop_timeout_set(&hreq->timeout, 1000);

  return 0;
}

enum {
  WATCH_ID,
  WATCH_COUNTER,
  __WATCH_MAX
};

static const struct blobmsg_policy watch_policy[__WATCH_MAX] = {
  [WATCH_ID] = { .name = "id", .type = BLOBMSG_TYPE_INT32 },
  [WATCH_COUNTER] = { .name = "counter", .type = BLOBMSG_TYPE_INT32 },
};

static void
test_handle_remove(struct ubus_context *ctx, struct ubus_subscriber *s,
                   uint32_t id)
{
  fprintf(stderr, "Object %08x went away\n", id);
}

static int
test_notify(struct ubus_context *ctx, struct ubus_object *obj,
            struct ubus_request_data *req, const char *method,
            struct blob_attr *msg)
{
#if 0
  char *str;

  str = blobmsg_format_json(msg, true);
  fprintf(stderr, "Received notification '%s': %s\n", method, str);
  free(str);
#endif

  return 0;
}

static int test_watch(struct ubus_context *ctx, struct ubus_object *obj,
                      struct ubus_request_data *req, const char *method,
                      struct blob_attr *msg)
{
  struct blob_attr *tb[__WATCH_MAX];
  int ret;

  blobmsg_parse(watch_policy, __WATCH_MAX, tb, blob_data(msg), blob_len(msg));
  if (!tb[WATCH_ID])
    return UBUS_STATUS_INVALID_ARGUMENT;

  test_event.remove_cb = test_handle_remove;
  test_event.cb = test_notify;
  ret = ubus_subscribe(ctx, &test_event, blobmsg_get_u32(tb[WATCH_ID]));
  fprintf(stderr, "Watching object %08x: %s\n", blobmsg_get_u32(tb[WATCH_ID]), ubus_strerror(ret));
  return ret;
}

enum {
  COUNT_TO,
  COUNT_STRING,
  __COUNT_MAX
};

static const struct blobmsg_policy count_policy[__COUNT_MAX] = {
  [COUNT_TO] = { .name = "to", .type = BLOBMSG_TYPE_INT32 },
  [COUNT_STRING] = { .name = "string", .type = BLOBMSG_TYPE_STRING },
};

static int test_count(struct ubus_context *ctx, struct ubus_object *obj,
                      struct ubus_request_data *req, const char *method,
                      struct blob_attr *msg)
{
  struct blob_attr *tb[__COUNT_MAX];
  char *s1, *s2;
  uint32_t num;

  blobmsg_parse(count_policy, __COUNT_MAX, tb, blob_data(msg), blob_len(msg));
  if (!tb[COUNT_TO] || !tb[COUNT_STRING])
    return UBUS_STATUS_INVALID_ARGUMENT;

  num = blobmsg_get_u32(tb[COUNT_TO]);
  s1 = blobmsg_get_string(tb[COUNT_STRING]);
  s2 = count_to_number(num);
  if (!s1 || !s2) {
    free(s2);
    return UBUS_STATUS_UNKNOWN_ERROR;
  }
  blob_buf_init(&b, 0);
  blobmsg_add_u32(&b, "rc", strcmp(s1, s2));
  ubus_send_reply(ctx, req, b.head);
  free(s2);

  return 0;
}

static const struct ubus_method test_methods[] = {
  UBUS_METHOD("hello", test_hello, hello_policy),
  UBUS_METHOD("watch", test_watch, watch_policy),
  UBUS_METHOD("count", test_count, count_policy),
};

static struct ubus_object_type test_object_type =
UBUS_OBJECT_TYPE("test", test_methods);

static struct ubus_object test_object = {
  .name = "test",
  .type = &test_object_type,
  .methods = test_methods,
  .n_methods = ARRAY_SIZE(test_methods),
};

static void server_main(void)
{
  int ret;

  ret = ubus_add_object(ctx, &test_object);
  if (ret)
    fprintf(stderr, "Failed to add object: %s\n", ubus_strerror(ret));

  ret = ubus_register_subscriber(ctx, &test_event);
  if (ret)
    fprintf(stderr, "Failed to add watch handler: %s\n", ubus_strerror(ret));

  uloop_run();
}

int main(int argc, char **argv)
{
  const char *ubus_socket = NULL;
  int ch;

  while ((ch = getopt(argc, argv, "cs:")) != -1) {
    switch (ch) {
      case 's':
        ubus_socket = optarg;
        break;
      default:
        break;
    }
  }

  argc -= optind;
  argv += optind;

  uloop_init();
  signal(SIGPIPE, SIG_IGN);

  ctx = ubus_connect(ubus_socket);
  if (!ctx) {
    fprintf(stderr, "Failed to connect to ubus\n");
    return -1;
  }

  ubus_add_uloop(ctx);

  server_main();

  ubus_free(ctx);
  uloop_done();

  return 0;
}


#endif // ___gugus

#endif // ENABLE_UBUS
