//
//  Copyright (c) 2013-2019 plan44.ch / Lukas Zeller, Zurich, Switzerland
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
#define FOCUSLOGLEVEL 0

#include "socketcomm.hpp"

#include <sys/ioctl.h>
#ifdef ESP_PLATFORM
const struct in6_addr in6addr_loopback = IN6ADDR_LOOPBACK_INIT;
#else
#include <sys/poll.h>
#endif

using namespace p44;

SocketComm::SocketComm(MainLoop &aMainLoop) :
  FdComm(aMainLoop),
  connectionOpen(false),
  isConnecting(false),
  isClosing(false),
  serving(false),
  nonLocal(true),
  clearHandlersAtClose(false),
  addressInfoList(NULL),
  currentAddressInfo(NULL),
  currentSockAddrP(NULL),
  peerSockAddrP(NULL),
  maxServerConnections(1),
  serverConnection(NULL),
  broadcast(false),
  connectionFd(-1),
  protocolFamily(PF_UNSPEC),
  socketType(SOCK_STREAM),
  protocol(0),
  connectionLess(false)
{
}


SocketComm::~SocketComm()
{
  FOCUSLOG("~SocketComm()")
  if (!isClosing) {
    internalCloseConnection();
  }
}


void SocketComm::setConnectionParams(const char* aHostNameOrAddress, const char* aServiceOrPortOrSocket, int aSocketType, int aProtocolFamily, int aProtocol, const char* aInterface)
{
  closeConnection();
  hostNameOrAddress = nonNullCStr(aHostNameOrAddress);
  serviceOrPortOrSocket = nonNullCStr(aServiceOrPortOrSocket);
  protocolFamily = aProtocolFamily;
  socketType = aSocketType;
  protocol = aProtocol;
  interface = nonNullCStr(aInterface);
  connectionLess = socketType==SOCK_DGRAM;
}


// MARK: - becoming a server

