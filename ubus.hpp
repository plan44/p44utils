//
//  Copyright (c) 2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__ubus__
#define __p44utils__ubus__

#include "p44utils_common.hpp"

#if ENABLE_UBUS

#include "socketcomm.hpp"
#include "jsonobject.hpp"

extern "C" {
  #include <libubox/blobmsg_json.h>
  #include <libubus.h>
}

using namespace std;

namespace p44 {

  class UbusServer;
  class UbusRequest;

  typedef boost::intrusive_ptr<UbusServer> UbusServerPtr;
  typedef boost::intrusive_ptr<UbusRequest> UbusRequestPtr;

  class UbusError : public Error
  {
  public:
    // Errors
    typedef enum ubus_msg_status ErrorCodes;

    static const char *domain() { return "ubus"; }
    virtual const char *getErrorDomain() const { return UbusError::domain(); };
    UbusError(ErrorCodes aError) : Error(ErrorCode(aError), ubus_strerror(aError)) {};
  };



  /// generic callback for delivering a received ubus message
  /// @param aUbusRequest the request, must call sendResponse() on it in all cases
  /// @param aUbusMethod the name of the method called
  /// @param aJsonRequest the request message in JSON format
  typedef boost::function<void (UbusRequestPtr aUbusRequest, const string aUbusMethod, JsonObjectPtr aJsonRequest)> UbusMethodHandler;


  class UbusRequest : public P44Obj
  {
    typedef P44Obj inherited;
    friend class UbusServer;

    struct ubus_request_data *currentReq; ///< the current request
    struct ubus_request_data deferredReq; ///< the deferred request structure (needed to answer it later)
    UbusServerPtr ubusServer; ///< the ubus server (needed to actually send answer later)
    int ubusErr; ///< the ubus error status set with sendResponse()

    // private constructor, only UbusServer may create me
    UbusRequest(UbusServerPtr aUbusServer, struct ubus_request_data *aReq);
    virtual ~UbusRequest();

    /// to be called before original method handling ends.
    /// In case that request was not yet responded to, this will defer the request for a later call to sendResponse()
    void defer();

    /// @return true if response for this request has been sent
    bool responded();

  public:

    /// send response to this request
    /// @param
    void sendResponse(JsonObjectPtr aResponse, int aUbusErr = UBUS_STATUS_OK);

  };



  class UbusObject : public P44Obj
  {
    typedef P44Obj inherited;
    friend class UbusServer;

    // libubus internals
    string objName;
    struct ubus_object ubusObj;
    struct ubus_object_type ubusObjType;

    UbusMethodHandler methodHandler; ///< the handler for calls to (any) method of this object

    bool registered; ///< if set, prevents adding of new methods

    /// @return the ubus object ready for ubus_add_object()
    /// @note finalizes the internal ubusObj struct on the first call and flags it registered
    struct ubus_object *getUbusObj();

  public:

    /// create new ubus object (descriptor)
    /// @param aObjectName the name of the object
    /// @param aMethodHandler the handler for methods called on the object
    UbusObject(const string aObjectName, UbusMethodHandler aMethodHandler);

    virtual ~UbusObject();

    /// add object method
    /// @note all methods will use the same handler, which must check the method name
    /// @param aMethodName the name of the method
    /// @param aMethodPolicy the policy (suggested syntax) of the method. If null, empty default policy will be published
    /// @note aMethodPolicy array must be terminated by a entry with .name==NULL !
    /// @note The aMethodPolicy struct array passed must remain permanently allocated!
    void addMethod(const string aMethodName, const struct blobmsg_policy *aMethodPolicy = NULL);

  };
  typedef boost::intrusive_ptr<UbusObject> UbusObjectPtr;



  typedef struct UbusServerCtx {
    ubus_context ctx;
    UbusServer &ubusServer;
    UbusServerCtx(UbusServer &aUbusServer) : ubusServer(aUbusServer) {};
  } UbusServerCtx;


  typedef std::list<UbusObjectPtr> UbusObjectsList;

  class UbusServer : public P44Obj
  {
    typedef P44Obj inherited;
    friend class UbusRequest;

    MLTicket restartTicket;

    // ubus context
    UbusServerCtx *ubusServerCtx;
    struct blob_buf responseBuffer;

    // objects
    UbusObjectsList ubusObjects;


  public:

    UbusServer(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~UbusServer();

    /// start the server
    ErrorPtr startServer();

    /// stop the server
    void stopServer();

    /// restart the server
    void restartServer();

    /// register ubus object
    /// @note the actual registration will be done at startServer()
    void registerObject(UbusObjectPtr aUbusObject);

    /// helper to convert blobMsg to JsonObject (as blobmsg library only provides blobmsg to JSON text)
    /// @param attr blob attribute from libubox
    /// @return JsonObject representing the blob attr
    static JsonObjectPtr blobMsgToJsonObject(const struct blob_attr *attr);

  private:

    bool pollHandler(int aFD, int aPollFlags);
    void retryStartServer();

    static void blobMsgToJsonContainer(JsonObjectPtr aContainer, const void *aBlobData, int aBlobLen);

  public:

    // callback from ubus
    int methodHandler(
      struct ubus_object *obj,
      struct ubus_request_data *req, const char *method,
      struct blob_attr *msg
    );

  };


} // namespace p44

#endif // ENABLE_UBUS
#endif /* defined(__p44utils__ubus__) */

