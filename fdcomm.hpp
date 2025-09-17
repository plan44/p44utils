//  SPDX-License-Identifier: GPL-3.0-or-later
//
//  Copyright (c) 2013-2024 plan44.ch / Lukas Zeller, Zurich, Switzerland
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

#ifndef __p44utils__fdcomm__
#define __p44utils__fdcomm__

#include "p44utils_main.hpp"


// unix I/O and network
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/param.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#ifndef ESP_PLATFORM
#include <termios.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif


using namespace std;

namespace p44 {


  class FdComm;


  typedef boost::intrusive_ptr<FdComm> FdCommPtr;

  /// wrapper for non-blocking I/O on a file descriptor
  class FdComm : public P44Obj
  {
    StatusCB mReceiveHandler;
    StatusCB mTransmitHandler;

  protected:

    int mDataFd;
    MainLoop &mMainLoop;
    char mDelimiter;
    string mReceiveBuffer;
    string mTransmitBuffer;
    size_t mDelimiterPos;
    bool mUnknownReadyBytes;

  public:

    FdComm(MainLoop &aMainLoop = MainLoop::currentMainLoop());
    virtual ~FdComm();

    /// place to attach a related object
    P44ObjPtr mRelatedObject;

    /// Set file descriptor
    /// @param aFd the file descriptor to monitor, -1 to cancel monitoring
    /// @param aUnknownReadyBytes if set, this indicates the FD might not be able to correctly
    ///   report the number of bytes, and the receive handler should be called even if
    ///   the number of bytes reported by FIONREAD at POLLIN is zero. The handler must NOT
    ///   rely on numBytesReady() and just read what's available.
    /// @note aFd should be non-blocking when aUnknownReadyBytes is set
    void setFd(int aFd, bool aUnknownReadyBytes = false);

    /// Stop monitoring (unregister MainLoop callbacks) and close the file descriptor
    void stopMonitoringAndClose();

    /// Get file descriptor
    int getFd() { return mDataFd; };

    /// write data (non-blocking)
    /// @param aNumBytes number of bytes to transfer
    /// @param aBytes pointer to buffer to be sent
    /// @param aError reference to ErrorPtr. Will be left untouched if no error occurs
    /// @return number ob bytes actually written, can be 0 (e.g. if connection is still in process of opening)
    virtual size_t transmitBytes(size_t aNumBytes, const uint8_t *aBytes, ErrorPtr &aError);

    /// transmit string
    /// @param aString string to transmit
    /// @note intended for datagrams. Use transmitBytes to be able to handle partial transmission or
    ///   sendString() for buffered transfers
    bool transmitString(const string &aString);

    /// @return number of bytes ready for read
    size_t numBytesReady();

    /// read data (non-blocking)
    /// @param aNumBytes max number of bytes to receive
    /// @param aBytes pointer to buffer to store received bytes
    /// @param aError reference to ErrorPtr. Will be left untouched if no error occurs
    /// @return number of bytes actually read
    virtual size_t receiveBytes(size_t aNumBytes, uint8_t *aBytes, ErrorPtr &aError);

    /// @return true when receiving is set to re-assemble delimited messages automatically
    /// @note if this returns true, receiveDelimitedString() should be used in receive handler
    bool delimitedReceive() { return mDelimiter!=0; }

    /// Can be called from receive handler when setReceiveHandler() was set up with a delimiter
    /// to get the accumulated delimited string
    /// @param aString will contain the delimited string, without delimiters included
    /// @return true if a delimited string could be returned
    bool receiveDelimitedString(string &aString);

    /// Send string, buffer and transmit later if needed
    /// @param aString string to send
    void sendString(const string &aString);

    /// read data into string
    ErrorPtr receiveIntoString(string &aString, ssize_t aMaxBytes = -1);

    /// read data and append to string
    ErrorPtr receiveAndAppendToString(string &aString, ssize_t aMaxBytes = -1);

    /// install callback for data becoming ready to read
    /// @param aReceiveHandler will be called when data is ready for reading (receiveBytes()) or an asynchronous error occurs on the file descriptor
    /// @param aDelimiter if set, aReceiveHandler will only be called after seeing the specified delimiter in the incoming stream.
    ///   use receiveDelimitedString() to get the (internally accumulated) delimited string. Note that when using delimiter,
    ///   data will be consumed internally into the chunkbuffer, so numBytesReady() and receiveBytes() should not be used.
    void setReceiveHandler(StatusCB aReceiveHandler, char aDelimiter = 0);

    /// install callback for file descriptor ready for accepting new data to send
    /// @param aTransmitHandler will be called when file descriptor is ready to transmit more data (using transmitBytes())
    void setTransmitHandler(StatusCB aTransmitHandler);

    /// make non-blocking
    /// @param aFd optional; fd to switch to non-blocking, defaults to this FdConn's fd set with setFd()
    void makeNonBlocking(int aFd = -1);

    /// clear all callbacks
    /// @note this is important because handlers might cause retain cycles when they have smart ptr arguments
    virtual void clearCallbacks() { mReceiveHandler = NoOP; mTransmitHandler = NoOP; }

  protected:
    /// this is intended to be overridden in subclases, and is called when
    /// an exception (HUP or error) occurs on the file descriptor
    virtual void dataExceptionHandler(int aFd, int aPollFlags);

  private:

    bool dataMonitorHandler(int aFd, int aPollFlags);
    void checkReceiveData();
    bool sendBufferedData();

  };


  class FdStringCollector : public FdComm
  {
    typedef FdComm inherited;

    bool mEnded; ///< set when FD returns error or HUP
    StatusCB mEndedCallback; ///< called when collecting ends (after setup by collectToEnd())

  public:

    string mCollectedData; ///< all data received from the fd is collected into this string

    FdStringCollector(MainLoop &aMainLoop);

    /// collect until file descriptor does not provide any more data
    void collectToEnd(StatusCB aEndedCallback);

    /// clear all callbacks
    /// @note this is important because handlers might cause retain cycles when they have smart ptr arguments
    virtual void clearCallbacks() { mEndedCallback = NoOP; inherited::clearCallbacks(); }

  protected:

    virtual void dataExceptionHandler(int aFd, int aPollFlags);

  private:

    void gotData(ErrorPtr aError);

  };
  typedef boost::intrusive_ptr<FdStringCollector> FdStringCollectorPtr;


} // namespace p44


#endif /* defined(__p44utils__fdcomm__) */