ErrorPtr SocketComm::startServer(ServerConnectionCB aServerConnectionHandler, int aMaxConnections)
{
  ErrorPtr err;

  struct sockaddr *saP = NULL;
  socklen_t saLen = 0;
  int proto = IPPROTO_IP;
  int one = 1;
  int socketFD = -1;

  maxServerConnections = aMaxConnections;
  // check for protocolfamily auto-choice
  if (protocolFamily==PF_UNSPEC) {
    // not specified, choose default
    #ifndef ESP_PLATFORM
    if (serviceOrPortOrSocket.size()>1 && serviceOrPortOrSocket[0]=='/') {
      protocolFamily = PF_LOCAL; // absolute paths are considered local sockets
    }
    else
    #endif  // !ESP_PLATFORM
    {
      protocolFamily = PF_INET; // otherwise, default to IPv4 for now
    }
  }
  // - protocol derived from socket type
  if (protocol==0) {
    // determine protocol automatically from socket type
    if (socketType==SOCK_STREAM)
      proto = IPPROTO_TCP;
    else
      proto = IPPROTO_UDP;
  }
  else
    proto = protocol;
  // now start server
  if (protocolFamily==PF_INET) {
    // IPv4 socket
    struct servent *pse;
    // - create suitable socket address
    struct sockaddr_in *sinP = new struct sockaddr_in;
    saLen = sizeof(struct sockaddr_in);
    saP = (struct sockaddr *)sinP;
    memset((char *)saP, 0, saLen);
    // - set listening socket address
    sinP->sin_family = (sa_family_t)protocolFamily;
    if (nonLocal)
      sinP->sin_addr.s_addr = htonl(INADDR_ANY);
    else
      sinP->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    // get service / port
    #ifndef ESP_PLATFORM
    // - look up port/service by name
    if ((pse = getservbyname(serviceOrPortOrSocket.c_str(), NULL)) != NULL) {
      sinP->sin_port = htons(ntohs((in_port_t)pse->s_port));
    }
    else
    #endif  // !ESP_PLATFORM
    // - numeric port number
    if ((sinP->sin_port = htons((in_port_t)atoi(serviceOrPortOrSocket.c_str()))) == 0) {
      err = Error::err<SocketCommError>(SocketCommError::CannotResolve, "Unknown service/port name");
    }
  } else if (protocolFamily==PF_INET6) {
    // IPv6 socket
    struct servent *pse;
    // - create suitable socket address
    struct sockaddr_in6 *sinP = new struct sockaddr_in6;
    saLen = sizeof(struct sockaddr_in6);
    saP = (struct sockaddr *)sinP;
    memset((char *)saP, 0, saLen);
    // - set listening socket address
    sinP->sin6_family = (sa_family_t)protocolFamily;
    if (nonLocal)
      sinP->sin6_addr = in6addr_any;
    else
      sinP->sin6_addr = in6addr_loopback;
    // get service / port
    #ifndef ESP_PLATFORM
    if ((pse = getservbyname(serviceOrPortOrSocket.c_str(), NULL)) != NULL) {
      sinP->sin6_port = htons(ntohs((in_port_t)pse->s_port));
    }
    else
    #endif  // !ESP_PLATFORM
    if ((sinP->sin6_port = htons((in_port_t)atoi(serviceOrPortOrSocket.c_str()))) == 0) {
      err = Error::err<SocketCommError>(SocketCommError::CannotResolve, "Unknown service/port name");
    }
  }
  #ifndef ESP_PLATFORM
  else if (protocolFamily==PF_LOCAL) {
    // Local (UNIX) socket
    // - create suitable socket address
    struct sockaddr_un *sunP = new struct sockaddr_un;
    saLen = sizeof(struct sockaddr_un);
    saP = (struct sockaddr *)sunP;
    memset((char *)saP, 0, saLen);
    // - set socket address
    sunP->sun_family = (sa_family_t)protocolFamily;
    strncpy(sunP->sun_path, serviceOrPortOrSocket.c_str(), sizeof (sunP->sun_path));
    sunP->sun_path[sizeof (sunP->sun_path) - 1] = '\0'; // emergency terminator
    // - protocol for local socket is not specific
    proto = 0;
  }
  #endif // !ESP_PLATFORM
  else {
    // TODO: implement other protocol families
    err = Error::err<SocketCommError>(SocketCommError::Unsupported, "Unsupported protocol family");
  }
  // now create and configure socket
  if (Error::isOK(err)) {
    socketFD = socket(protocolFamily, socketType, proto);
    if (socketFD<0) {
      err = SysError::errNo("Cannot create server socket: ");
    }
    else {
      // socket created, set options
      if (setsockopt(socketFD, SOL_SOCKET, SO_REUSEADDR, (char *)&one, (int)sizeof(one)) == -1) {
        err = SysError::errNo("Cannot setsockopt(SO_REUSEADDR): ");
      }
      else {
        #if defined(ESP_PLATFORM) || defined(__APPLE__)
        if (!interface.empty()) {
          err = TextError::err("SO_BINDTODEVICE not supported on macOS");
        }
        #else
        if (!interface.empty() && setsockopt(socketFD, SOL_SOCKET, SO_BINDTODEVICE, interface.c_str(), interface.size()) == -1) {
          err = SysError::errNo("Cannot setsockopt(SO_BINDTODEVICE): ");
        }
        #endif
        else {
          // options ok, bind to address
          if (::bind(socketFD, saP, saLen) < 0) {
            err = SysError::errNo("Cannot bind socket (server already running?): ");
          }
        }
      }
    }
  }
  // listen
  if (Error::isOK(err)) {
    if (socketType==SOCK_STREAM && listen(socketFD, maxServerConnections) < 0) {
      err = SysError::errNo("Cannot listen on socket: ");
    }
    else {
      // listen ok or not needed, make non-blocking
      makeNonBlocking(socketFD);
      // now socket is ready, register in mainloop to receive connections
      connectionFd = socketFD;
      serving = true;
      serverConnectionHandler = aServerConnectionHandler;
      // - install callback for when FD becomes writable (or errors out)
      mainLoop.registerPollHandler(
        connectionFd,
        POLLIN,
        boost::bind(&SocketComm::connectionAcceptHandler, this, _1, _2)
      );
    }
  }
  if (saP) {
    delete saP; saP = NULL;
  }
  return err;
}


