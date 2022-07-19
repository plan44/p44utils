//
//  Copyright (c) 2013-2022 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__socketcomm__
#define __p44utils__socketcomm__

#include "p44utils_main.hpp"

#include "fdcomm.hpp"

// unix I/O and network
#include <sys/socket.h>
#ifdef ESP_PLATFORM
  #define  NI_MAXHOST  1025
  #define  NI_MAXSERV  32
#else
  #include <sys/un.h>
#endif
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>

#if ENABLE_P44SCRIPT && !defined(ENABLE_SOCKET_SCRIPT_FUNCS)
  #define ENABLE_SOCKET_SCRIPT_FUNCS 1
#endif

#if ENABLE_SOCKET_SCRIPT_FUNCS
#include "p44script.hpp"
#endif


using namespace std;

namespace p44 {

  class SocketCommError : public Error
  {
  public:
    // Errors
    typedef enum {
      OK,
      NoParams, ///< parameters missing to even try initiating connection
      Unsupported, ///< unsupported mode/feature
      CannotResolve, ///< host or service name cannot be resolved
      NoConnection, ///< no connection could be established (none of the addresses worked)
      HungUp, ///< other side closed connection (hung up, HUP)
      Closed, ///< closed from my side
      FDErr, ///< error on file descriptor
      numErrorCodes
    } ErrorCodes;
    
    static const char *domain() { return "SocketComm"; }
    virtual const char *getErrorDomain() const P44_OVERRIDE { return SocketCommError::domain(); };
    SocketCommError(ErrorCodes aError) : Error(ErrorCode(aError)) {};
    #if ENABLE_NAMED_ERRORS
  protected:
    virtual const char* errorName() const P44_OVERRIDE { return errNames[getErrorCode()]; };
  private:
    static constexpr const char* const errNames[numErrorCodes] = {
      "OK",
      "NoParams",
      "Unsupported",
      "CannotResolve",
      "NoConnection",
      "HungUp",
      "Closed",
      "FDErr",
    };
    #endif // ENABLE_NAMED_ERRORS
  };


  class SocketComm;

  typedef boost::intrusive_ptr<SocketComm> SocketCommPtr;
  typedef std::list<SocketCommPtr> SocketCommList;

  /// callback for signalling ready for receive or transmit, or error
  typedef boost::function<void (SocketCommPtr aSocketComm, ErrorPtr aError)> SocketCommCB;
  /// callback for accepting new server connections
  /// @return must return a new SocketComm connection object which will handle the connection
  typedef boost::function<SocketCommPtr (SocketCommPtr aServerSocketComm)> ServerConnectionCB;


  /// A class providing socket communication (client and server)
  class SocketComm : public FdComm
  {
    typedef FdComm inherited;

    // connection parameter
    string mHostNameOrAddress;
    string mServiceOrPortOrSocket;
    int mProtocolFamily;
    int mSocketType;
    int mProtocol;
    string mInterface;
    bool mServing; ///< is serving socket (TCP) or receiving socket (UDP)
    bool mNonLocal; ///< if set, server sockets (TCP) and receiving sockets (UDP) are bound to external interfaces, otherwise to local loopback only
    bool mConnectionLess; ///< if set, this is a socket w/o connections (datagram, UDP)
    bool mBroadcast; ///< if set and this is a connectionless socket (UDP), it will also receive broadcasts, not only unicasts
    // connection making fd (for server to listen, for clients or server handlers for opening connection)
    int mConnectionFd;
    // client connection internals
    struct addrinfo *mAddressInfoList; ///< list of possible connection addresses
    struct addrinfo *mCurrentAddressInfo; ///< address currently connecting to
    struct sockaddr *mCurrentSockAddrP; ///< address info as currently in use by open connection
    socklen_t mCurrentSockAddrLen; ///< length of current sockAddr struct
    struct sockaddr *mPeerSockAddrP; ///< address info of last UDP receive
    socklen_t mPeerSockAddrLen; ///< length of address info of last UDP receive
    bool mIsConnecting; ///< in progress of opening connection
    bool mIsClosing; ///< in progress of closing connection
    bool mConnectionOpen; ///< regular data connection is open
    bool mClearHandlersAtClose; ///< when socket closes, all handlers are cleared (to break retain cycles)
    SocketCommCB mConnectionStatusHandler;
    // server connection internals
    int mMaxServerConnections;
    ServerConnectionCB mServerConnectionHandler;
    SocketCommList mClientConnections;
    SocketCommPtr mServerConnection;
  public:

    SocketComm(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~SocketComm();

    /// Set parameters for connection (client and server)
    /// @param aHostNameOrAddress host name/address (1.2.3.4 or xxx.yy) - client only
    /// @param aServiceOrPortOrSocket port number, service name or absolute local socket path
    /// @param aSocketType defaults to SOCK_STREAM (TCP)
    /// @param aProtocolFamily defaults to PF_UNSPEC, means that address family is derived from host name and/or service name (starting with slash means PF_LOCAL)
    /// @param aProtocol defaults to 0
    /// @param aInterface specific network interface to be used, defaults to NULL
    /// @note must be called before initiateConnection() or startServer()
    void setConnectionParams(const char* aHostNameOrAddress, const char* aServiceOrPortOrSocket, int aSocketType = SOCK_STREAM, int aProtocolFamily = PF_UNSPEC, int aProtocol = 0, const char* aInterface = NULL);

    /// Set if server may accept non-local connections
    /// @param aAllow if set, server accepts non-local connections
    /// @note must be called before initiateConnection() or startServer()
    void setAllowNonlocalConnections(bool aAllow) { mNonLocal = aAllow; };

    /// Set datagram (UDP) socket options
    /// @param aReceive set to enable receiving (using protocol family as set in setConnectionParams()).
    ///   If setAllowNonlocalConnections(true) was used, the socket will be bound to INADDR_ANY/in6addr_any,
    ///   otherwise to the local loopback only
    /// @param aBroadcast if true, socket will be configured to allow broadcast (sending and receiving)
    /// @note must be called before initiateConnection()
    void setDatagramOptions(bool aReceive, bool aBroadcast) { mServing = aReceive; mBroadcast = aBroadcast; }

    /// get host name we are connected to (useful for server to query connecting client's address)
    /// @return name or IP address of host (for server: actually connected, for client: as set with setConnectionParams())
    const char *getHost() { return mHostNameOrAddress.c_str(); };

    /// get port, service name or socket path
    /// @return port/service/path (for server: actually connected, for client: as set with setConnectionParams())
    const char *getPort() { return mServiceOrPortOrSocket.c_str(); };

    /// get datagram origin information
    /// @param aAddress will be set to address of datagram origin
    /// @param aPort will be set to port of datagram origin
    /// @return true if origin information is available
    /// @note only works for SOCK_DGRAM type connections, and is valid only after a successful receiveBytes() operation
    bool getDatagramOrigin(string &aAddress, string &aPort);

    /// start the server
    /// @param aServerConnectionHandler will be called when a server connection is accepted
    ///   The SocketComm object passed in the handler is a new SocketComm object for that particular connection
    /// @param aMaxConnections max number of simultaneous server connections
    ///   local connections are accepted
    ErrorPtr startServer(ServerConnectionCB aServerConnectionHandler, int aMaxConnections);

    /// initiate the connection (non-blocking)
    /// This starts the connection process
    /// @return if no error is returned, this means the connection could be initiated
    ///   (but actual connection might still fail)
    /// @note can be called multiple times, initiates connection only if not already open or initiated
    ///   When connection status changes, the connectionStatusHandler (if set) will be called
    /// @note if connectionStatusHandler is set, it will be called when initiation fails with the same error
    ///   code as returned by initiateConnection itself.
    ErrorPtr initiateConnection();

    /// close the current connection, if any, or stop the server and close all client connections in case of a server
    /// @note can be called multiple times, closes connection if a connection is open (or connecting)
    void closeConnection();

    /// set connection status handler
    /// @param aConnectionStatusHandler will be called when connection status changes.
    ///   If callback is called without error, connection was established. Otherwise, error signals
    ///   why connection was closed
    void setConnectionStatusHandler(SocketCommCB aConnectionStatusHandler);

    /// check if parameters set so connection could be initiated
    /// @return true if connection can be initiated
    bool connectable();

    /// check if connection in progress
    /// @return true if connection initiated and in progress.
    /// @note checking connecting does not automatically try to establish a connection
    bool connecting();

    /// check if connected
    /// @return true if connected.
    /// @note checking connected does not automatically try to establish a connection
    /// @note for connectionless sockets, connected() means "ready to transmit/receive data"
    bool connected();

    /// write data (non-blocking)
    /// @param aNumBytes number of bytes to transfer
    /// @param aBytes pointer to buffer to be sent
    /// @param aError reference to ErrorPtr. Will be left untouched if no error occurs
    /// @return number ob bytes actually written, can be 0 (e.g. if connection is still in process of opening)
    /// @note for UDP, the host/port specified in setConnectionParams() will be used to send datagrams to
    virtual size_t transmitBytes(size_t aNumBytes, const uint8_t *aBytes, ErrorPtr &aError);

    /// read data (non-blocking)
    /// @param aNumBytes max number of bytes to receive
    /// @param aBytes pointer to buffer to store received bytes
    /// @param aError reference to ErrorPtr. Will be left untouched if no error occurs
    /// @return number ob bytes actually read
    virtual size_t receiveBytes(size_t aNumBytes, uint8_t *aBytes, ErrorPtr &aError);

    /// clear all callbacks
    /// @note this is important because handlers might cause retain cycles when they have smart ptr arguments
    virtual void clearCallbacks() { mConnectionStatusHandler = NoOP; mServerConnectionHandler = NoOP; inherited::clearCallbacks(); }

    /// make sure handlers are cleared as soon as connection closes
    /// @note this is for connections that only live by themselves and should deallocate when they close. As handlers might hold
    ///   smart pointers to the connection, it is essential the handlers are cleared
    void setClearHandlersAtClose() { mClearHandlersAtClose = true; }

    /// get server (listening) socketComm
    /// @return NULL if this is not a client connection, server listening socketComm otherwise
    SocketCommPtr getServerConnection() { return mServerConnection; }

  private:
    void freeAddressInfo();
    ErrorPtr socketError(int aSocketFd);
    ErrorPtr connectNextAddress();
    bool connectionMonitorHandler(int aFd, int aPollFlags);
    void internalCloseConnection();
    virtual void dataExceptionHandler(int aFd, int aPollFlags);

    bool connectionAcceptHandler(int aFd, int aPollFlags);
    void passClientConnection(int aFD, SocketCommPtr aServerConnection); // used by listening SocketComm to pass accepted client connection to child SocketComm
    SocketCommPtr returnClientConnection(SocketCommPtr aClientConnection); // used to notify listening SocketComm when client connection ends

  };


  #if ENABLE_SOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT

  // FIXME: refactor to have SocketComm as the event source

  namespace P44Script {

    class SocketObj;
  
    /// represents a socket
    /// Note: is an event source, but does not expose it directly, only via SocketMessageObjs
    class SocketObj : public StructuredLookupObject, public EventSource
    {
      typedef StructuredLookupObject inherited;
      SocketCommPtr mSocket;
    public:
      SocketObj(SocketCommPtr aSocket);
      virtual ~SocketObj();
      virtual string getAnnotation() const P44_OVERRIDE { return "socket"; };
      SocketCommPtr socket() { return mSocket; }
    private:
      void gotData(ErrorPtr aError);
    };


    /// represents the global objects related to http
    class SocketLookup : public BuiltInMemberLookup
    {
      typedef BuiltInMemberLookup inherited;
    public:
      SocketLookup();
    };

  }

  #endif // ENABLE_SOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT


} // namespace p44


#endif /* defined(__p44utils__socketcomm__) */