bool SocketComm::connectionAcceptHandler(int aFd, int aPollFlags)
{
  ErrorPtr err;
  if (aPollFlags & POLLIN) {
    // server socket has data, means connection waiting to get accepted
    socklen_t fsinlen;
    struct sockaddr fsin;
    int clientFD = -1;
    fsinlen = sizeof(fsin);
    clientFD = accept(connectionFd, (struct sockaddr *) &fsin, &fsinlen);
    if (clientFD>0) {
      // get address and port of incoming connection
      char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
      #ifndef ESP_PLATFORM
      if (protocolFamily==PF_LOCAL) {
        // no real address and port
        strcpy(hbuf,"local");
        strcpy(sbuf,"local_socket");
      }
      else
      #endif // !ESP_PLATFORM
      {
        #ifdef ESP_PLATFORM
        #warning "%%% ESP32 version of getnameinfo missing"
        // TODO: find how to use getnameinfo on ESP32
        #else
        int s = getnameinfo(
          &fsin, fsinlen,
          hbuf, sizeof hbuf,
          sbuf, sizeof sbuf,
          NI_NUMERICHOST | NI_NUMERICSERV
        );
        if (s!=0)
        #endif
        {
          strcpy(hbuf,"<unknown>");
          strcpy(sbuf,"<unknown>");
        }
      }
      // actually accepted
      // - establish keepalive checks, so we'll detect eventually when client connection breaks w/o FIN
      int one = 1;
      if (setsockopt(clientFD, SOL_SOCKET, SO_KEEPALIVE, (char *)&one, (int)sizeof(one)) == -1) {
        LOG(LOG_WARNING, "Cannot set SO_KEEPALIVE for new connection");
      }
      // - call handler to create child connection
      SocketCommPtr clientComm;
      if (serverConnectionHandler) {
        clientComm = serverConnectionHandler(this);
      }
      if (clientComm) {
        // - set host/port
        clientComm->hostNameOrAddress = hbuf;
        clientComm->serviceOrPortOrSocket = sbuf;
        // - remember
        clientConnections.push_back(clientComm);
        LOG(LOG_DEBUG, "New client connection accepted from %s:%s (now %zu connections)", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str(), clientConnections.size());
        // - pass connection to child
        clientComm->passClientConnection(clientFD, this);
      }
      else {
        // can't handle connection, close immediately
        LOG(LOG_NOTICE, "Connection not accepted from %s:%s - shut down", hbuf, sbuf);
        shutdown(clientFD, SHUT_RDWR);
        close(clientFD);
      }
    }
  }
  // handled
  return true;
}


void SocketComm::passClientConnection(int aFd, SocketCommPtr aServerConnection)
{
  // make non-blocking
  makeNonBlocking(aFd);
  // save and mark open
  serverConnection = aServerConnection;
  // set Fd and let FdComm base class install receive & transmit handlers
  setFd(aFd);
  // save fd for my own use
  connectionFd = aFd;
  isConnecting = false;
  connectionOpen = true;
  // call handler if defined
  if (connectionStatusHandler) {
    // connection ok
    connectionStatusHandler(this, ErrorPtr());
  }
}



SocketCommPtr SocketComm::returnClientConnection(SocketCommPtr aClientConnection)
{
  SocketCommPtr endingConnection;
  // remove the client connection from the list
  for (SocketCommList::iterator pos = clientConnections.begin(); pos!=clientConnections.end(); ++pos) {
    if (pos->get()==aClientConnection.get()) {
      // found, keep around until really done with everything
      endingConnection = *pos;
      // remove from list
      clientConnections.erase(pos);
      break;
    }
  }
  LOG(LOG_DEBUG, "Client connection terminated (now %zu connections)", clientConnections.size());
  // return connection object to prevent premature deletion
  return endingConnection;
}




// MARK: - connecting to a client


bool SocketComm::connectable()
{
  return !hostNameOrAddress.empty();
}



ErrorPtr SocketComm::initiateConnection()
{
  int res;
  ErrorPtr err;

  if (!connectionOpen && !isConnecting && !serverConnection) {
    freeAddressInfo();
    // check for protocolfamily auto-choice
    if (protocolFamily==PF_UNSPEC) {
      // not specified, choose local socket if service spec begins with slash
      #ifndef ESP_PLATFORM
      if (serviceOrPortOrSocket.size()>1 && serviceOrPortOrSocket[0]=='/') {
        protocolFamily = PF_LOCAL; // absolute paths are considered local sockets
      }
      #endif  // !ESP_PLATFORM
    }
    #ifndef ESP_PLATFORM
    if (protocolFamily==PF_LOCAL) {
      // local socket -> just connect, no lists to try
      LOG(LOG_DEBUG, "Initiating local socket %s connection", serviceOrPortOrSocket.c_str());
      hostNameOrAddress = "local"; // set it for log display
      // synthesize address info for unix socket, because standard UN*X getaddrinfo() call usually does not handle PF_LOCAL
      addressInfoList = new struct addrinfo;
      memset(addressInfoList, 0, sizeof(addrinfo));
      addressInfoList->ai_family = protocolFamily;
      addressInfoList->ai_socktype = socketType;
      addressInfoList->ai_protocol = protocol;
      struct sockaddr_un *sunP = new struct sockaddr_un;
      addressInfoList->ai_addr = (struct sockaddr *)sunP;
      addressInfoList->ai_addrlen = sizeof(struct sockaddr_un);
      memset((char *)sunP, 0, addressInfoList->ai_addrlen);
      sunP->sun_family = (sa_family_t)protocolFamily;
      strncpy(sunP->sun_path, serviceOrPortOrSocket.c_str(), sizeof (sunP->sun_path));
      sunP->sun_path[sizeof (sunP->sun_path) - 1] = '\0'; // emergency terminator
    }
    else
    #endif // !ESP_PLATFORM
    {
      // assume internet connection -> get list of possible addresses and try them
      if (hostNameOrAddress.empty() && !connectionLess) {
        err = Error::err<SocketCommError>(SocketCommError::NoParams, "Missing host name or address");
        goto done;
      }
      // try to resolve host and service name (at least: service name)
      struct addrinfo hint;
      memset(&hint, 0, sizeof(addrinfo));
      hint.ai_flags = 0; // no flags
      hint.ai_family = protocolFamily;
      hint.ai_socktype = socketType;
      hint.ai_protocol = protocol;
      res = getaddrinfo(hostNameOrAddress.empty() ? NULL : hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str(), &hint, &addressInfoList);
      if (res!=0) {
        // error
        #ifdef ESP_PLATFORM
        err = Error::err<SocketCommError>(SocketCommError::CannotResolve, "getaddrinfo error %d", res);
        #else
        err = Error::err<SocketCommError>(SocketCommError::CannotResolve, "getaddrinfo error %d: %s", res, gai_strerror(res));
        #endif
        DBGLOG(LOG_DEBUG, "SocketComm: getaddrinfo failed: %s", err->text());
        goto done;
      }
    }
    // now try all addresses in the list
    // - init iterator pointer
    currentAddressInfo = addressInfoList;
    // - try connecting first address
    LOG(LOG_DEBUG, "Initializing socket for connection to %s:%s", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str());
    err = connectNextAddress();
  }
done:
  if (Error::notOK(err) && connectionStatusHandler) {
    connectionStatusHandler(this, err);
  }
  // return it
  return err;
}


void SocketComm::freeAddressInfo()
{
  if (!currentAddressInfo && addressInfoList) {
    // entire list consumed, free it
    freeaddrinfo(addressInfoList);
    addressInfoList = NULL;
  }
}


ErrorPtr SocketComm::connectNextAddress()
{
  int res;
  ErrorPtr err;
  const int one = 1;

  // close possibly not fully open connection FD
  internalCloseConnection();
  // try to create a socket
  int socketFD = -1;
  // as long as we have more addresses to check and not already connecting
  bool startedConnecting = false;
  while (currentAddressInfo && !startedConnecting) {
    err.reset();
    socketFD = socket(currentAddressInfo->ai_family, currentAddressInfo->ai_socktype, currentAddressInfo->ai_protocol);
    if (socketFD==-1) {
      err = SysError::errNo("Cannot create client socket: ");
    }
    else {
      // usable address found, socket created
      // - make socket non-blocking
      makeNonBlocking(socketFD);
      // Now we have a socket
      if (connectionLess) {
        // UDP: no connect phase
        // - enable for broadcast if requested
        if (broadcast) {
          // needs SO_BROADCAST
          if (setsockopt(socketFD, SOL_SOCKET, SO_BROADCAST, (char *)&one, (int)sizeof(one)) == -1) {
            err = SysError::errNo("Cannot setsockopt(SO_BROADCAST): ");
          }
        }
        if (Error::isOK(err) && serving) {
          // We want this socket to be ready to receive messages
          // Note: w/o the following initialisation, the socket remains unbound for now,
          //   but issuing the first send will bind it to a random port to have an identity
          //   (which peers can use to send an answer)
          #ifdef ESP_PLATFORM
          #warning "%%% ESP32 version of getnameinfo missing"
          // TODO: find how to use getnameinfo on ESP32
          err = TextError::err("ESP32: Cannot determine port and thus cannot bind to INADDR_ANY");
          #else
          // Get port number
          char sbuf[NI_MAXSERV];
          // If we explicitly bind here, the socket will have the port number specified in setConnectionParams()
          int s = getnameinfo(
            currentAddressInfo->ai_addr, currentAddressInfo->ai_addrlen,
            NULL, 0, // no host address
            sbuf, sizeof sbuf, // only service/port
            NI_NUMERICSERV
          );
          if (s==0) {
            // convert to numeric port number
            int port;
            if (sscanf(sbuf, "%d", &port)==1) {
              if (currentAddressInfo->ai_family==AF_INET6) {
                struct sockaddr_in6 recvaddr;
                memset(&recvaddr, 0, sizeof recvaddr);
                recvaddr.sin6_family = AF_INET6;
                recvaddr.sin6_port = htons(port);
                if (nonLocal)
                  recvaddr.sin6_addr = in6addr_any;
                else
                  recvaddr.sin6_addr = in6addr_loopback;
                if (::bind(socketFD, (struct sockaddr *)&recvaddr, sizeof recvaddr) == -1) {
                  err = SysError::errNo("Cannot bind to in6addr_any/in6addr_loopback: ");
                }
              }
              else if (currentAddressInfo->ai_family==AF_INET) {
                // bind connectionless socket to INADDR_ANY to receive broadcasts at all
                struct sockaddr_in recvaddr;
                memset(&recvaddr, 0, sizeof recvaddr);
                recvaddr.sin_family = AF_INET;
                recvaddr.sin_port = htons(port);
                if (nonLocal)
                  recvaddr.sin_addr.s_addr = htonl(INADDR_ANY);
                else
                  recvaddr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
                if (::bind(socketFD, (struct sockaddr*)&recvaddr, sizeof recvaddr) == -1) {
                  err = SysError::errNo("Cannot bind to INADDR_ANY/INADDR_LOOPBACK: ");
                }
              }
            }
          }
          #endif // !ESP_PLATFORM
        } // serving (UDP socket bound to specific port)
        if (Error::isOK(err)) {
          startedConnecting = true;
          // save valid address info for later use (UDP needs it to send datagrams)
          if (currentSockAddrP)
            free(currentSockAddrP);
          currentSockAddrLen = currentAddressInfo->ai_addrlen;
          currentSockAddrP = (sockaddr *)malloc(currentSockAddrLen);
          memcpy(currentSockAddrP, currentAddressInfo->ai_addr, currentAddressInfo->ai_addrlen);
        }
      } // connectionLess
      else {
        // TCP: initiate connection
        LOG(LOG_DEBUG, "- Attempting connection with address family = %d, protocol = %d, addrlen=%d/sizeof=%zu", currentAddressInfo->ai_family, currentAddressInfo->ai_protocol, currentAddressInfo->ai_addrlen, sizeof(*(currentAddressInfo->ai_addr)));
        res = connect(socketFD, currentAddressInfo->ai_addr, currentAddressInfo->ai_addrlen);
        if (res==0 || errno==EINPROGRESS) {
          // connection initiated (or already open, but connectionMonitorHandler will take care in both cases)
          startedConnecting = true;
        }
        else {
          // immediate error connecting
          err = SysError::errNo("Cannot connect: ");
        }
      }
    }
    // advance to next address
    currentAddressInfo = currentAddressInfo->ai_next;
  }
  if (!startedConnecting) {
    // exhausted addresses without starting to connect
    if (!err) err = Error::err<SocketCommError>(SocketCommError::NoConnection, "No connection could be established");
    LOG(LOG_DEBUG, "Cannot initiate connection to %s:%s - %s", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str(), err->text());
  }
  else {
    if (!connectionLess) {
      // connection in progress
      isConnecting = true;
      // - save FD
      connectionFd = socketFD;
      // - install callback for when FD becomes writable (or errors out)
      mainLoop.registerPollHandler(
        connectionFd,
        POLLOUT,
        boost::bind(&SocketComm::connectionMonitorHandler, this, _1, _2)
      );
    }
    else {
      // UDP socket successfully created
      LOG(LOG_DEBUG, "Connectionless socket ready for address family = %d, protocol = %d", protocolFamily, protocol);
      connectionOpen = true;
      isConnecting = false;
      currentAddressInfo = NULL; // no more addresses to check
      // immediately use socket for I/O
      setFd(socketFD);
      // call handler if defined
      if (connectionStatusHandler) {
        // connection ok
        connectionStatusHandler(this, ErrorPtr());
      }
    }
  }
  // clean up if list processed
  freeAddressInfo();
  // return status
  return err;
}


// MARK: - general connection handling


ErrorPtr SocketComm::socketError(int aSocketFd)
{
  ErrorPtr err;
  int result;
  socklen_t result_len = sizeof(result);
  if (getsockopt(aSocketFd, SOL_SOCKET, SO_ERROR, &result, &result_len) < 0) {
    // error, fail somehow, close socket
    err = SysError::errNo("Cant get socket error status: ");
  }
  else {
    err = SysError::err(result, "Socket Error status: ");
  }
  return err;
}



bool SocketComm::connectionMonitorHandler(int aFd, int aPollFlags)
{
  ErrorPtr err;
  if ((aPollFlags & POLLOUT) && isConnecting) {
    // became writable, check status
    err = socketError(aFd);
  }
  else if (aPollFlags & POLLHUP) {
    err = Error::err<SocketCommError>(SocketCommError::HungUp, "Connection HUP while opening (= connection rejected)");
  }
  else if (aPollFlags & POLLERR) {
    err = socketError(aFd);
  }
  // now check if successful
  if (Error::isOK(err)) {
    // successfully connected
    connectionOpen = true;
    isConnecting = false;
    currentAddressInfo = NULL; // no more addresses to check
    freeAddressInfo();
    LOG(LOG_DEBUG, "Connection to %s:%s established", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str());
    // call handler if defined
    if (connectionStatusHandler) {
      // connection ok
      connectionStatusHandler(this, ErrorPtr());
    }
    // let FdComm base class operate open connection (will install handlers)
    setFd(aFd);
  }
  else {
    // this attempt has failed, try next (if any)
    LOG(LOG_DEBUG, "- Connection attempt failed: %s", err->text());
    // this will return no error if we have another address to try
    err = connectNextAddress();
    if (err) {
      // no next attempt started, report error
      LOG(LOG_WARNING, "Connection to %s:%s failed: %s", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str(), err->text());
      if (connectionStatusHandler) {
        connectionStatusHandler(this, err);
      }
      freeAddressInfo();
      internalCloseConnection();
    }
  }
  // handled
  return true;
}


void SocketComm::setConnectionStatusHandler(SocketCommCB aConnectedHandler)
{
  // set handler
  connectionStatusHandler = aConnectedHandler;
}



void SocketComm::closeConnection()
{
  if (connectionOpen && !isClosing) {
    isClosing = true; // prevent doing it more than once due to handlers called
    // report to handler
    LOG(LOG_DEBUG, "Connection with %s:%s explicitly closing", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str());
    if (connectionStatusHandler) {
      // connection ok
      ErrorPtr err = Error::err<SocketCommError>(SocketCommError::Closed, "Connection closed");
      connectionStatusHandler(this, err);
    }
    // close the connection
    internalCloseConnection();
  }
}


void SocketComm::internalCloseConnection()
{
  isClosing = true; // prevent doing it more than once due to handlers called
  if (!connectionLess && serving) {
    // serving TCP socket
    // - close listening socket
    mainLoop.unregisterPollHandler(connectionFd);
    close(connectionFd);
    connectionFd = -1;
    serving = false;
    // - close all child connections (closing will remove them from the list)
    while (clientConnections.size()>0) {
      SocketCommPtr conn = *clientConnections.begin();
      conn->closeConnection();
      conn->clearCallbacks(); // clear callbacks to break possible retain cycles
    }
  }
  else if (connectionOpen || isConnecting) {
    // stop monitoring data connection and close descriptor
    if (connectionFd==getFd()) connectionFd = -1; // is the same descriptor, don't double-close
    stopMonitoringAndClose(); // close the data connection
    // to make sure, also unregister handler for connectionFd (in case FdComm had no fd set yet)
    mainLoop.unregisterPollHandler(connectionFd);
    if (serverConnection) {
      shutdown(connectionFd, SHUT_RDWR);
    }
    if (connectionFd>0) {
      close(connectionFd);
      connectionFd = -1;
    }
    connectionOpen = false;
    isConnecting = false;
    broadcast = false;
    // if this was a client connection to our server, let server know
    if (serverConnection) {
      serverConnection->returnClientConnection(this);
      serverConnection = NULL;
    }
  }
  // free the address info
  if (currentSockAddrP) {
    free(currentSockAddrP);
    currentSockAddrP = NULL;
  }
  if (peerSockAddrP) {
    free(peerSockAddrP);
    peerSockAddrP = NULL;
  }
  // now clear handlers if requested
  if (clearHandlersAtClose) {
    clearCallbacks();
  }
  isClosing = false;
}


bool SocketComm::connected()
{
  return connectionOpen && !isClosing;
}


bool SocketComm::connecting()
{
  return isConnecting;
}


// MARK: - connectionless data exchange


size_t SocketComm::transmitBytes(size_t aNumBytes, const uint8_t *aBytes, ErrorPtr &aError)
{
  if (connectionLess) {
    if (dataFd<0)
      return 0; // not ready yet
    ssize_t res = sendto(dataFd, aBytes, aNumBytes, 0, currentSockAddrP, currentSockAddrLen);
    if (res<0) {
      aError = SysError::errNo("SocketComm::transmitBytes (connectionless): ");
      return 0; // nothing transmitted
    }
    return res;
  }
  else {
    return inherited::transmitBytes(aNumBytes, aBytes, aError);
  }
}


size_t SocketComm::receiveBytes(size_t aNumBytes, uint8_t *aBytes, ErrorPtr &aError)
{
  if (connectionLess) {
    if (dataFd>=0) {
      // read
      ssize_t res = 0;
      if (aNumBytes>0) {
        if (peerSockAddrP)
          free(peerSockAddrP);
        peerSockAddrLen = sizeof(sockaddr); // pass in buffer size
        peerSockAddrP = (sockaddr *)malloc(peerSockAddrLen); // prepare buffer
        res = recvfrom(dataFd, (void *)aBytes, aNumBytes, 0, peerSockAddrP, &peerSockAddrLen);
        if (res<0) {
          if (errno==EWOULDBLOCK)
            return 0; // nothing received
          else {
            aError = SysError::errNo("SocketComm::receiveBytes: ");
            return 0; // nothing received
          }
        }
        return res;
      }
    }
    return 0; // no fd set, nothing to read
  }
  else {
    return inherited::receiveBytes(aNumBytes, aBytes, aError);
  }
}


bool SocketComm::getDatagramOrigin(string &aAddress, string &aPort)
{
  if (peerSockAddrP) {
    // get address and port of incoming connection
    #ifdef ESP_PLATFORM
    #warning "%%% ESP32 version of getnameinfo missing"
    // TODO: find how to use getnameinfo on ESP32
    #else
    char hbuf[NI_MAXHOST], sbuf[NI_MAXSERV];
    int s = getnameinfo(
      peerSockAddrP, peerSockAddrLen,
      hbuf, sizeof hbuf,
      sbuf, sizeof sbuf,
      NI_NUMERICHOST | NI_NUMERICSERV
    );
    if (s==0) {
      aAddress = hbuf;
      aPort = sbuf;
      return true;
    }
    #endif
  }
  return false;
}


// MARK: - handling data exception


void SocketComm::dataExceptionHandler(int aFd, int aPollFlags)
{
  SocketCommPtr keepMyselfAlive(this);
  DBGLOG(LOG_DEBUG, "SocketComm::dataExceptionHandler(fd==%d, pollflags==0x%X)", aFd, aPollFlags);
  if (!isClosing) {
    if (aPollFlags & POLLHUP) {
      // other end has closed connection
      // - report
      if (connectionStatusHandler) {
        // report reason for closing
        connectionStatusHandler(this, Error::err<SocketCommError>(SocketCommError::HungUp, "Connection closed (HUP)"));
      }
    }
    else if (aPollFlags & POLLIN) {
      // Note: on linux a socket closed server side does not return POLLHUP, but POLLIN with no data
      // alerted for read, but nothing to read any more: assume connection closed
      ErrorPtr err = socketError(aFd);
      if (Error::isOK(err))
        err = Error::err<SocketCommError>(SocketCommError::HungUp, "Connection closed (POLLIN but no data -> interpreted as HUP)");
      DBGLOG(LOG_DEBUG, "Connection to %s:%s has POLLIN but no data; error: %s", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str(), err->text());
      // - report
      if (connectionStatusHandler) {
        // report reason for closing
        connectionStatusHandler(this, err);
      }
    }
    else if (aPollFlags & POLLERR) {
      // error
      ErrorPtr err = socketError(aFd);
      LOG(LOG_WARNING, "Connection to %s:%s reported error: %s", hostNameOrAddress.c_str(), serviceOrPortOrSocket.c_str(), err->text());
      // - report
      if (connectionStatusHandler) {
        // report reason for closing
        connectionStatusHandler(this, err);
      }
    }
    else {
      // NOP
      return;
    }
    // - shut down (Note: if nobody else retains the connection except the server SocketComm, this will delete the connection)
    internalCloseConnection();
  }
}


// MARK: - script support

#if ENABLE_SOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT

using namespace P44Script;

SocketMessageObj::SocketMessageObj(SocketObj* aSocketObj) :
  inherited(""),
  mSocketObj(aSocketObj)
{
}


string SocketMessageObj::getAnnotation() const
{
  return "UDP message";
}


TypeInfo SocketMessageObj::getTypeInfo() const
{
  return inherited::getTypeInfo()|oneshot|keeporiginal; // returns the request only once, must keep the original
}


EventSource* SocketMessageObj::eventSource() const
{
  return static_cast<EventSource*>(mSocketObj);
}


string SocketMessageObj::stringValue() const
{
  return mSocketObj ? mSocketObj->lastDatagram : "";
}




// send(data)
static const BuiltInArgDesc send_args[] = { { any } };
static const size_t send_numargs = sizeof(send_args)/sizeof(BuiltInArgDesc);
static void send_func(BuiltinFunctionContextPtr f)
{
  SocketObj* s = dynamic_cast<SocketObj*>(f->thisObj().get());
  assert(s);
  ErrorPtr err;
  string datagram = f->arg(0)->stringValue();
  size_t res = s->socket()->transmitBytes(datagram.length(), (uint8_t *)datagram.c_str(), err);
  if (Error::notOK(err)) {
    f->finish(new ErrorValue(err));
  }
  else {
    f->finish(new NumericValue(res==datagram.length()));
  }
}


// message()
static void message_func(BuiltinFunctionContextPtr f)
{
  SocketObj* s = dynamic_cast<SocketObj*>(f->thisObj().get());
  assert(s);
  // return latest message
  f->finish(new SocketMessageObj(s));
}


static const BuiltinMemberDescriptor socketFunctions[] = {
  { "send", executable|error, send_numargs, send_args, &send_func },
  { "message", executable|text|null, 0, NULL, &message_func },
  { NULL } // terminator
};

static BuiltInMemberLookup* sharedSocketFunctionLookupP = NULL;

SocketObj::SocketObj(SocketCommPtr aSocket) :
  mSocket(aSocket)
{
  if (sharedSocketFunctionLookupP==NULL) {
    sharedSocketFunctionLookupP = new BuiltInMemberLookup(socketFunctions);
    sharedSocketFunctionLookupP->isMemberVariable(); // disable refcounting
  }
  registerMemberLookup(sharedSocketFunctionLookupP);
  // handle incoming data
  mSocket->setReceiveHandler(boost::bind(&SocketObj::gotData, this, _1));
}

SocketObj::~SocketObj()
{
  mSocket->clearCallbacks();
}


void SocketObj::gotData(ErrorPtr aError)
{
  ErrorPtr err = mSocket->receiveIntoString(lastDatagram);
  if (Error::isOK(err)) {
    sendEvent(new SocketMessageObj(this));
  }
  else {
    sendEvent(new ErrorValue(err));
  }
}


// udpsocket(host, port, receive, nonlocal, broadcast)
static const BuiltInArgDesc udpsocket_args[] = { { text }, { text|numeric }, { numeric|optionalarg }, { numeric|optionalarg }, { numeric|optionalarg } };
static const size_t udpsocket_numargs = sizeof(udpsocket_args)/sizeof(BuiltInArgDesc);
static void udpsocket_func(BuiltinFunctionContextPtr f)
{
  SocketCommPtr socket = new SocketComm();
  socket->setConnectionParams(
    f->arg(0)->stringValue().c_str(),
    f->arg(1)->stringValue().c_str(),
    SOCK_DGRAM,
    PF_UNSPEC
  );
  bool rec = f->arg(2)->boolValue(); // defaults to not receiving
  bool nonlocal = f->arg(3)->boolValue(); // defaults to local only
  bool broadcast = f->arg(3)->boolValue(); // defaults to no broadcast
  socket->setAllowNonlocalConnections(nonlocal);
  socket->setDatagramOptions(rec, broadcast);
  ErrorPtr err = socket->initiateConnection();
  if (Error::isOK(err)) {
    f->finish(new SocketObj(socket));
  }
  else {
    f->finish(new ErrorValue(err));
  }
}

static const BuiltinMemberDescriptor socketGlobals[] = {
  { "udpsocket", executable|null, udpsocket_numargs, udpsocket_args, &udpsocket_func },
  { NULL } // terminator
};

SocketLookup::SocketLookup() :
  inherited(socketGlobals)
{
}

#endif // ENABLE_SOCKET_SCRIPT_FUNCS && ENABLE_P44SCRIPT
